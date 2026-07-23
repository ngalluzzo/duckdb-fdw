#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb_api/duckdb_secret.hpp"
#include "duckdb_api/package_generation_composition.hpp"
#include "duckdb_api/scan_planner.hpp"
#include "duckdb_api_extension.hpp"
#include "connector/support/local_package_source_test_fixtures.hpp"
#include "runtime/service/controlled_runtime_scenario.hpp"
#include "support/require.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

using duckdb_api_test::Require;

void LoadRepositoryPackage(duckdb::Connection &connection, const std::string &repository_root) {
	auto load = connection.Query("CALL system.main.duckdb_api_load_connector(package_root := '" + repository_root +
	                             "/connectors/github')");
	Require(!load->HasError() && load->RowCount() == 1 && load->GetValue(4, 0).GetValue<std::uint64_t>() == 4,
	        "actual DuckDB did not load the repository package");
}

void LoadRickAndMortyPackage(duckdb::Connection &connection, const std::string &repository_root) {
	auto load = connection.Query("CALL system.main.duckdb_api_load_connector(package_root := '" + repository_root +
	                             "/connectors/rickandmorty')");
	Require(!load->HasError() && load->RowCount() == 1 && load->GetValue(4, 0).GetValue<std::uint64_t>() == 2,
	        "actual DuckDB did not load the Rick and Morty repository package");
}

void CreatePackageRuntimeSecret(duckdb::Connection &connection) {
	auto secret = connection.Query("CREATE TEMPORARY SECRET package_runtime "
	                               "(TYPE duckdb_api, PROVIDER config, TOKEN 'package-runtime-token')");
	Require(!secret->HasError(), "actual DuckDB could not create the package Runtime secret");
}

duckdb_api::ValueKind KindFor(const std::string &logical_type) {
	if (logical_type == "BIGINT") {
		return duckdb_api::ValueKind::BIGINT;
	}
	if (logical_type == "VARCHAR") {
		return duckdb_api::ValueKind::VARCHAR;
	}
	if (logical_type == "BOOLEAN") {
		return duckdb_api::ValueKind::BOOLEAN;
	}
	if (logical_type == "DOUBLE") {
		return duckdb_api::ValueKind::DOUBLE;
	}
	throw std::logic_error("package product fake received an unsupported output type");
}

class PlanEchoStream final : public duckdb_api::BatchStream {
public:
	explicit PlanEchoStream(duckdb_api::ScanPlan plan_p) : plan(std::move(plan_p)), emitted(false), closed(false) {
	}

	bool Next(duckdb_api::ExecutionControl &control, duckdb_api::TypedBatch &batch) override {
		batch.Clear();
		if (control.IsCancellationRequested()) {
			throw duckdb_api::ExecutionCancelled();
		}
		if (emitted || closed) {
			return false;
		}
		duckdb_api::TypedRow row;
		for (const auto &column : plan.OutputColumns()) {
			const auto kind = KindFor(column.logical_type);
			batch.column_types.push_back(kind);
			switch (kind) {
			case duckdb_api::ValueKind::BIGINT:
				row.values.push_back(duckdb_api::TypedValue::BigInt(11));
				break;
			case duckdb_api::ValueKind::VARCHAR:
				row.values.push_back(duckdb_api::TypedValue::Varchar(plan.RelationName() + ":" + column.name));
				break;
			case duckdb_api::ValueKind::BOOLEAN:
				row.values.push_back(duckdb_api::TypedValue::Boolean(false));
				break;
			case duckdb_api::ValueKind::DOUBLE:
				row.values.push_back(duckdb_api::TypedValue::Double(1.5));
				break;
			}
		}
		batch.rows.push_back(std::move(row));
		emitted = true;
		return true;
	}

	void Cancel() noexcept override {
		closed = true;
	}

	void Close() noexcept override {
		closed = true;
	}

private:
	const duckdb_api::ScanPlan plan;
	bool emitted;
	bool closed;
};

// Query's catalog-composition oracle intentionally does not simulate HTTP.
// It validates the real Semantics plan and closed authorization alternative,
// then returns one schema-aligned row through Runtime's public pull contract.
class PlanEchoExecutor final : public duckdb_api::ScanExecutor {
public:
	PlanEchoExecutor() : anonymous_opens(0), authenticated_opens(0) {
	}

	std::unique_ptr<duckdb_api::BatchStream> Open(const duckdb_api::ScanPlan &,
	                                              duckdb_api::ExecutionControl &) const override {
		throw std::logic_error("package product fake received the legacy execution path");
	}

