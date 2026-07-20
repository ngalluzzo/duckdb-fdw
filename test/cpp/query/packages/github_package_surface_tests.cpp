#include "query/packages/support/package_query_test_support.hpp"

#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "support/require.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace duckdb_api_test {
namespace {

struct ExpectedColumn final {
	const char *name;
	const char *type;
};

void RequireSchema(duckdb::Connection &connection, const std::string &function,
                   const std::vector<ExpectedColumn> &expected, const std::string &arguments = std::string()) {
	auto result = connection.Query("DESCRIBE SELECT * FROM system.main." + function + "(" + arguments + ")");
	if (result->HasError()) {
		throw std::runtime_error("generated GitHub relation DESCRIBE failed for " + function + ": " +
		                         result->GetError());
	}
	Require(result->RowCount() == expected.size(), "generated GitHub relation column count drifted for " + function);
	for (duckdb::idx_t row = 0; row < result->RowCount(); row++) {
		Require(result->GetValue(0, row).ToString() == expected[row].name &&
		            result->GetValue(1, row).ToString() == expected[row].type,
		        "generated GitHub relation schema drifted for " + function + "." + expected[row].name);
	}
}

void RequireSingleRow(duckdb::Connection &connection, const std::string &sql, const std::string &label) {
	auto result = connection.Query(sql);
	if (result->HasError()) {
		throw std::runtime_error(label + " failed: " + result->GetError());
	}
	Require(result->RowCount() == 1, label + " did not execute exactly one controlled Runtime row");
}

void TestCompiledGithubPackageIsThePublishedSqlSurface(const std::string &absolute_repository_root) {
	auto probe = std::shared_ptr<PackageQueryProbe>(new PackageQueryProbe());
	auto staging = BuildGithubPackageQueryStaging(absolute_repository_root, probe);
	duckdb::DuckDB database(nullptr);
	(void)RegisterPackageQuerySurface(database, staging);
	duckdb::Connection connection(database);
	const auto package_root = absolute_repository_root + "/docs/rfcs/evidence/0013/github";
	RequirePackageQuerySuccess(connection,
	                           "CALL system.main.duckdb_api_load_connector(package_root := '" + package_root + "')");

	auto connector = connection.Query("SELECT connector, package_version, spec_version, relation_count "
	                                  "FROM system.main.duckdb_api_loaded_connectors()");
	Require(!connector->HasError() && connector->RowCount() == 1 && connector->GetValue(0, 0).ToString() == "github" &&
	            connector->GetValue(1, 0).ToString() == "1.0.0" &&
	            connector->GetValue(2, 0).ToString() == "duckdb_api/v1" &&
	            connector->GetValue(3, 0).GetValue<std::uint64_t>() == 4,
	        "published GitHub connector identity did not come from the compiled repository package");
	Require(probe->publication_commits.load(std::memory_order_relaxed) == 1 &&
	            probe->publication_discards.load(std::memory_order_relaxed) == 0,
	        "compiled GitHub package did not publish its Runtime candidate exactly once");

	auto relations =
	    connection.Query("SELECT relation, sql_name FROM system.main.duckdb_api_loaded_relations() ORDER BY relation");
	Require(!relations->HasError() && relations->RowCount() == 4,
	        "compiled GitHub package did not publish exactly four relation rows");
	const std::vector<std::pair<const char *, const char *>> expected_relations = {
	    {"authenticated_repositories", "github_authenticated_repositories"},
	    {"authenticated_user", "github_authenticated_user"},
	    {"duckdb_login_search_page", "github_duckdb_login_search_page"},
	    {"viewer_repository_metrics", "github_viewer_repository_metrics"},
	};
	for (duckdb::idx_t row = 0; row < relations->RowCount(); row++) {
		Require(relations->GetValue(0, row).ToString() == expected_relations[row].first &&
		            relations->GetValue(1, row).ToString() == expected_relations[row].second,
		        "compiled GitHub relation identity drifted at deterministic inventory row " + std::to_string(row));
	}

	auto functions = connection.Query(
	    "SELECT function_name, database_name, schema_name FROM duckdb_functions() "
	    "WHERE function_name IN ('github_authenticated_repositories', 'github_authenticated_user', "
	    "'github_duckdb_login_search_page', 'github_viewer_repository_metrics') ORDER BY function_name");
	Require(!functions->HasError() && functions->RowCount() == 4,
	        "compiled GitHub package did not install four exact DuckDB functions");
	for (duckdb::idx_t row = 0; row < functions->RowCount(); row++) {
		Require(functions->GetValue(0, row).ToString() == expected_relations[row].second &&
		            functions->GetValue(1, row).ToString() == "system" &&
		            functions->GetValue(2, row).ToString() == "main",
		        "compiled GitHub function was not installed with its exact system.main identity");
	}

	RequireSchema(connection, "github_duckdb_login_search_page",
	              {{"id", "BIGINT"}, {"login", "VARCHAR"}, {"site_admin", "BOOLEAN"}});
	RequireSchema(connection, "github_authenticated_user",
	              {{"id", "BIGINT"}, {"login", "VARCHAR"}, {"site_admin", "BOOLEAN"}},
	              "secret := 'offline-not-resolved'");
	RequireSchema(connection, "github_authenticated_repositories",
	              {{"id", "BIGINT"},
	               {"full_name", "VARCHAR"},
	               {"private", "BOOLEAN"},
	               {"fork", "BOOLEAN"},
	               {"archived", "BOOLEAN"},
	               {"visibility", "VARCHAR"}},
	              "secret := 'offline-not-resolved'");
	RequireSchema(connection, "github_viewer_repository_metrics",
	              {{"id", "VARCHAR"},
	               {"full_name", "VARCHAR"},
	               {"owner_login", "VARCHAR"},
	               {"stars", "BIGINT"},
	               {"primary_language", "VARCHAR"},
	               {"private", "BOOLEAN"},
	               {"archived", "BOOLEAN"},
	               {"updated_at", "VARCHAR"}},
	              "secret := 'offline-not-resolved'");
	Require(probe->streams_opened.load(std::memory_order_relaxed) == 0,
	        "compiled GitHub relation DESCRIBE opened Runtime");

	auto arguments = connection.Query(
	    "SELECT relation, argument, duckdb_type, nullable, has_default, default_value, argument_origin "
	    "FROM system.main.duckdb_api_relation_arguments() ORDER BY relation, argument");
	Require(!arguments->HasError() && arguments->RowCount() == 3,
	        "compiled GitHub package did not expose exactly three authenticated secret arguments");
	const std::vector<std::string> authenticated_relations = {"authenticated_repositories", "authenticated_user",
	                                                          "viewer_repository_metrics"};
	for (duckdb::idx_t row = 0; row < arguments->RowCount(); row++) {
		Require(arguments->GetValue(0, row).ToString() == authenticated_relations[row] &&
		            arguments->GetValue(1, row).ToString() == "secret" &&
		            arguments->GetValue(2, row).ToString() == "VARCHAR" &&
		            !arguments->GetValue(3, row).GetValue<bool>() && !arguments->GetValue(4, row).GetValue<bool>() &&
		            arguments->GetValue(5, row).IsNull() && arguments->GetValue(6, row).ToString() == "query",
		        "compiled GitHub secret argument shape drifted for " + authenticated_relations[row]);
	}

	RequirePackageQuerySuccess(connection, "PREPARE github_authenticated AS SELECT login FROM "
	                                       "system.main.github_authenticated_user(secret := 'github_fixture')");
	Require(probe->streams_opened.load(std::memory_order_relaxed) == 0,
	        "compiled GitHub relation PREPARE opened Runtime");
	const auto missing_secret = PackageQueryError(connection, "SELECT * FROM system.main.github_authenticated_user()");
	Require(missing_secret.find("required named argument secret is missing") != std::string::npos,
	        "authenticated generated relation used the wrong missing-secret diagnostic: " + missing_secret);
	const auto anonymous_secret = PackageQueryError(
	    connection, "SELECT * FROM system.main.github_duckdb_login_search_page(secret := 'not-accepted')");
	Require(anonymous_secret.find("secret") != std::string::npos,
	        "anonymous generated relation did not reject a secret argument");
	Require(probe->streams_opened.load(std::memory_order_relaxed) == 0,
	        "generated GitHub bind failures opened Runtime");

	RequirePackageQuerySuccess(
	    connection, "CREATE TEMPORARY SECRET github_fixture (TYPE duckdb_api, PROVIDER config, TOKEN 'offline-token')");
	RequireSingleRow(connection, "EXECUTE github_authenticated", "prepared authenticated GitHub user relation");
	RequireSingleRow(connection, "SELECT * FROM system.main.github_duckdb_login_search_page()",
	                 "anonymous GitHub relation");
	RequireSingleRow(connection,
	                 "SELECT * FROM system.main.github_authenticated_repositories(secret := 'github_fixture')",
	                 "authenticated GitHub repositories relation");
	RequireSingleRow(connection,
	                 "SELECT * FROM system.main.github_viewer_repository_metrics(secret := 'github_fixture')",
	                 "authenticated GitHub GraphQL relation");
	Require(probe->streams_opened.load(std::memory_order_relaxed) == 4 &&
	            probe->streams_closed.load(std::memory_order_relaxed) == 4,
	        "four generated GitHub executions did not own four bounded Runtime stream lifecycles");
}

} // namespace

void RunGithubPackageSurfaceTests(const std::string &absolute_repository_root) {
	TestCompiledGithubPackageIsThePublishedSqlSurface(absolute_repository_root);
}

} // namespace duckdb_api_test
