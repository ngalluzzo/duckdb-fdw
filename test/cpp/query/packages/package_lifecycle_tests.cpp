#include "query/packages/support/package_query_test_support.hpp"

#include "catalog_generation_coordinator.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "support/require.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace duckdb_api_test {
namespace {

const char *const FIXTURE_ROOT = "/fixture/package";

void TestCloseRejectsPublicationButAllowsPublishedScans() {
	auto probe = std::shared_ptr<PackageQueryProbe>(new PackageQueryProbe());
	auto staging = BuildCompatibilityPackageQueryStaging(FIXTURE_ROOT, probe);
	duckdb::DuckDB database(nullptr);
	auto coordinator = RegisterPackageQuerySurface(database, staging);
	duckdb::Connection connection(database);
	RequirePackageQuerySuccess(connection, std::string("CALL system.main.duckdb_api_load_connector(package_root := '") +
	                                           FIXTURE_ROOT + "')");

	coordinator->BeginClose();
	Require(coordinator->IsClosing(), "Query catalog coordinator did not enter closing state");
	auto scan = connection.Query("SELECT status FROM system.main.fixture_package_distinct_status()");
	Require(!scan->HasError() && scan->GetValue(0, 0).ToString() == "old:status",
	        "closing rejected an already-published immutable generation");

	staging->SetReloadChanged(true);
	const auto reload_error =
	    PackageQueryError(connection, "CALL system.main.duckdb_api_reload_connector(connector := 'fixture_package')");
	Require(reload_error.find("coordinator is closing") != std::string::npos,
	        "closing reload used the wrong diagnostic: " + reload_error);
	auto inventory = connection.Query("SELECT package_version FROM system.main.duckdb_api_loaded_connectors()");
	Require(!inventory->HasError() && inventory->GetValue(0, 0).ToString() == "1.2.3",
	        "closing publication changed the active generation");
}

void TestFailedPublicationReleasesCandidateOwner() {
	auto probe = std::shared_ptr<PackageQueryProbe>(new PackageQueryProbe());
	auto staging = BuildCompatibilityPackageQueryStaging(FIXTURE_ROOT, probe);
	duckdb::DuckDB database(nullptr);
	(void)RegisterPackageQuerySurface(database, staging);
	duckdb::Connection connection(database);
	RequirePackageQuerySuccess(connection,
	                           "CREATE MACRO fixture_package_distinct_status() AS TABLE SELECT 'unrelated' AS status");
	(void)PackageQueryError(connection, std::string("CALL system.main.duckdb_api_load_connector(package_root := '") +
	                                        FIXTURE_ROOT + "')");
	Require(staging->LastCandidate().expired(), "failed publication retained the staged Query generation");
	Require(probe->generation_owners_destroyed.load(std::memory_order_relaxed) == 1,
	        "failed publication did not release its opaque Runtime owner exactly once");
}

void TestDatabaseLifetimeSentryReleasesCoordinatorAndGeneration() {
	std::weak_ptr<duckdb::duckdb_api_query_internal::CatalogGenerationCoordinator> weak_coordinator;
	std::weak_ptr<const duckdb_api::QueryPublishedGeneration> weak_generation;
	auto probe = std::shared_ptr<PackageQueryProbe>(new PackageQueryProbe());
	{
		auto staging = BuildCompatibilityPackageQueryStaging(FIXTURE_ROOT, probe);
		duckdb::DuckDB database(nullptr);
		auto coordinator = RegisterPackageQuerySurface(database, staging);
		weak_coordinator = coordinator;
		duckdb::Connection connection(database);
		RequirePackageQuerySuccess(connection,
		                           std::string("CALL system.main.duckdb_api_load_connector(package_root := '") +
		                               FIXTURE_ROOT + "')");
		weak_generation = staging->LastCandidate();
		Require(!weak_generation.expired(), "published generation was not retained by DuckDB catalog state");
		coordinator.reset();
		staging.reset();
	}
	Require(weak_coordinator.expired(), "database destruction retained the Query catalog coordinator");
	Require(weak_generation.expired(), "database destruction retained a published Query generation");
	Require(probe->generation_owners_destroyed.load(std::memory_order_relaxed) == 1,
	        "database destruction did not release the opaque Runtime owner exactly once");
}

} // namespace

void RunPackageLifecycleTests() {
	TestCloseRejectsPublicationButAllowsPublishedScans();
	TestFailedPublicationReleasesCandidateOwner();
	TestDatabaseLifetimeSentryReleasesCoordinatorAndGeneration();
}

} // namespace duckdb_api_test