	mutable std::atomic<std::uint64_t> anonymous_opens;
	mutable std::atomic<std::uint64_t> authenticated_opens;

protected:
	std::unique_ptr<duckdb_api::BatchStream>
	OpenAuthorizationEnvelope(const duckdb_api::ScanPlan &plan, duckdb_api::ScanAuthorization authorization,
	                          duckdb_api::ExecutionControl &control) const override {
		if (control.IsCancellationRequested()) {
			throw duckdb_api::ExecutionCancelled();
		}
		const auto alternative = AlternativeOf(authorization);
		// Query's ResolveDuckdbApiSecret supplies the kind-neutral CREDENTIAL
		// alternative for every authenticated relation (it cannot know the
		// target relation's bearer-vs-api_key credential kind at resolution
		// time), so either non-anonymous alternative is a valid authenticated
		// open here, not BEARER specifically.
		if (plan.Authentication() == duckdb_api::FeatureState::DISABLED &&
		    alternative == AuthorizationAlternative::ANONYMOUS) {
			anonymous_opens.fetch_add(1, std::memory_order_relaxed);
		} else if (plan.Authentication() == duckdb_api::FeatureState::ENABLED &&
		           alternative != AuthorizationAlternative::ANONYMOUS) {
			authenticated_opens.fetch_add(1, std::memory_order_relaxed);
		} else {
			throw std::logic_error("package product fake received a mismatched authorization alternative");
		}
		return std::unique_ptr<duckdb_api::BatchStream>(new PlanEchoStream(plan));
	}
};

