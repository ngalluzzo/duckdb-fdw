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

// Typed evidence for package-root identity variants. Positive candidates state
// whether semantic bytes preserve or change the generation digest; rejected
// variants are accepted only after the stable package-identity/source outcome.
// Other fixture scopes return NOT_APPLICABLE.
enum class PackageFixtureSourceIdentityOutcome {
	NOT_APPLICABLE,
	IDENTICAL_GENERATION,
	CHANGED_GENERATION,
	SOURCE_IDENTITY_REJECTED
};

enum class PackageFixtureCompilerCancellationOutcome {
	SOURCE_ENUMERATION,
	SOURCE_READ,
	YAML_PARSE,
	REFERENCE_VALIDATION,
	GENERATION_VALIDATION
};

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
	// Returns the independently checked source-identity relation for this
	// candidate; callers do not infer it from a digest or diagnostic string.
	PackageFixtureSourceIdentityOutcome SourceIdentityOutcome() const noexcept;

private:
	class State;
	explicit PackageFixtureSourceCandidate(std::shared_ptr<const State> state);
	std::shared_ptr<const State> state;

	friend class internal::PackageFixtureSourceCandidateBuilder;
};

// Builds only Connector-owned variants that require a distinct semantic-source
// candidate: all reload variants; copied-root, byte-change, symbolic-link,
// hard-link, entry-change, unlisted-relation, and portable case-collision
// source-identity variants; the no-candidate relation-selection variant; plus
// max_document_bytes boundary/one-over GraphQL resource variants. The typed
// entry must come from independent coverage derivation. Work is synchronous,
// bounded by ordinary v1 source and compiler limits, cancellation-aware, and
// uses a private no-follow copy of the retained semantic snapshot. It never
// reads fixtures or performs network, credential, planning, execution, or
// publication work. Other variants and scopes remain the owning provider's
// responsibility and are rejected rather than marked.
PackageFixtureSourceCandidate BuildPackageFixtureSourceCandidate(const CompiledLocalPackage &active,
                                                                 const PackageFixtureCoverageEntry &coverage_entry,
                                                                 PackageCancellation &cancellation);

// Executes one Connector-owned compiler-cancellation coverage entry against a
// private semantic-source copy. An internal phase hook only arms caller-owned
// cancellation immediately before the ordinary production check; it cannot
// replace that check. The returned enum proves the exact reached boundary.
// publication_wait belongs to Query publication and is rejected here.
PackageFixtureCompilerCancellationOutcome
RunPackageFixtureCompilerCancellation(const CompiledLocalPackage &active,
                                      const PackageFixtureCoverageEntry &coverage_entry,
                                      PackageCancellation &cancellation);

} // namespace connector
} // namespace duckdb_api
