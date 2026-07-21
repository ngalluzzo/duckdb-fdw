#include "connector/support/package_compiler_test_fixtures.hpp"

#include "duckdb_api/package_fixture_candidates.hpp"
#include "support/require.hpp"

#include <iostream>
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

const duckdb_api::connector::PackageFixtureCoverageEntry &
FindEntry(const duckdb_api::connector::PackageFixtureCoverage &coverage, const std::string &key) {
	for (const auto &entry : coverage.Entries()) {
		if (entry.key == key) {
			return entry;
		}
	}
	throw std::runtime_error("fixture candidate test coverage key is absent");
}

void TestReloadCandidates(const duckdb_api::CompiledLocalPackage &active,
                          const duckdb_api::connector::PackageFixtureCoverage &coverage) {
	struct Expected {
		const char *variant;
		duckdb_api::PackageReloadClassification classification;
	};
	const Expected expected[] = {
	    {"exact_no_op", duckdb_api::PackageReloadClassification::EXACT_NO_OP},
	    {"compatible_patch", duckdb_api::PackageReloadClassification::COMPATIBLE_PROVENANCE_PATCH},
	    {"compatible_minor", duckdb_api::PackageReloadClassification::COMPATIBLE_PROVENANCE_MINOR},
	    {"version_reuse_rejected", duckdb_api::PackageReloadClassification::REJECTED_PACKAGE_IDENTITY},
	    {"downgrade_rejected", duckdb_api::PackageReloadClassification::REJECTED_PACKAGE_IDENTITY},
	    {"incompatible_rejected", duckdb_api::PackageReloadClassification::INCOMPATIBLE_RELOAD},
	};
	NeverCancel cancellation;
	for (const auto &item : expected) {
		const auto &entry = FindEntry(coverage, "reload_" + std::string(item.variant));
		const auto candidate = duckdb_api::connector::BuildPackageFixtureSourceCandidate(active, entry, cancellation);
		Require(candidate.Succeeded() && candidate.Candidate() != nullptr && candidate.Diagnostics().empty() &&
		            candidate.ReloadDecision() != nullptr &&
		            candidate.ReloadDecision()->Classification() == item.classification &&
		            candidate.ReloadDecision()->Matches(active.Generation().OpaqueHandle(),
		                                                candidate.Candidate()->Generation().OpaqueHandle()) &&
		            candidate.CoverageEntry().key == entry.key,
		        std::string("reload source candidate did not produce its exact production decision: ") + item.variant);
		const auto reacquired = duckdb_api::connector::RecompileLocalPackage(
		    *candidate.Candidate(), candidate.Candidate()->Generation().OpaqueHandle(), cancellation);
		Require(reacquired.Succeeded(),
		        std::string("reload source candidate did not retain private source custody: ") + item.variant);
	}
}

void TestGraphqlDocumentCandidates(const duckdb_api::CompiledLocalPackage &active,
                                   const duckdb_api::connector::PackageFixtureCoverage &coverage) {
	NeverCancel cancellation;
	const auto &boundary = FindEntry(
	    coverage, "resource_viewer_repository_metrics_github_viewer_repository_metrics_max_document_bytes_boundary");
	const auto accepted = duckdb_api::connector::BuildPackageFixtureSourceCandidate(active, boundary, cancellation);
	Require(accepted.Succeeded() && accepted.Candidate() != nullptr && accepted.ReloadDecision() == nullptr,
	        "GraphQL document boundary did not compile through the production candidate service");
	const auto *relation = accepted.Candidate()->Generation().Connector().FindRelation("viewer_repository_metrics");
	Require(relation != nullptr && relation->Operations().size() == 1 &&
	            relation->Operations()[0].Graphql().max_document_bytes ==
	                relation->Operations()[0].Graphql().document.size(),
	        "GraphQL document boundary candidate did not land on the exact generated-document size");

	const auto &one_over = FindEntry(
	    coverage,
	    "resource_viewer_repository_metrics_github_viewer_repository_metrics_max_document_bytes_one_over_rejected");
	const auto rejected = duckdb_api::connector::BuildPackageFixtureSourceCandidate(active, one_over, cancellation);
	Require(!rejected.Succeeded() && rejected.Candidate() == nullptr && rejected.ReloadDecision() == nullptr &&
	            rejected.Diagnostics().size() == 1 &&
	            rejected.Diagnostics()[0].Code() ==
	                duckdb_api::connector::PackageDiagnosticCode::INVALID_GRAPHQL_PROFILE,
	        "GraphQL document one-over candidate did not fail through the production compiler");
}

