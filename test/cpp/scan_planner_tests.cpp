#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_plan.hpp"
#include "duckdb_api/scan_request.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <locale>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using duckdb_api_test::Require;

template <typename T>
class HasBaseUrlMember {
	template <typename U>
	static char Test(decltype(&U::base_url));
	template <typename U>
	static long Test(...);

public:
	static const bool VALUE = sizeof(Test<T>(0)) == sizeof(char);
};

template <typename T>
class HasPathMember {
	template <typename U>
	static char Test(decltype(&U::path));
	template <typename U>
	static long Test(...);

public:
	static const bool VALUE = sizeof(Test<T>(0)) == sizeof(char);
};

template <typename T>
class HasQueryMember {
	template <typename U>
	static char Test(decltype(&U::query));
	template <typename U>
	static long Test(...);

public:
	static const bool VALUE = sizeof(Test<T>(0)) == sizeof(char);
};

template <typename T>
class HasFragmentMember {
	template <typename U>
	static char Test(decltype(&U::fragment));
	template <typename U>
	static long Test(...);

public:
	static const bool VALUE = sizeof(Test<T>(0)) == sizeof(char);
};

static_assert(!HasBaseUrlMember<duckdb_api::PlannedRestOperation>::VALUE,
              "PlannedRestOperation must not restore a parseable base URL");
static_assert(!HasPathMember<duckdb_api::PlannedRestOrigin>::VALUE,
              "PlannedRestOrigin must not carry a path component");
static_assert(!HasQueryMember<duckdb_api::PlannedRestOrigin>::VALUE,
              "PlannedRestOrigin must not carry a query component");
static_assert(!HasFragmentMember<duckdb_api::PlannedRestOrigin>::VALUE,
              "PlannedRestOrigin must not carry a fragment component");
static_assert(std::is_same<decltype(duckdb_api::PlannedRestOrigin::scheme), duckdb_api::PlannedUrlScheme>::value,
              "PlannedRestOrigin scheme must remain typed");
static_assert(std::is_same<decltype(duckdb_api::PlannedRestOrigin::port), std::uint16_t>::value,
              "PlannedRestOrigin port must remain an explicit uint16_t");

class GroupedDigits final : public std::numpunct<char> {
protected:
	char do_thousands_sep() const override {
		return '_';
	}

	std::string do_grouping() const override {
		return "\3";
	}
};

duckdb_api::ScanRequest BuildLiveRequest(const duckdb_api::CompiledConnector &connector) {
	duckdb_api::ScanRequest result;
	result.connector_name = connector.connector_name;
	result.relation_name = connector.relation_name;
	for (const auto &column : connector.columns) {
		result.projected_columns.push_back(column.name);
	}
	result.predicate = "TRUE";
	result.has_limit = false;
	result.has_offset = false;
	result.capabilities = {false, false, false, false, false, false, true, false};
	return result;
}

void RequireColumn(const duckdb_api::PlannedColumn &column, const std::string &name, const std::string &logical_type,
                   const std::string &extractor) {
	Require(column.name == name, "planned column name drifted for " + name);
	Require(column.logical_type == logical_type, "planned column type drifted for " + name);
	Require(!column.nullable, "planned column became nullable for " + name);
	Require(column.extractor == extractor, "planned column extractor drifted for " + name);
}

void RequireConnectorRejected(duckdb_api::CompiledConnector connector, const std::string &field) {
	bool rejected = false;
	try {
		duckdb_api::BuildConservativeScanPlan(connector, BuildLiveRequest(connector));
	} catch (const std::logic_error &) {
		rejected = true;
	}
	Require(rejected, "planner accepted invalid connector metadata " + field);
}

void RequireRequestRejected(duckdb_api::ScanRequest request, const std::string &field) {
	bool rejected = false;
	try {
		const auto connector = duckdb_api::BuildNativeGithubConnector();
		duckdb_api::BuildConservativeScanPlan(connector, request);
	} catch (const std::logic_error &) {
		rejected = true;
	}
	Require(rejected, "planner accepted non-conservative request " + field);
}

void RequireBudgetFieldBounded(const duckdb_api::ResourceBudgets &baseline,
                               std::uint64_t duckdb_api::ResourceBudgets::*field, std::uint64_t host_cap,
                               const std::string &name) {
	auto invalid = baseline;
	invalid.*field = 0;
	Require(!invalid.IsWithinLiveRestBounds(), "zero " + name + " budget was accepted");
	invalid = baseline;
	invalid.*field = host_cap + 1;
	Require(!invalid.IsWithinLiveRestBounds(), name + " budget widened its host cap");
}

