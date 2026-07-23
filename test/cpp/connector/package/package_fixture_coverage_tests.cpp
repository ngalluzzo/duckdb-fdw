#include "connector/support/package_compiler_test_fixtures.hpp"

#include "duckdb_api/content_digest.hpp"
#include "duckdb_api/local_package_compiler.hpp"
#include "duckdb_api/package_fixture_runner.hpp"
#include "support/require.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

using duckdb_api_test::Require;

class NeverCancel final : public duckdb_api::connector::PackageCancellation {
public:
	bool IsCancellationRequested() const noexcept override {
		return false;
	}
};

class AlwaysCancel final : public duckdb_api::connector::PackageCancellation {
public:
	bool IsCancellationRequested() const noexcept override {
		return true;
	}
};

class FirstCaseProbe final : public duckdb_api::connector::PackageFixtureExecutionService {
public:
	duckdb_api::connector::PackageFixtureObservation
	Execute(const duckdb_api::CompiledPackageGeneration &, const duckdb_api::connector::PackageFixtureCase &,
	        const std::vector<duckdb_api::connector::PackageFixtureCoverageEntry> &,
	        duckdb_api::connector::PackageCancellation &) override {
		calls++;
		throw std::runtime_error("corpus probe stops at the first identity-verified case");
	}

	std::size_t calls = 0;
};

std::string ReadBytes(const std::string &path) {
	std::ifstream input(path.c_str(), std::ios::binary);
	Require(static_cast<bool>(input), "fixture contract asset could not be opened");
	std::ostringstream bytes;
	bytes << input.rdbuf();
	return bytes.str();
}

bool Contains(const duckdb_api::connector::PackageFixtureCoverage &coverage, const std::string &key) {
	return std::find(coverage.RequiredKeys().begin(), coverage.RequiredKeys().end(), key) !=
	       coverage.RequiredKeys().end();
}