void TestRealCatalogCompositionQueriesAnonymousAndAuthenticated(const std::string &repository_root) {
	auto executor = std::shared_ptr<PlanEchoExecutor>(new PlanEchoExecutor());
	duckdb::DuckDB database(nullptr);
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_package_catalog_composition_test");
	duckdb::RegisterDuckdbApiSecrets(loader);
	duckdb::RegisterDuckdbApiPackageSurface(loader, duckdb_api::BuildPackageGenerationComposition(executor));
	duckdb::Connection connection(database);
	const auto package_root = repository_root + "/connectors/github";
	auto load = connection.Query("CALL system.main.duckdb_api_load_connector(package_root := '" + package_root + "')");
	Require(!load->HasError() && load->RowCount() == 1 && load->GetValue(4, 0).GetValue<std::uint64_t>() == 4 &&
	            load->GetValue(5, 0).GetValue<bool>(),
	        "actual DuckDB did not publish the real compiler generation through product composition");
	auto duplicate =
	    connection.Query("CALL system.main.duckdb_api_load_connector(package_root := '" + package_root + "')");
	const auto duplicate_error = duplicate->GetError();
	Require(duplicate->HasError() &&
	            duplicate_error.find("[duckdb_api][publication] code=DUCKDB_API_PUBLICATION_CONFLICT:") !=
	                std::string::npos &&
	            duplicate_error.find("DUCKDB_API_CONNECTOR_ALREADY_ACTIVE") == std::string::npos &&
	            duplicate_error.find("[duckdb_api][runtime]") == std::string::npos &&
	            duplicate_error.find(package_root) == std::string::npos,
	        "duplicate actual-DuckDB load did not expose only the public publication conflict");
	for (const auto &name : {"github_authenticated_repositories", "github_authenticated_user",
	                         "github_duckdb_login_search_page", "github_viewer_repository_metrics"}) {
		auto found = connection.Query("SELECT count(*) FROM duckdb_functions() WHERE database_name = 'system' "
		                              "AND schema_name = 'main' AND function_name = '" +
		                              std::string(name) + "'");
		Require(!found->HasError() && found->GetValue(0, 0).GetValue<int64_t>() == 1,
		        "actual DuckDB is missing compiler-generated function " + std::string(name));
	}
	auto inventory =
	    connection.Query("SELECT count(*), count(DISTINCT sql_name) FROM system.main.duckdb_api_loaded_relations()");
	Require(!inventory->HasError() && inventory->GetValue(0, 0).GetValue<int64_t>() == 4 &&
	            inventory->GetValue(1, 0).GetValue<int64_t>() == 4,
	        "actual DuckDB package inventory did not contain four unique generated relations");
	for (const auto &relation :
	     {"github_authenticated_repositories(secret := 'package_product')",
	      "github_authenticated_user(secret := 'package_product')", "github_duckdb_login_search_page()",
	      "github_viewer_repository_metrics(secret := 'package_product')"}) {
		auto described = connection.Query("DESCRIBE SELECT * FROM system.main." + std::string(relation));
		Require(!described->HasError(),
		        "compiler-generated relation failed offline DESCRIBE: " + std::string(relation));
		auto explained = connection.Query("EXPLAIN SELECT * FROM system.main." + std::string(relation));
		Require(!explained->HasError(), "compiler-generated relation failed offline EXPLAIN: " + std::string(relation));
	}
	Require(executor->anonymous_opens.load(std::memory_order_relaxed) == 0 &&
	            executor->authenticated_opens.load(std::memory_order_relaxed) == 0,
	        "compiler-generated DESCRIBE or EXPLAIN entered Runtime execution");
	auto anonymous = connection.Query("SELECT id, login FROM system.main.github_duckdb_login_search_page()");
	Require(!anonymous->HasError() && anonymous->RowCount() == 1 &&
	            anonymous->GetValue(0, 0).GetValue<int64_t>() == 11 &&
	            anonymous->GetValue(1, 0).ToString() == "duckdb_login_search_page:login",
	        "anonymous compiler-generated relation did not execute its real Semantics plan");
	auto secret = connection.Query("CREATE TEMPORARY SECRET package_product "
	                               "(TYPE duckdb_api, PROVIDER config, TOKEN 'package-product-token')");
	Require(!secret->HasError(), "actual-DuckDB package product could not create its logical secret");
	const auto malformed = duckdb_api_test::BuildRepositoryMalformedYamlPackageFixture(repository_root);
	auto invalid =
	    connection.Query("CALL system.main.duckdb_api_load_connector(package_root := '" + malformed.Root() + "')");
	const auto invalid_error = invalid->GetError();
	Require(invalid->HasError() &&
	            invalid_error.find("[duckdb_api][syntax] code=DUCKDB_API_MALFORMED_YAML "
	                               "file=relations/viewer_repository_metrics.yaml line=1 column=15 yaml_path=$:") !=
	                std::string::npos &&
	            invalid_error.find(malformed.Root()) == std::string::npos &&
	            invalid_error.find(package_root) == std::string::npos &&
	            invalid_error.find("package-product-token") == std::string::npos &&
	            invalid_error.find("api_version") == std::string::npos,
	        "invalid actual-DuckDB package did not preserve its safe compiler source coordinate");
	auto authenticated = connection.Query(
	    "SELECT id, login, site_admin FROM system.main.github_authenticated_user(secret := 'package_product')");
	Require(!authenticated->HasError() && authenticated->RowCount() == 1 &&
	            authenticated->GetValue(0, 0).GetValue<int64_t>() == 11 &&
	            authenticated->GetValue(1, 0).ToString() == "authenticated_user:login" &&
	            !authenticated->GetValue(2, 0).GetValue<bool>() &&
	            executor->anonymous_opens.load(std::memory_order_relaxed) == 1 &&
	            executor->authenticated_opens.load(std::memory_order_relaxed) == 1,
	        "authenticated compiler-generated relation did not execute its real Semantics plan");
	auto reload = connection.Query("CALL system.main.duckdb_api_reload_connector(connector := 'github')");
	Require(!reload->HasError() && reload->RowCount() == 1 && !reload->GetValue(5, 0).GetValue<bool>(),
	        "byte-identical actual-DuckDB package reload was not changed=false");
	auto final_inventory = connection.Query("SELECT count(*) FROM system.main.duckdb_api_loaded_relations()");
	Require(!final_inventory->HasError() && final_inventory->GetValue(0, 0).GetValue<int64_t>() == 4,
	        "rejected duplicate or invalid package changed the active DuckDB catalog generation");
}