void TestTypedImmutableLivePlan() {
	Require(std::is_copy_constructible<duckdb_api::ScanPlan>::value,
	        "ScanPlan must be copy-constructible for bind state");
	Require(std::is_move_constructible<duckdb_api::ScanPlan>::value,
	        "ScanPlan must be move-constructible for bind state");
	Require(!std::is_copy_assignable<duckdb_api::ScanPlan>::value,
	        "ScanPlan must not allow post-construction assignment");
	Require(!std::is_move_assignable<duckdb_api::ScanPlan>::value,
	        "ScanPlan must not allow post-construction move assignment");

	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto plan = duckdb_api::BuildConservativeScanPlan(connector, BuildLiveRequest(connector));
	Require(plan.ConnectorName() == "github" && plan.ConnectorVersion() == "0.3.0" &&
	            plan.RelationName() == "duckdb_login_search_page",
	        "ScanPlan identity drifted");
	Require(plan.SourceSnapshot() == connector.Snapshot(), "ScanPlan is not bound to its connector snapshot");
	Require(plan.Domain() == duckdb_api::BaseDomain::SINGLE_RESPONSE_PAGE, "ScanPlan widened the bounded base domain");

	const auto &operation = plan.Operation();
	Require(operation.operation_name == "github_search_duckdb_login_page" &&
	            operation.protocol == duckdb_api::PlannedProtocol::REST &&
	            operation.method == duckdb_api::PlannedHttpMethod::GET &&
	            operation.cardinality == duckdb_api::PlannedCardinality::ZERO_TO_MANY &&
	            operation.replay_safety == duckdb_api::PlannedReplaySafety::SAFE,
	        "ScanPlan operation classification drifted");
	Require(operation.origin.scheme == duckdb_api::PlannedUrlScheme::HTTPS &&
	            operation.origin.host == "api.github.com" && operation.origin.port == 443 &&
	            operation.path == "/search/users" && operation.records_extractor == "$.items[*]",
	        "ScanPlan REST structure drifted");
	Require(operation.query_parameters.size() == 2 && operation.query_parameters[0].name == "q" &&
	            operation.query_parameters[0].encoded_value == "duckdb+in%3Alogin" &&
	            operation.query_parameters[1].name == "per_page" && operation.query_parameters[1].encoded_value == "3",
	        "ScanPlan source-definition query drifted");
	Require(operation.headers.size() == 3 && operation.headers[0].name == "Accept" &&
	            operation.headers[0].value == "application/vnd.github+json" &&
	            operation.headers[1].name == "User-Agent" && operation.headers[1].value == "duckdb-api/0.3.0" &&
	            operation.headers[2].name == "X-GitHub-Api-Version" && operation.headers[2].value == "2022-11-28",
	        "ScanPlan fixed headers drifted");

	Require(plan.OutputColumns().size() == 3, "ScanPlan projection closure width drifted");
	RequireColumn(plan.OutputColumns()[0], "id", "BIGINT", "$.id");
	RequireColumn(plan.OutputColumns()[1], "login", "VARCHAR", "$.login");
	RequireColumn(plan.OutputColumns()[2], "site_admin", "BOOLEAN", "$.site_admin");

	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB,
	        "ScanPlan changed the bounded-domain predicate classification");
	Require(plan.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().ordering == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().offset == duckdb_api::RelationalOwner::DUCKDB,
	        "ScanPlan transferred a relational operator away from DuckDB");
	Require(plan.RemoteOrdering() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeOrdering() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RemoteLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RemoteOffset() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeOffset() == duckdb_api::RelationalDelegation::NONE,
	        "ScanPlan claimed hidden ordering or bounds");
	Require(plan.Pagination() == duckdb_api::FeatureState::DISABLED &&
	            plan.Providers() == duckdb_api::FeatureState::DISABLED &&
	            plan.Retry() == duckdb_api::FeatureState::DISABLED &&
	            plan.Cache() == duckdb_api::FeatureState::DISABLED &&
	            plan.Authentication() == duckdb_api::FeatureState::DISABLED,
	        "ScanPlan enabled an excluded feature");
	Require(plan.Network().allowed_schemes == std::vector<std::string>({"https"}) &&
	            plan.Network().allowed_hosts == std::vector<std::string>({"api.github.com"}) &&
	            !plan.Network().redirects_enabled && !plan.Network().private_addresses_enabled &&
	            !plan.Network().link_local_addresses_enabled && !plan.Network().loopback_addresses_enabled,
	        "ScanPlan network capability drifted");
	Require(plan.Budgets().IsWithinLiveRestBounds(), "ScanPlan effective resource budget escaped host bounds");
	Require(
	    plan.ClassificationReason() ==
	        "fixed request defines the complete single-response base domain; DuckDB retains all relational operators",
	    "ScanPlan classification reason drifted");
}