void TestSourceIdentityCandidates(const duckdb_api::CompiledLocalPackage &active,
                                  const duckdb_api::connector::PackageFixtureCoverage &coverage) {
	NeverCancel cancellation;
	const auto copied = duckdb_api::connector::BuildPackageFixtureSourceCandidate(
	    active, FindEntry(coverage, "source_identity_copied_root"), cancellation);
	Require(copied.Succeeded() && copied.Candidate() != nullptr &&
	            copied.SourceIdentityOutcome() ==
	                duckdb_api::connector::PackageFixtureSourceIdentityOutcome::IDENTICAL_GENERATION &&
	            copied.Candidate()->Generation().Identity().PackageDigest() ==
	                active.Generation().Identity().PackageDigest(),
	        "copied-root source candidate did not preserve the exact semantic identity");
	const auto changed = duckdb_api::connector::BuildPackageFixtureSourceCandidate(
	    active, FindEntry(coverage, "source_identity_byte_change"), cancellation);
	Require(changed.Succeeded() && changed.Candidate() != nullptr &&
	            changed.SourceIdentityOutcome() ==
	                duckdb_api::connector::PackageFixtureSourceIdentityOutcome::CHANGED_GENERATION &&
	            changed.Candidate()->Generation().Identity().PackageDigest() !=
	                active.Generation().Identity().PackageDigest(),
	        "byte-change source candidate did not produce a distinct compiled identity");

	for (const auto *variant : {"symlink_rejected", "hardlink_rejected", "entry_change_rejected",
	                            "unlisted_relation_rejected", "case_collision_rejected"}) {
		const auto candidate = duckdb_api::connector::BuildPackageFixtureSourceCandidate(
		    active, FindEntry(coverage, "source_identity_" + std::string(variant)), cancellation);
		Require(!candidate.Succeeded() && candidate.Candidate() == nullptr && candidate.Diagnostics().size() == 1 &&
		            candidate.SourceIdentityOutcome() ==
		                duckdb_api::connector::PackageFixtureSourceIdentityOutcome::SOURCE_IDENTITY_REJECTED &&
		            candidate.Diagnostics()[0].Code() ==
		                duckdb_api::connector::PackageDiagnosticCode::PACKAGE_IDENTITY &&
		            candidate.Diagnostics()[0].Phase() == duckdb_api::connector::PackageDiagnosticPhase::SOURCE,
		        std::string("source identity candidate did not fail through production custody: ") + variant);
	}
}

void TestNoCandidateSource(const std::string &repository_root) {
	NeverCancel cancellation;
	const auto compiled = duckdb_api::connector::CompileLocalPackageRoot(
	    repository_root + "/test/fixtures/package_graphql_non_github", cancellation);
	Require(compiled.Succeeded() && compiled.Package() != nullptr, "multi-operation package fixture did not compile");
	const duckdb_api::CompiledLocalPackage active(*compiled.Package());
	const auto coverage = duckdb_api::connector::DerivePackageFixtureCoverage(active.Generation());
	const auto &entry = FindEntry(coverage, "selection_regional_events_no_candidate_rejected");
	const auto candidate = duckdb_api::connector::BuildPackageFixtureSourceCandidate(active, entry, cancellation);
	Require(candidate.Succeeded() && candidate.Candidate() != nullptr && candidate.Diagnostics().empty() &&
	            candidate.ReloadDecision() == nullptr &&
	            candidate.SourceIdentityOutcome() ==
	                duckdb_api::connector::PackageFixtureSourceIdentityOutcome::NOT_APPLICABLE,
	        "no-candidate selection source did not compile through the production boundary");
	const auto *relation = candidate.Candidate()->Generation().Connector().FindRelation("regional_events");
	Require(relation != nullptr && relation->Operations().size() == 2,
	        "no-candidate selection source lost its multi-operation relation");
	for (const auto &operation : relation->Operations()) {
		Require(!operation.fallback && !operation.selector.RequiredInputReferences().empty(),
		        "no-candidate selection source retained an unconditionally eligible operation");
	}
}

void TestCandidateDispatchAndCancellation(const duckdb_api::CompiledLocalPackage &active,
                                          const duckdb_api::connector::PackageFixtureCoverage &coverage) {
	NeverCancel cancellation;
	const auto &unsupported = FindEntry(coverage, "diagnostic_duckdb_api_fixture_mismatch");
	try {
		(void)duckdb_api::connector::BuildPackageFixtureSourceCandidate(active, unsupported, cancellation);
	} catch (const std::invalid_argument &) {
		const auto &exact = FindEntry(coverage, "reload_exact_no_op");
		AlwaysCancel cancelled;
		try {
			(void)duckdb_api::connector::BuildPackageFixtureSourceCandidate(active, exact, cancelled);
		} catch (const duckdb_api::connector::PackageCompilationCancelled &) {
			return;
		}
		throw std::runtime_error("fixture source candidate ignored cancellation");
	}
	throw std::runtime_error("fixture source candidate accepted a provider-owned coverage scope");
}

} // namespace

int main(int argc, char **argv) {
	try {
		Require(argc == 2, "usage: package_fixture_candidate_tests ABSOLUTE_REPOSITORY_ROOT");
		const auto active = duckdb_api_test::CompileRepositoryGithubLocalPackageFixture(argv[1]);
		const auto coverage = duckdb_api::connector::DerivePackageFixtureCoverage(active.Generation());
		TestReloadCandidates(active, coverage);
		TestGraphqlDocumentCandidates(active, coverage);
		TestSourceIdentityCandidates(active, coverage);
		TestNoCandidateSource(argv[1]);
		TestCandidateDispatchAndCancellation(active, coverage);
		std::cout << "package fixture candidate tests passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