// RFC 0019, Query Experience review: RFC 0016 promised a real-EXPLAIN test for
// response_next and never delivered one (only "graphql_cursor" was ever
// asserted against actual EXPLAIN output anywhere in the repository). This
// test closes that gap for short_page: it loads a byte-identical copy of
// connectors/github with authenticated_repositories.yaml declaring
// `strategy: short_page`, then asserts the literal string appears in real
// DuckDB EXPLAIN output, not merely in an internal function call.
void TestShortPageReachesRealExplainOutput(const std::string &repository_root) {
	auto executor = std::shared_ptr<PlanEchoExecutor>(new PlanEchoExecutor());
	duckdb::DuckDB database(nullptr);
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_short_page_explain_test");
	duckdb::RegisterDuckdbApiSecrets(loader);
	duckdb::RegisterDuckdbApiPackageSurface(loader, duckdb_api::BuildPackageGenerationComposition(executor));
	duckdb::Connection connection(database);
	const auto package = duckdb_api_test::BuildRepositoryShortPagePackageFixture(repository_root);
	auto load =
	    connection.Query("CALL system.main.duckdb_api_load_connector(package_root := '" + package.Root() + "')");
	Require(!load->HasError() && load->RowCount() == 1, "short_page package fixture failed to load");
	auto explained = connection.Query(
	    "EXPLAIN SELECT * FROM system.main.github_authenticated_repositories(secret := 'short_page_explain')");
	if (explained->HasError()) {
		throw std::runtime_error("short_page relation failed offline EXPLAIN: " + explained->GetError());
	}
	std::string explanation;
	for (duckdb::idx_t row = 0; row < explained->RowCount(); row++) {
		for (duckdb::idx_t column = 0; column < explained->ColumnCount(); column++) {
			explanation += explained->GetValue(column, row).ToString();
			explanation.push_back('\n');
		}
	}
	Require(explanation.find("short_page") != std::string::npos,
	        "real EXPLAIN output for a short_page relation omitted the literal pagination strategy");
	Require(executor->anonymous_opens.load(std::memory_order_relaxed) == 0 &&
	            executor->authenticated_opens.load(std::memory_order_relaxed) == 0,
	        "offline EXPLAIN of a short_page relation entered Runtime");
}

// RFC 0020, Query Experience review requirement: prove DOUBLE reaches real
// DuckDB output end to end, not merely an internal LogicalTypeForKind-style
// unit call. DESCRIBE is the oracle this repository already uses to assert a
// literal SQL type string against a real bind (see
// TestOfflineBindPrepareAndSafeExplanation's own DESCRIBE assertions in
// graphql_adapter_contract_tests.cpp) — EXPLAIN's own safe explanation map
// carries no per-column type fact, so DESCRIBE is the correct, not merely
// convenient, real-DuckDB oracle for the type. A real SELECT through the same
// PlanEchoExecutor fake additionally proves the decoded DOUBLE value itself
// round-trips through WriteTypedBatch into a real DuckDB result vector.
void TestDoubleColumnReachesRealDescribeAndSelectOutput(const std::string &repository_root) {
	auto executor = std::shared_ptr<PlanEchoExecutor>(new PlanEchoExecutor());
	duckdb::DuckDB database(nullptr);
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_double_column_describe_test");
	duckdb::RegisterDuckdbApiSecrets(loader);
	duckdb::RegisterDuckdbApiPackageSurface(loader, duckdb_api::BuildPackageGenerationComposition(executor));
	duckdb::Connection connection(database);
	const auto package = duckdb_api_test::BuildRepositoryDoubleColumnPackageFixture(repository_root);
	auto load =
	    connection.Query("CALL system.main.duckdb_api_load_connector(package_root := '" + package.Root() + "')");
	Require(!load->HasError() && load->RowCount() == 1, "DOUBLE-column package fixture failed to load");
	auto described = connection.Query(
	    "DESCRIBE SELECT * FROM system.main.github_authenticated_repositories(secret := 'double_column_describe')");
	if (described->HasError()) {
		throw std::runtime_error("DOUBLE-column relation failed offline DESCRIBE: " + described->GetError());
	}
	bool found_double_archived = false;
	for (duckdb::idx_t row = 0; row < described->RowCount(); row++) {
		if (described->GetValue(0, row).ToString() == "archived" &&
		    described->GetValue(1, row).ToString() == "DOUBLE") {
			found_double_archived = true;
		}
	}
	Require(found_double_archived,
	        "real DESCRIBE output for a DOUBLE-column relation omitted the literal DOUBLE scalar type");

	auto secret = connection.Query("CREATE TEMPORARY SECRET double_column_describe "
	                               "(TYPE duckdb_api, PROVIDER config, TOKEN 'double-column-token')");
	Require(!secret->HasError(), "actual-DuckDB DOUBLE-column test could not create its logical secret");
	auto result = connection.Query(
	    "SELECT archived FROM system.main.github_authenticated_repositories(secret := 'double_column_describe')");
	Require(!result->HasError() && result->RowCount() == 1 && result->GetValue(0, 0).GetValue<double>() == 1.5,
	        "a real DuckDB SELECT did not round-trip a decoded DOUBLE value through Query's vector conversion");
}