void TestGoldenSnapshotAndDeterminism() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto request = BuildLiveRequest(connector);
	const auto first = duckdb_api::BuildConservativeScanPlan(connector, request);
	const auto second = duckdb_api::BuildConservativeScanPlan(connector, request);
	const std::string expected =
	    "connector=github;version=0.3.0;relation=duckdb_login_search_page;source_snapshot=[" + connector.Snapshot() +
	    "];domain=single_response_page;operation=github_search_duckdb_login_page:zero_to_many:REST:GET:safe;"
	    "request=origin:[scheme:https,host:api.github.com,port:443],path:/search/users,"
	    "query:[q=duckdb+in%3Alogin,per_page=3],"
	    "headers:[Accept=application/vnd.github+json,User-Agent=duckdb-api/0.3.0,"
	    "X-GitHub-Api-Version=2022-11-28];response_records=$.items[*];"
	    "projection=id:BIGINT!:$.id,login:VARCHAR!:$.login,site_admin:BOOLEAN!:$.site_admin;"
	    "remote_predicate=TRUE@single_response_page;residual_predicate=TRUE@single_response_page;"
	    "residual_owner=duckdb;owners=filter:duckdb,ordering:duckdb,limit:duckdb,offset:duckdb;"
	    "delegation=remote_ordering:none,runtime_ordering:none,remote_limit:none,remote_offset:none,"
	    "runtime_limit:none,runtime_offset:none;features=pagination:disabled,providers:disabled,retry:disabled,"
	    "cache:disabled,authentication:disabled;network=schemes:[https],hosts:[api.github.com],redirects:denied,"
	    "private:denied,link_local:denied,loopback:denied;budgets=request_attempts:1,response_bytes:65536,"
	    "header_bytes:16384,decompressed_bytes:65536,records:3,string_bytes:256,json_nesting:16,"
	    "decoded_memory_bytes:131072,batch_rows:2,wall_ms:5000,concurrency:1;reason=fixed request defines the "
	    "complete single-response base domain; DuckDB retains all relational operators";
	Require(first.Snapshot() == expected, "ScanPlan golden snapshot drifted");
	Require(second.Snapshot() == first.Snapshot(), "offline planning is not deterministic");
	const std::locale original_locale;
	std::string localized_snapshot;
	try {
		std::locale::global(std::locale(std::locale::classic(), new GroupedDigits()));
		localized_snapshot = first.Snapshot();
		std::locale::global(original_locale);
	} catch (...) {
		std::locale::global(original_locale);
		throw;
	}
	Require(localized_snapshot == expected, "ScanPlan snapshot depends on the process-global locale");
}

void TestSourceConstantsAreNotPushdown() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto plan = duckdb_api::BuildConservativeScanPlan(connector, BuildLiveRequest(connector));
	Require(plan.Operation().query_parameters[0].name == "q" && plan.Operation().query_parameters[1].name == "per_page",
	        "fixed source constants disappeared from the executable operation");
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.RemoteLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB,
	        "fixed q or per_page was misclassified as relational pushdown");
}

