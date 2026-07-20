#pragma once

#include "duckdb_api/compiled_package_generation.hpp"
#include "duckdb_api/package_semver.hpp"

#include <string>

namespace duckdb_api {

// Exhaustive duckdb_api/v1 reload outcomes. Compatible provenance changes are
// separated by their package-version step; append-only structural evolution
// requires a MINOR step. Rejections map one-to-one to the accepted diagnostic
// vocabulary without carrying source coordinates, paths, or explanation text.
enum class PackageReloadClassification {
	EXACT_NO_OP,
	COMPATIBLE_PROVENANCE_PATCH,
	COMPATIBLE_PROVENANCE_MINOR,
	COMPATIBLE_APPEND_ONLY_MINOR,
	REJECTED_PACKAGE_IDENTITY,
	INCOMPATIBLE_RELOAD
};

// Immutable result of comparing an active and candidate generation. Connector
// owns classification; publication remains a Query/Runtime responsibility.
// Successful values have no diagnostic. Rejected values always report phase
// `compatibility` and exactly one RFC 0013 code.
class PackageReloadDecision {
public:
	PackageReloadDecision(const PackageReloadDecision &) = default;
	PackageReloadDecision(PackageReloadDecision &&) = default;
	PackageReloadDecision &operator=(const PackageReloadDecision &) = delete;
	PackageReloadDecision &operator=(PackageReloadDecision &&) = delete;

	PackageReloadClassification Classification() const;
	bool IsCompatible() const;
	bool Changed() const;
	bool HasDiagnostic() const;
	const char *DiagnosticCode() const;
	const char *DiagnosticPhase() const;
	const std::string &ConnectorId() const;

private:
	friend PackageReloadDecision ClassifyPackageReload(const CompiledPackageGeneration &active,
	                                                   const CompiledPackageGeneration &candidate);

	PackageReloadDecision(PackageReloadClassification classification, std::string connector_id);

	PackageReloadClassification classification;
	std::string connector_id;
};

// Compares immutable normalized compiled descriptors and identity. Package
// version, digest, source coordinates, package path, fixtures, README, and safe
// explanation are deliberately excluded from descriptor equality. The
// operation is deterministic, thread-safe, bounded by generation size, and
// performs no I/O or publication.
PackageReloadDecision ClassifyPackageReload(const CompiledPackageGeneration &active,
                                            const CompiledPackageGeneration &candidate);

} // namespace duckdb_api
