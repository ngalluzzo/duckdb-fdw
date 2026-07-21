#pragma once

#include "duckdb_api/package_compatibility.hpp"
#include "duckdb_api/package_fixture_runner.hpp"

#include <memory>
#include <vector>

namespace duckdb_api {
namespace connector {

namespace internal {
class PackageFixtureSourceCandidateBuilder;
}

// One closed source-backed fixture variant compiled through the production
// package boundary. A successful value owns opaque candidate custody; a failed
// value owns the compiler's bounded stable diagnostics. Reload variants also
// carry Connector's generation-bound compatibility decision. Private copied
// roots and semantic source bytes are deliberately not observable.
class PackageFixtureSourceCandidate {
public:
	PackageFixtureSourceCandidate(const PackageFixtureSourceCandidate &) = default;
	PackageFixtureSourceCandidate(PackageFixtureSourceCandidate &&) = default;
	PackageFixtureSourceCandidate &operator=(const PackageFixtureSourceCandidate &) = delete;
	PackageFixtureSourceCandidate &operator=(PackageFixtureSourceCandidate &&) = delete;

	const PackageFixtureCoverageEntry &CoverageEntry() const;
	bool Succeeded() const noexcept;
	const CompiledLocalPackage *Candidate() const noexcept;
	const std::vector<PackageDiagnostic> &Diagnostics() const noexcept;
	const PackageReloadDecision *ReloadDecision() const noexcept;

private:
	class State;
	explicit PackageFixtureSourceCandidate(std::shared_ptr<const State> state);
	std::shared_ptr<const State> state;

	friend class internal::PackageFixtureSourceCandidateBuilder;
};

// Builds only Connector-owned variants that require a distinct semantic-source
// candidate: all reload variants and max_document_bytes boundary/one-over
// GraphQL resource variants. The typed entry must come from independent
// coverage derivation. Work is synchronous, bounded by ordinary v1 source and
// compiler limits, cancellation-aware, and uses a private no-follow copy of
// the retained semantic snapshot. It never reads fixtures or performs network,
// credential, planning, execution, or publication work. Other scopes remain
// the owning provider's responsibility and are rejected rather than marked.
PackageFixtureSourceCandidate BuildPackageFixtureSourceCandidate(const CompiledLocalPackage &active,
                                                                 const PackageFixtureCoverageEntry &coverage_entry,
                                                                 PackageCancellation &cancellation);

} // namespace connector
} // namespace duckdb_api