void TestConnectorCeilingsNarrowHostBudgets() {
	auto connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.max_response_bytes = 2048;
	connector.resource_ceilings.max_records = 2;
	connector.resource_ceilings.max_extracted_string_bytes = 64;
	const auto narrowed = duckdb_api::BuildConservativeScanPlan(connector, BuildLiveRequest(connector));
	Require(narrowed.Budgets().response_bytes == 2048 && narrowed.Budgets().decoded_records == 2 &&
	            narrowed.Budgets().extracted_string_bytes == 64,
	        "connector ceilings did not narrow host budgets");
	Require(narrowed.Budgets().IsWithinLiveRestBounds(),
	        "valid connector-narrowed budgets were rejected as outside host bounds");

	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.max_response_bytes = duckdb_api::HOST_MAX_RESPONSE_BYTES + 1;
	connector.resource_ceilings.max_records = duckdb_api::HOST_MAX_DECODED_RECORDS + 1;
	connector.resource_ceilings.max_extracted_string_bytes = duckdb_api::HOST_MAX_EXTRACTED_STRING_BYTES + 1;
	const auto capped = duckdb_api::BuildConservativeScanPlan(connector, BuildLiveRequest(connector));
	Require(capped.Budgets().response_bytes == duckdb_api::HOST_MAX_RESPONSE_BYTES &&
	            capped.Budgets().decoded_records == duckdb_api::HOST_MAX_DECODED_RECORDS &&
	            capped.Budgets().extracted_string_bytes == duckdb_api::HOST_MAX_EXTRACTED_STRING_BYTES,
	        "connector metadata widened host budgets");
	Require(capped.Budgets().IsWithinLiveRestBounds(), "host-capped budgets were rejected as invalid");

	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::response_bytes,
	                          duckdb_api::HOST_MAX_RESPONSE_BYTES, "response byte");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::header_bytes,
	                          duckdb_api::HOST_MAX_HEADER_BYTES, "header byte");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::decompressed_bytes,
	                          duckdb_api::HOST_MAX_DECOMPRESSED_BYTES, "decompressed byte");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::decoded_records,
	                          duckdb_api::HOST_MAX_DECODED_RECORDS, "decoded record");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::extracted_string_bytes,
	                          duckdb_api::HOST_MAX_EXTRACTED_STRING_BYTES, "extracted string byte");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::json_nesting,
	                          duckdb_api::HOST_MAX_JSON_NESTING, "JSON nesting");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::decoded_memory_bytes,
	                          duckdb_api::HOST_MAX_DECODED_MEMORY_BYTES, "decoded memory byte");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::batch_rows,
	                          duckdb_api::OUTPUT_BATCH_ROWS, "batch row");
	RequireBudgetFieldBounded(narrowed.Budgets(), &duckdb_api::ResourceBudgets::wall_milliseconds,
	                          duckdb_api::MAX_EXECUTION_MILLISECONDS, "wall time");

	auto invalid = narrowed.Budgets();
	invalid.request_attempts = 2;
	Require(!invalid.IsWithinLiveRestBounds(), "effective budget enabled a second request attempt");
	invalid = narrowed.Budgets();
	invalid.request_attempts = 0;
	Require(!invalid.IsWithinLiveRestBounds(), "effective budget disabled the required request attempt");
	invalid = narrowed.Budgets();
	invalid.concurrency = 2;
	Require(!invalid.IsWithinLiveRestBounds(), "effective budget enabled a second concurrent transfer");
	invalid = narrowed.Budgets();
	invalid.concurrency = 0;
	Require(!invalid.IsWithinLiveRestBounds(), "effective budget removed the required concurrency slot");
}

void TestPrivateControlledCapabilityUsesTheSamePlanner() {
	auto connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.origin.scheme = duckdb_api::CompiledUrlScheme::HTTP;
	connector.operation.request.origin.host = duckdb_api::CompiledRestHost("127.0.0.1");
	connector.operation.request.origin.port = 8080;
	connector.network_policy.allowed_schemes = {"http"};
	connector.network_policy.allowed_hosts = {"127.0.0.1"};
	connector.network_policy.loopback_addresses_enabled = true;
	const auto plan = duckdb_api::BuildConservativeScanPlan(connector, BuildLiveRequest(connector));
	Require(plan.Operation().origin.scheme == duckdb_api::PlannedUrlScheme::HTTP &&
	            plan.Operation().origin.host == "127.0.0.1" && plan.Operation().origin.port == 8080 &&
	            plan.Network().loopback_addresses_enabled,
	        "private controlled capability did not traverse the production planner");
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.RemoteLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB,
	        "private controlled capability changed relational meaning");
}