void TestGithubCoverageMatchesAcceptedMapping(const std::string &repository_root) {
	const auto generation = duckdb_api_test::CompileRepositoryGithubGenerationFixture(repository_root);
	const auto coverage = duckdb_api::connector::DerivePackageFixtureCoverage(generation);
	Require(coverage.RequiredKeys().size() == 258,
	        "repository GitHub package did not derive all 258 accepted coverage keys");
	Require(coverage.Entries().size() == coverage.RequiredKeys().size(),
	        "typed coverage registry does not align one-for-one with rendered keys");
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
	const auto predicate_entry =
	    std::find_if(coverage.Entries().begin(), coverage.Entries().end(),
	                 [](const duckdb_api::connector::PackageFixtureCoverageEntry &entry) {
		                 return entry.key == "predicate_authenticated_repositories_private_visibility_positive";
	                 });
	const auto diagnostic_entry = std::find_if(coverage.Entries().begin(), coverage.Entries().end(),
	                                           [](const duckdb_api::connector::PackageFixtureCoverageEntry &entry) {
		                                           return entry.key == "diagnostic_duckdb_api_fixture_mismatch";
	                                           });
	Require(predicate_entry != coverage.Entries().end() &&
	            predicate_entry->scope == duckdb_api::connector::PackageFixtureCoverageScope::PREDICATE &&
	            predicate_entry->relation == "authenticated_repositories" &&
	            predicate_entry->predicate == "private_visibility" && predicate_entry->variant == "positive" &&
	            diagnostic_entry != coverage.Entries().end() &&
	            diagnostic_entry->scope == duckdb_api::connector::PackageFixtureCoverageScope::DIAGNOSTIC &&
	            diagnostic_entry->diagnostic == "DUCKDB_API_FIXTURE_MISMATCH",
	        "typed coverage registry lost structural predicate or diagnostic bindings");

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
	            Contains(coverage, "predicate_regional_events_public_events_positive") &&
	            Contains(coverage, "graphql_regional_events_regional_event_graph_serialized_body_identity") &&
	            !Contains(coverage, "predicate_authenticated_repositories_private_visibility_positive"),
	        "coverage derivation used repository identity instead of compiled feature facts");
	Require(coverage.RequiredKeys().size() == 198,
	        "controlled package did not derive its complete semantic coverage matrix");

	const auto *relation = generation.Connector().FindRelation("regional_events");
	Require(relation != nullptr && relation->Inputs().size() == 7 && relation->Operations().size() == 3 &&
	            relation->PredicateMappings().size() == 2,
	        "controlled package lost its input, operation-selection, or predicate matrix");
	const auto operation = [&](const std::string &name) -> const duckdb_api::CompiledOperation & {
		const auto found =
		    std::find_if(relation->Operations().begin(), relation->Operations().end(),
		                 [&](const duckdb_api::CompiledOperation &candidate) { return candidate.name == name; });
		Require(found != relation->Operations().end(), "controlled package operation is missing");
		return *found;
	};
	const auto input = [&](const std::string &name) -> const duckdb_api::CompiledRelationInput & {
		const auto found =
		    std::find_if(relation->Inputs().begin(), relation->Inputs().end(),
		                 [&](const duckdb_api::CompiledRelationInput &candidate) { return candidate.Name() == name; });
		Require(found != relation->Inputs().end(), "controlled package input is missing");
		return *found;
	};
	Require(input("include_cancelled").Default().HasDefault() &&
	            input("include_cancelled").Default().Value().Type() == duckdb_api::CompiledScalarType::BOOLEAN &&
	            !input("include_cancelled").Default().Value().Boolean() &&
	            input("minimum_attendance").Default().HasDefault() &&
	            input("minimum_attendance").Default().Value().Type() == duckdb_api::CompiledScalarType::BIGINT &&
	            input("minimum_attendance").Default().Value().Bigint() == 25 &&
	            input("audience").Default().HasDefault() &&
	            input("audience").Default().Value().Type() == duckdb_api::CompiledScalarType::VARCHAR &&
	            input("audience").Default().Value().Varchar() == "public" && input("audience").Nullable() &&
	            input("note").Default().HasDefault() && input("note").Default().Value().IsNull() &&
	            input("note").Nullable(),
	        "controlled package lost typed BOOLEAN/BIGINT/VARCHAR or typed NULL defaults");
	const auto &graph = operation("regional_event_graph");
	const auto &rest = operation("regional_event_rest");
	const auto &fallback = operation("fallback_events");
	const auto requires_relation_input = [](const duckdb_api::CompiledOperation &candidate, const std::string &id) {
		return std::find_if(candidate.selector.RequiredInputReferences().begin(),
		                    candidate.selector.RequiredInputReferences().end(),
		                    [&](const duckdb_api::CompiledRequiredInputReference &reference) {
			                    return reference.Kind() == duckdb_api::CompiledRequiredInputKind::RELATION_INPUT &&
			                           reference.Id() == id;
		                    }) != candidate.selector.RequiredInputReferences().end();
	};
	Require(!graph.fallback && graph.selector.RequiredInputReferences().size() == 2 && !rest.fallback &&
	            requires_relation_input(graph, "region") && requires_relation_input(graph, "graph_view") &&
	            rest.selector.RequiredInputReferences().size() == 2 && requires_relation_input(rest, "rest_view") &&
	            requires_relation_input(rest, "include_cancelled") && fallback.fallback &&
	            fallback.selector.RequiredInputReferences().empty(),
	        "controlled package lost its equal-rank unique/tie candidates or unconditional fallback");
	const auto nullable_rest_binding = [&](const std::string &name) {
		const auto &query = rest.Rest().request.query_parameters;
		const auto found =
		    std::find_if(query.begin(), query.end(),
		                 [&](const duckdb_api::CompiledQueryParameter &field) { return field.name == name; });
		return found != query.end() && found->source == duckdb_api::CompiledQueryValueSource::RELATION_INPUT &&
		       found->source_id == name && found->omit_when_null;
	};
	Require(nullable_rest_binding("region") && nullable_rest_binding("audience") && nullable_rest_binding("note"),
	        "controlled REST operation no longer proves nullable inputs are omitted at the request boundary");
	Require(relation->PredicateMappings()[0].Accuracy() == duckdb_api::CompiledPredicateAccuracy::EXACT &&
	            relation->PredicateMappings()[1].Accuracy() == duckdb_api::CompiledPredicateAccuracy::SUPERSET &&
	            graph.Graphql().document_digest == "ca2060e0db0b535bbf4a2b96050127159fb3e953cd52bd17dd8b2ae955464d28",
	        "controlled package lost exact/superset proof facts or its distinct GraphQL recipe identity");
}

