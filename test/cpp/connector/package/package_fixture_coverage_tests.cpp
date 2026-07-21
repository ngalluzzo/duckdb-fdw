#include "connector/support/package_compiler_test_fixtures.hpp"

#include "duckdb_api/local_package_compiler.hpp"
#include "duckdb_api/package_fixture_runner.hpp"
#include "support/require.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using duckdb_api_test::Require;

bool Contains(const duckdb_api::connector::PackageFixtureCoverage &coverage, const std::string &key) {
	return std::find(coverage.RequiredKeys().begin(), coverage.RequiredKeys().end(), key) !=
	       coverage.RequiredKeys().end();
}

void TestGithubCoverageMatchesAcceptedMapping(const std::string &repository_root) {
	const auto generation = duckdb_api_test::CompileRepositoryGithubGenerationFixture(repository_root);
	const auto coverage = duckdb_api::connector::DerivePackageFixtureCoverage(generation);
	Require(coverage.RequiredKeys().size() == 258,
	        "repository GitHub package did not derive all 258 accepted coverage keys");
	Require(coverage.OrderedDigest() == "sha256.a4e2cafbe8acea3e02b5739e4751a9b05a7c927164d5ec23ec9e5bd03a734aa8",
	        "repository GitHub coverage ordering drifted from RFC 0013");
	Require(coverage.RequiredKeys().front() ==
	                "operation_duckdb_login_search_page_github_search_duckdb_login_page_success" &&
	            coverage.RequiredKeys().back() == "diagnostic_duckdb_api_publication_conflict",
	        "coverage rule ordering no longer follows the accepted mapping");
	Require(Contains(coverage, "predicate_authenticated_repositories_private_visibility_unavailable_structure_local") &&
	            Contains(coverage, "resource_viewer_repository_metrics_github_viewer_repository_metrics_"
	                               "max_serialized_body_bytes_per_scan_one_over_rejected") &&
	            Contains(coverage, "diagnostic_duckdb_api_fixture_mismatch"),
	        "coverage derivation lost predicate, GraphQL resource, or fixture-diagnostic scope");

	const auto *relation = generation.Connector().FindRelation("authenticated_repositories");
	Require(relation != nullptr && relation->PredicateMappings().size() == 1 &&
	            relation->PredicateMappings()[0].Name() == "private_visibility",
	        "compiled predicate facts lost the author identity required for independent coverage derivation");
}

void TestCoverageIsCompiledFactDriven(const std::string &repository_root) {
	const auto generation = duckdb_api_test::CompileNonGithubGraphqlGenerationFixture(repository_root);
	const auto coverage = duckdb_api::connector::DerivePackageFixtureCoverage(generation);
	Require(Contains(coverage, "selection_regional_events_regional_event_graph_selected") &&
	            Contains(coverage, "selection_regional_events_fallback_events_selected") &&
	            Contains(coverage, "selection_regional_events_highest_rank_tie_rejected") &&
	            Contains(coverage, "predicate_regional_events_active_events_positive") &&
	            Contains(coverage, "graphql_regional_events_regional_event_graph_serialized_body_identity") &&
	            !Contains(coverage, "predicate_authenticated_repositories_private_visibility_positive"),
	        "coverage derivation used repository identity instead of compiled feature facts");
}

void TestFixtureDiagnosticVocabulary() {
	const duckdb_api::connector::PackageDiagnostic diagnostic(
	    duckdb_api::connector::PackageDiagnosticCode::FIXTURE_MISMATCH,
	    duckdb_api::connector::PackageDiagnosticPhase::FIXTURE, {"fixtures/index.yaml", 7, 5, "$.cases[1]"}, "github",
	    "authenticated_repositories", "github_authenticated_repositories", nullptr, "private_visibility_matching");
	Require(std::string(duckdb_api::connector::PackageDiagnosticCodeName(diagnostic.Code())) ==
	                "DUCKDB_API_FIXTURE_MISMATCH" &&
	            std::string(duckdb_api::connector::PackageDiagnosticPhaseName(diagnostic.Phase())) == "fixture" &&
	            diagnostic.FixtureCase() == "private_visibility_matching",
	        "fixture mismatch diagnostic lost its closed name, phase, or safe case identity");
}

} // namespace

int main(int argc, char **argv) {
	try {
		Require(argc == 2, "usage: package_fixture_coverage_tests ABSOLUTE_REPOSITORY_ROOT");
		TestFixtureDiagnosticVocabulary();
		TestGithubCoverageMatchesAcceptedMapping(argv[1]);
		TestCoverageIsCompiledFactDriven(argv[1]);
		std::cout << "package fixture coverage tests passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