void TestConnectorAndRequestCounterexamples() {
	auto connector = duckdb_api::BuildNativeGithubConnector();
	connector.columns.clear();
	RequireConnectorRejected(connector, "with no schema");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.columns[0].nullable = true;
	RequireConnectorRejected(connector, "with nullable required output");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.columns[0].logical_type = "DOUBLE";
	RequireConnectorRejected(connector, "with unsupported output type");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.columns[0].extractor.clear();
	RequireConnectorRejected(connector, "with missing extractor");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.columns[1].name = connector.columns[0].name;
	RequireConnectorRejected(connector, "with duplicate column");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.fallback = false;
	RequireConnectorRejected(connector, "without fallback operation");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.retry_enabled = true;
	RequireConnectorRejected(connector, "with retry enabled");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.authentication_enabled = true;
	RequireConnectorRejected(connector, "with authentication enabled");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.pagination_enabled = true;
	RequireConnectorRejected(connector, "with pagination enabled");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.path = "search/users";
	RequireConnectorRejected(connector, "with invalid path");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.path = "/search/users?per_page=3";
	RequireConnectorRejected(connector, "with query structure in path");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.path = "/search/users#page";
	RequireConnectorRejected(connector, "with fragment structure in path");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.query_parameters[0].name = "q?hidden";
	RequireConnectorRejected(connector, "with URL structure in query name");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.query_parameters[0].encoded_value = "duckdb#fragment";
	RequireConnectorRejected(connector, "with URL structure in encoded query value");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.query_parameters[1].name = connector.operation.request.query_parameters[0].name;
	RequireConnectorRejected(connector, "with duplicate query name");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.headers[0].value.clear();
	RequireConnectorRejected(connector, "with empty header value");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.origin.scheme = duckdb_api::CompiledUrlScheme::HTTP;
	RequireConnectorRejected(connector, "with origin outside declared scheme");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.origin.host = duckdb_api::CompiledRestHost("example.com");
	RequireConnectorRejected(connector, "with origin outside declared host");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.origin.port = 444;
	RequireConnectorRejected(connector, "with non-canonical HTTPS port");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.operation.request.origin.port = 0;
	RequireConnectorRejected(connector, "with zero origin port");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.allowed_schemes.push_back("http");
	RequireConnectorRejected(connector, "with widened scheme capability");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.allowed_hosts.push_back("example.com");
	RequireConnectorRejected(connector, "with widened host capability");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.redirects_enabled = true;
	RequireConnectorRejected(connector, "with redirects enabled");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.private_addresses_enabled = true;
	RequireConnectorRejected(connector, "with private-address authority enabled");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.link_local_addresses_enabled = true;
	RequireConnectorRejected(connector, "with link-local authority enabled");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.loopback_addresses_enabled = true;
	RequireConnectorRejected(connector, "with loopback authority inconsistent with HTTPS origin");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.network_policy.max_response_bytes = 0;
	RequireConnectorRejected(connector, "with zero response budget");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.resource_ceilings.max_records = 0;
	RequireConnectorRejected(connector, "with zero record budget");
	connector = duckdb_api::BuildNativeGithubConnector();
	connector.resource_ceilings.max_extracted_string_bytes = 0;
	RequireConnectorRejected(connector, "with zero extracted-string budget");

	connector = duckdb_api::BuildNativeGithubConnector();
	auto request = BuildLiveRequest(connector);
	request.connector_name = "other";
	RequireRequestRejected(request, "with wrong connector");
	request = BuildLiveRequest(connector);
	request.relation_name = "other";
	RequireRequestRejected(request, "with wrong relation");
	request = BuildLiveRequest(connector);
	request.explicit_inputs.push_back("unexpected");
	RequireRequestRejected(request, "with explicit inputs");
	request = BuildLiveRequest(connector);
	request.projected_columns.pop_back();
	RequireRequestRejected(request, "with incomplete projection closure");
	request = BuildLiveRequest(connector);
	request.predicate = "id > 1";
	RequireRequestRejected(request, "with unavailable predicate");
	request = BuildLiveRequest(connector);
	request.orderings.push_back("id");
	RequireRequestRejected(request, "with unavailable ordering");
	request = BuildLiveRequest(connector);
	request.has_limit = true;
	RequireRequestRejected(request, "with unavailable limit");
	request = BuildLiveRequest(connector);
	request.has_offset = true;
	RequireRequestRejected(request, "with unavailable offset");
	request = BuildLiveRequest(connector);
	request.capabilities.projection = true;
	RequireRequestRejected(request, "with projection capability");
	request = BuildLiveRequest(connector);
	request.capabilities.cancellation = false;
	RequireRequestRejected(request, "without verified cancellation");
}

} // namespace

int main() {
	try {
		TestTypedImmutableLivePlan();
		TestGoldenSnapshotAndDeterminism();
		TestSourceConstantsAreNotPushdown();
		TestConnectorCeilingsNarrowHostBudgets();
		TestPrivateControlledCapabilityUsesTheSamePlanner();
		TestConnectorAndRequestCounterexamples();
		std::cout << "scan planner tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "scan planner tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