void TestRetryV2CoverageUsesTheSameOfflineContract(const std::string &repository_root) {
	const auto generation = duckdb_api_test::CompileRetryV2GenerationFixture(repository_root);
	const auto coverage = duckdb_api::connector::DerivePackageFixtureCoverage(generation);
	Require(generation.Identity().SpecIdentifier() == "duckdb_api/v2" &&
	            Contains(coverage, "operation_duplicate_events_list_duplicate_events_success") &&
	            Contains(coverage, "operation_duplicate_graphql_events_list_duplicate_graphql_events_success") &&
	            Contains(coverage, "pagination_duplicate_events_list_duplicate_events_single_page_termination") &&
	            Contains(coverage, "graphql_duplicate_graphql_events_list_duplicate_graphql_events_"
	                               "serialized_body_identity"),
	        "duckdb_api/v2 retry operations were rejected or omitted by offline fixture coverage derivation");
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

void TestFixtureContractAssetsAreByteLocked(const std::string &repository_root) {
	const auto schema = ReadBytes(repository_root + "/src/connector/package/assets/fixture-index-v1.schema.json");
	const auto mapping = ReadBytes(repository_root + "/src/connector/package/assets/fixture-coverage-v1.json");
	Require(duckdb_api::connector::VerifyPackageFixtureContractAssets(),
	        "embedded fixture contract assets failed their content identities");
	Require("sha256." + duckdb_api::ComputeSha256Hex(schema) ==
	                duckdb_api::connector::PackageFixtureIndexV1SchemaDigest() &&
	            "sha256." + duckdb_api::ComputeSha256Hex(mapping) ==
	                duckdb_api::connector::PackageFixtureCoverageV1MappingDigest(),
	        "production fixture contract asset bytes drifted from their accepted identities");
	Require(schema == ReadBytes(repository_root + "/docs/rfcs/evidence/0022/fixture-index-v1.schema.json") &&
	            mapping == ReadBytes(repository_root + "/docs/rfcs/evidence/0022/fixture-coverage-v1.json"),
	        "production fixture assets are not exact copies of accepted RFC 0022 evidence");
}

duckdb_api::CompiledLocalPackage CompileControlledPackage(const std::string &repository_root) {
	NeverCancel cancellation;
	const auto result = duckdb_api::connector::CompileLocalPackageRoot(
	    repository_root + "/test/fixtures/package_graphql_non_github", cancellation);
	Require(result.Succeeded() && result.Package() != nullptr, "controlled fixture package did not compile");
	return *result.Package();
}

void RequireCompleteCorpusReachesProvider(const duckdb_api::CompiledLocalPackage &package,
                                          const std::string &first_case) {
	NeverCancel cancellation;
	FirstCaseProbe execution;
	const auto report = duckdb_api::connector::RunPackageFixtures(
	    package, execution, duckdb_api::connector::PackageFixtureLimits::V1(), cancellation);
	Require(!report.Succeeded() && report.ExecutedCases() == 0 && report.RequiredCoverageKeys().empty() &&
	            report.Diagnostics().size() == 1 &&
	            report.Diagnostics()[0].Code() == duckdb_api::connector::PackageDiagnosticCode::FIXTURE_MISMATCH &&
	            report.Diagnostics()[0].Phase() == duckdb_api::connector::PackageDiagnosticPhase::FIXTURE &&
	            report.Diagnostics()[0].FixtureCase() == first_case && execution.calls == 1,
	        "fixture corpus did not establish schema, exact claims, and exact payload identity before provider entry");
}

void TestCompleteCorporaAndPreProviderBoundaries(const std::string &repository_root) {
	const auto package = duckdb_api_test::CompileRepositoryGithubLocalPackageFixture(repository_root);
	RequireCompleteCorpusReachesProvider(package, "github_search_base");
	RequireCompleteCorpusReachesProvider(CompileControlledPackage(repository_root), "announcements_base");

	auto one_byte = duckdb_api::connector::PackageFixtureLimits::V1();
	one_byte.max_index_bytes = 1;
	NeverCancel cancellation;
	FirstCaseProbe execution;
	const auto exhausted = duckdb_api::connector::RunPackageFixtures(package, execution, one_byte, cancellation);
	Require(!exhausted.Succeeded() && exhausted.Diagnostics().size() == 1 &&
	            exhausted.Diagnostics()[0].Code() == duckdb_api::connector::PackageDiagnosticCode::RESOURCE_EXHAUSTED &&
	            exhausted.Diagnostics()[0].Coordinate().file == "fixtures/index.yaml" && execution.calls == 0,
	        "fixture index boundary did not fail closed before provider execution");

	AlwaysCancel cancelled;
	try {
		(void)duckdb_api::connector::RunPackageFixtures(package, execution,
		                                                duckdb_api::connector::PackageFixtureLimits::V1(), cancelled);
	} catch (const duckdb_api::connector::PackageCompilationCancelled &) {
		Require(execution.calls == 0, "cancelled fixture work reached its provider");
		return;
	}
	throw std::runtime_error("fixture cancellation did not preserve the public cancellation boundary");
}

} // namespace

int main(int argc, char **argv) {
	try {
		Require(argc == 2, "usage: package_fixture_coverage_tests ABSOLUTE_REPOSITORY_ROOT");
		TestFixtureDiagnosticVocabulary();
		TestFixtureContractAssetsAreByteLocked(argv[1]);
		TestGithubCoverageMatchesAcceptedMapping(argv[1]);
		TestCoverageIsCompiledFactDriven(argv[1]);
		TestRetryV2CoverageUsesTheSameOfflineContract(argv[1]);
		TestCompleteCorporaAndPreProviderBoundaries(argv[1]);
		std::cout << "package fixture coverage tests passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