// This is the whole-graph product oracle. Runtime owns the response programs;
// Query sees only a ScanExecutor and safe counters while the permanent package
// supplies the declarations consumed by Connector and Semantics.
void TestGeneratedRelationsExecuteThroughRuntime(const std::string &repository_root) {
	{
		auto scenario = duckdb_api_test::BuildControlledRuntimeScenario(
		    duckdb_api_test::ControlledRuntimeScenarioId::RICKANDMORTY_CHARACTER_EPISODES);
		duckdb::DuckDB database(nullptr);
		duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_rickandmorty_array_product_test");
		duckdb::RegisterDuckdbApiSecrets(loader);
		duckdb::RegisterDuckdbApiPackageSurface(loader,
		                                        duckdb_api::BuildPackageGenerationComposition(scenario->Executor()));
		duckdb::Connection connection(database);
		LoadRickAndMortyPackage(connection, repository_root);
		auto described =
		    connection.Query("DESCRIBE SELECT * FROM system.main.rickandmorty_character_search(status := 'Alive')");
		bool found_episode_list = false;
		for (duckdb::idx_t row = 0; !described->HasError() && row < described->RowCount(); row++) {
			found_episode_list = found_episode_list || (described->GetValue(0, row).ToString() == "episode" &&
			                                            described->GetValue(1, row).ToString() == "VARCHAR[]");
		}
		Require(!described->HasError() && found_episode_list,
		        "real DuckDB DESCRIBE did not expose Rick and Morty's episode VARCHAR[] column");
		auto all_rows = connection.Query("SELECT id, episode "
		                                 "FROM system.main.rickandmorty_character_search(status := 'Alive') "
		                                 "ORDER BY id");
		Require(!all_rows->HasError() && all_rows->RowCount() == 4,
		        "Rick and Morty's ARRAY scan changed base-row cardinality");
		const auto id_one_value = all_rows->GetValue(1, 0);
		const auto id_two_value = all_rows->GetValue(1, 1);
		const auto id_three_value = all_rows->GetValue(1, 2);
		const auto id_four_value = all_rows->GetValue(1, 3);
		const auto &id_one_episodes = duckdb::ListValue::GetChildren(id_one_value);
		const auto &id_two_episodes = duckdb::ListValue::GetChildren(id_two_value);
		const auto &id_three_episodes = duckdb::ListValue::GetChildren(id_three_value);
		const auto &id_four_episodes = duckdb::ListValue::GetChildren(id_four_value);
		Require(all_rows->GetValue(0, 0).GetValue<int64_t>() == 1 && id_one_episodes.size() == 2 &&
		            id_one_episodes[0].ToString() == "https://rickandmortyapi.com/api/episode/1" &&
		            id_one_episodes[1].ToString() == "https://rickandmortyapi.com/api/episode/2" &&
		            all_rows->GetValue(0, 1).GetValue<int64_t>() == 2 && id_two_episodes.size() == 1 &&
		            id_two_episodes[0].ToString() == "https://rickandmortyapi.com/api/episode/2" &&
		            all_rows->GetValue(0, 2).GetValue<int64_t>() == 3 && id_three_episodes.empty() &&
		            all_rows->GetValue(0, 3).GetValue<int64_t>() == 4 && id_four_episodes.size() == 3 &&
		            id_four_episodes[0].ToString() == "https://rickandmortyapi.com/api/episode/4" &&
		            id_four_episodes[1].ToString() == "https://rickandmortyapi.com/api/episode/1" &&
		            id_four_episodes[2].ToString() == "https://rickandmortyapi.com/api/episode/4",
		        "Rick and Morty's complete ARRAY scan changed an id, list value, order, duplicate, or empty list");
		auto result = connection.Query("SELECT id, episode, episode[1] "
		                               "FROM system.main.rickandmorty_character_search(status := 'Alive') "
		                               "WHERE id <> 1 ORDER BY id LIMIT 2 OFFSET 1");
		const bool has_two_rows = !result->HasError() && result->RowCount() == 2;
		const auto first_episodes = has_two_rows ? result->GetValue(1, 0) : duckdb::Value();
		const auto second_episodes = has_two_rows ? result->GetValue(1, 1) : duckdb::Value();
		const auto &second_children = duckdb::ListValue::GetChildren(second_episodes);
		Require(has_two_rows && result->GetValue(0, 0).GetValue<int64_t>() == 3 &&
		            duckdb::ListValue::GetChildren(first_episodes).empty() && result->GetValue(2, 0).IsNull() &&
		            result->GetValue(0, 1).GetValue<int64_t>() == 4 && second_children.size() == 3 &&
		            second_children[0].ToString() == "https://rickandmortyapi.com/api/episode/4" &&
		            second_children[1].ToString() == "https://rickandmortyapi.com/api/episode/1" &&
		            second_children[2].ToString() == "https://rickandmortyapi.com/api/episode/4" &&
		            result->GetValue(2, 1).ToString() == "https://rickandmortyapi.com/api/episode/4",
		        "Rick and Morty's episode arrays did not survive package compilation, Runtime decoding, and SQL");
		const auto observation = scenario->Observation();
		Require(observation.request_count == observation.expected_request_count,
		        "Rick and Morty ARRAY query did not consume its exact Runtime scenario");
	}
	{
		auto scenario = duckdb_api_test::BuildControlledRuntimeScenario(
		    duckdb_api_test::ControlledRuntimeScenarioId::RETAINED_REST_USER);
		duckdb::DuckDB database(nullptr);
		duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_package_rest_product_test");
		duckdb::RegisterDuckdbApiSecrets(loader);
		duckdb::RegisterDuckdbApiPackageSurface(loader,
		                                        duckdb_api::BuildPackageGenerationComposition(scenario->Executor()));
		duckdb::Connection connection(database);
		LoadRepositoryPackage(connection, repository_root);
		CreatePackageRuntimeSecret(connection);
		auto result = connection.Query(
		    "SELECT id, login, site_admin FROM system.main.github_authenticated_user(secret := 'package_runtime')");
		Require(!result->HasError() && result->RowCount() == 1 && result->GetValue(0, 0).GetValue<int64_t>() == 11 &&
		            result->GetValue(1, 0).ToString() == "duckdb" && !result->GetValue(2, 0).GetValue<bool>(),
		        "compiler-generated REST relation did not execute through Runtime");
		const auto observation = scenario->Observation();
		Require(observation.request_count == observation.expected_request_count,
		        "compiler-generated REST relation did not consume the Runtime scenario");
	}
	{
		auto scenario = duckdb_api_test::BuildControlledRuntimeScenario(
		    duckdb_api_test::ControlledRuntimeScenarioId::GRAPHQL_MULTI_PAGE_NULL_DUPLICATE);
		duckdb::DuckDB database(nullptr);
		duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_package_graphql_product_test");
		duckdb::RegisterDuckdbApiSecrets(loader);
		duckdb::RegisterDuckdbApiPackageSurface(loader,
		                                        duckdb_api::BuildPackageGenerationComposition(scenario->Executor()));
		duckdb::Connection connection(database);
		LoadRepositoryPackage(connection, repository_root);
		CreatePackageRuntimeSecret(connection);
		for (std::uint64_t execution = 0; execution < 2; execution++) {
			auto result =
			    connection.Query("SELECT id, primary_language FROM system.main.github_viewer_repository_metrics("
			                     "secret := 'package_runtime') ORDER BY primary_language NULLS FIRST");
			Require(!result->HasError() && result->RowCount() == 2 &&
			            result->GetValue(0, 0).ToString() == "R-duplicate" &&
			            result->GetValue(0, 1).ToString() == "R-duplicate" && result->GetValue(1, 0).IsNull() &&
			            result->GetValue(1, 1).ToString() == "C++",
			        "compiler-generated GraphQL relation changed Runtime's nullable duplicate bag");
		}
		const auto observation = scenario->Observation();
		Require(observation.request_count == observation.expected_request_count,
		        "compiler-generated GraphQL relation did not consume both cursor-page scenarios");
	}
}

} // namespace

int main(int argc, char **argv) {
	try {
		if (argc != 2 || argv[1][0] != '/') {
			throw std::invalid_argument("usage: package_product_contract_tests ABSOLUTE_REPOSITORY_ROOT");
		}
		TestRealCatalogCompositionQueriesAnonymousAndAuthenticated(argv[1]);
		TestShortPageReachesRealExplainOutput(argv[1]);
		TestDoubleColumnReachesRealDescribeAndSelectOutput(argv[1]);
		TestGeneratedRelationsExecuteThroughRuntime(argv[1]);
		std::cout << "actual-DuckDB package product contract tests passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "actual-DuckDB package product contract tests failed: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
