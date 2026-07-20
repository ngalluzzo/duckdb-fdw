#pragma once

#include "duckdb_api/compiled_package_generation.hpp"
#include "duckdb_api/internal/connector/package/failsafe_yaml.hpp"
#include "duckdb_api/internal/connector/package/package_source.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace duckdb_api {
namespace connector {

enum class PackageDiagnosticCode : std::uint8_t {
	UNSUPPORTED_SPEC,
	UNSUPPORTED_DIALECT,
	MALFORMED_YAML,
	UNKNOWN_FIELD,
	MISSING_FIELD,
	DUPLICATE_ID,
	INVALID_REFERENCE,
	INVALID_IDENTIFIER,
	INVALID_TYPE,
	INVALID_EXTRACTOR,
	RESERVED_INPUT,
	UNSUPPORTED_DECLARATION,
	INVALID_SELECTOR,
	INVALID_PREDICATE,
	INVALID_GRAPHQL_PROFILE,
	POLICY_WIDENING,
	RESOURCE_EXHAUSTED,
	PACKAGE_IDENTITY
};

enum class PackageDiagnosticPhase : std::uint8_t { SOURCE, SYNTAX, SCHEMA, REFERENCE, COMPILE };

const char *PackageDiagnosticCodeName(PackageDiagnosticCode code);
const char *PackageDiagnosticPhaseName(PackageDiagnosticPhase phase);

struct PackageSourceCoordinate {
	std::string file;
	std::uint64_t line;
	std::uint64_t column;
	std::string yaml_path;
};

// Stable secret-safe compiler diagnostic. It carries structural identifiers
// and package-relative coordinates only; source scalar content, absolute roots,
// generated documents, request bodies, and credential values are unrepresentable.
class PackageDiagnostic {
public:
	PackageDiagnostic(PackageDiagnosticCode code, PackageDiagnosticPhase phase, PackageSourceCoordinate coordinate,
	                  std::string connector, std::string relation, std::string operation,
	                  std::shared_ptr<const PackageSourceCoordinate> related = nullptr);

	PackageDiagnosticCode Code() const;
	PackageDiagnosticPhase Phase() const;
	const PackageSourceCoordinate &Coordinate() const;
	const std::string &Connector() const;
	const std::string &Relation() const;
	const std::string &Operation() const;
	const PackageSourceCoordinate *Related() const;

private:
	PackageDiagnosticCode code;
	PackageDiagnosticPhase phase;
	PackageSourceCoordinate coordinate;
	std::string connector;
	std::string relation;
	std::string operation;
	std::shared_ptr<const PackageSourceCoordinate> related;
};

struct PackageCompilerLimits {
	FailsafeYamlLimits yaml;
	std::uint64_t max_diagnostics;

	static PackageCompilerLimits V1();
};

// A candidate is all-or-nothing: Generation() is present only after syntax,
// closed schema, references, policy, resources, protocol generation, and the
// immutable model constructor all succeed. Diagnostics are deduplicated and
// returned in RFC 0013's total order, capped at 255 details plus one terminal
// resource record. The service performs no network, credential, catalog, or
// fixture work and never retains the call-scoped cancellation view.
class PackageCompileResult {
public:
	PackageCompileResult(std::shared_ptr<const CompiledPackageGeneration> generation,
	                     std::vector<PackageDiagnostic> diagnostics);

	const CompiledPackageGeneration *Generation() const;
	const std::vector<PackageDiagnostic> &Diagnostics() const;
	bool Succeeded() const;

private:
	std::shared_ptr<const CompiledPackageGeneration> generation;
	std::vector<PackageDiagnostic> diagnostics;
};

PackageCompileResult CompilePackage(const PackageSourceSnapshot &snapshot, const PackageCompilerLimits &host_limits,
                                    PackageCancellation &cancellation);

// Bounded production entry point for an explicit canonical local root. It
// performs no DuckDB registration and stages no Runtime state: success owns
// only the immutable generation, whose QueryRegistration() returns the narrow
// registration view and opaque generation handle. Source-custody failures are
// converted to stable secret-safe diagnostics; cancellation retains its
// existing exception boundary.
PackageCompileResult CompileLocalPackageRoot(const std::string &absolute_root, const PackageSourceLimits &source_limits,
                                             const PackageCompilerLimits &compiler_limits,
                                             PackageCancellation &cancellation);
PackageCompileResult CompileLocalPackageRoot(const std::string &absolute_root, PackageCancellation &cancellation);

// Permanent product-asset identity for the byte-copied normative RFC 0013
// schema. Compilation refuses to run if the embedded product asset drifts.
const char *ConnectorPackageV1SchemaDigest();
bool VerifyConnectorPackageV1SchemaAsset();

} // namespace connector
} // namespace duckdb_api
