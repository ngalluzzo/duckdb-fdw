#include "duckdb_api/package_fixture_candidates.hpp"

#include "package_fixture_candidate_internal.hpp"

#include <stdexcept>
#include <utility>

namespace duckdb_api {
namespace connector {

class PackageFixtureSourceCandidate::State {
public:
	State(PackageFixtureCoverageEntry coverage_entry_p,
	      std::unique_ptr<internal::PrivatePackageSourceCopy> source_copy_p,
	      std::shared_ptr<const CompiledLocalPackage> candidate_p, std::vector<PackageDiagnostic> diagnostics_p,
	      std::shared_ptr<const PackageReloadDecision> reload_decision_p)
	    : coverage_entry(std::move(coverage_entry_p)), source_copy(std::move(source_copy_p)),
	      candidate(std::move(candidate_p)), diagnostics(std::move(diagnostics_p)),
	      reload_decision(std::move(reload_decision_p)) {
		if ((candidate != nullptr) == !diagnostics.empty() || (reload_decision != nullptr && candidate == nullptr)) {
			throw std::invalid_argument("fixture source candidate must contain exactly one compiler outcome");
		}
	}

	PackageFixtureCoverageEntry coverage_entry;
	std::unique_ptr<internal::PrivatePackageSourceCopy> source_copy;
	std::shared_ptr<const CompiledLocalPackage> candidate;
	std::vector<PackageDiagnostic> diagnostics;
	std::shared_ptr<const PackageReloadDecision> reload_decision;
};

namespace internal {

class PackageFixtureSourceCandidateBuilder {
public:
	static PackageFixtureSourceCandidate Build(PackageFixtureCoverageEntry coverage_entry,
	                                           std::unique_ptr<PrivatePackageSourceCopy> source_copy,
	                                           std::shared_ptr<const CompiledLocalPackage> candidate,
	                                           std::vector<PackageDiagnostic> diagnostics,
	                                           std::shared_ptr<const PackageReloadDecision> reload_decision) {
		return PackageFixtureSourceCandidate(std::shared_ptr<const PackageFixtureSourceCandidate::State>(
		    new PackageFixtureSourceCandidate::State(std::move(coverage_entry), std::move(source_copy),
		                                             std::move(candidate), std::move(diagnostics),
		                                             std::move(reload_decision))));
	}
};

} // namespace internal

PackageFixtureSourceCandidate::PackageFixtureSourceCandidate(std::shared_ptr<const State> state_p)
    : state(std::move(state_p)) {
	if (!state) {
		throw std::invalid_argument("fixture source candidate state cannot be empty");
	}
}

const PackageFixtureCoverageEntry &PackageFixtureSourceCandidate::CoverageEntry() const {
	return state->coverage_entry;
}

bool PackageFixtureSourceCandidate::Succeeded() const noexcept {
	return state->candidate != nullptr;
}

const CompiledLocalPackage *PackageFixtureSourceCandidate::Candidate() const noexcept {
	return state->candidate.get();
}

const std::vector<PackageDiagnostic> &PackageFixtureSourceCandidate::Diagnostics() const noexcept {
	return state->diagnostics;
}

const PackageReloadDecision *PackageFixtureSourceCandidate::ReloadDecision() const noexcept {
	return state->reload_decision.get();
}

PackageFixtureSourceCandidate BuildPackageFixtureSourceCandidate(const CompiledLocalPackage &active,
                                                                 const PackageFixtureCoverageEntry &coverage_entry,
                                                                 PackageCancellation &cancellation) {
	auto sources = internal::BuildFixtureCandidateSources(active, coverage_entry, cancellation);
	auto source_copy = internal::PrivatePackageSourceCopy::Create(sources, cancellation);
	auto compiled = CompileLocalPackageRoot(source_copy->Root(), cancellation);
	if (!compiled.Succeeded()) {
		return internal::PackageFixtureSourceCandidateBuilder::Build(coverage_entry, nullptr, nullptr,
		                                                             compiled.Diagnostics(), nullptr);
	}
	auto candidate = std::shared_ptr<const CompiledLocalPackage>(new CompiledLocalPackage(*compiled.Package()));
	std::shared_ptr<const PackageReloadDecision> decision;
	if (coverage_entry.scope == PackageFixtureCoverageScope::RELOAD) {
		decision = std::shared_ptr<const PackageReloadDecision>(
		    new PackageReloadDecision(ClassifyPackageReload(active.Generation(), candidate->Generation())));
	}
	return internal::PackageFixtureSourceCandidateBuilder::Build(coverage_entry, std::move(source_copy),
	                                                             std::move(candidate), {}, std::move(decision));
}

} // namespace connector
} // namespace duckdb_api
