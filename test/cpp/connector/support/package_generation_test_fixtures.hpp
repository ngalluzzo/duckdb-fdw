#pragma once

#include "duckdb_api/compiled_package_generation.hpp"

#include <string>

namespace duckdb_api_test {

extern const char PACKAGE_TYPED_RELATION[];
extern const char PACKAGE_DISTINCT_RELATION[];
extern const char PACKAGE_PREDICATE_RELATION[];

// Closed structural variants used to prove every RFC 0013 reload category.
// They are valid immutable generations, not malformed-construction probes.
enum class PackageCompatibilityFixture {
	BASELINE,
	CONNECTOR_ID_CHANGED,
	RELATION_REMOVED,
	RELATION_REORDERED,
	RELATION_INSERTED_BEFORE,
	RELATION_CHANGED,
	COLUMN_CHANGED,
	INPUT_CHANGED,
	SELECTOR_REFERENCE_CHANGED,
	OPERATION_CHANGED,
	PREDICATE_CHANGED,
	AUTHENTICATION_CHANGED,
	RESOURCE_CHANGED,
	OPERATION_ORIGIN_CHANGED,
	NETWORK_POLICY_CHANGED,
	APPEND_RELATION
};

// Controlled package for future Semantics tests. The named relation carries
// ordered BOOLEAN/BIGINT/VARCHAR inputs, an absent default, concrete defaults,
// a typed NULL default, one input-selected operation, and one fallback. The
// generation also contains a structurally distinct valid relation. Consumers
// link the package-generation fixture target and use only immutable public
// Connector APIs; no internal builder or test construction access is exposed.
duckdb_api::CompiledPackageGeneration
BuildTypedFallbackPackageGenerationFixture(const std::string &package_version = "1.2.3", char digest_fill = 'a');

// Same structural package, except the typed relation has two equally ranked
// eligible operations and no fallback. This remains valid Connector metadata;
// Relational Semantics owns the eventual tie diagnostic.
duckdb_api::CompiledPackageGeneration
BuildTypedTiePackageGenerationFixture(const std::string &package_version = "1.2.3", char digest_fill = 'b');

// One-relation valid package whose name, schema, input shape, and response
// structure differ from the typed fixture relation.
duckdb_api::CompiledPackageGeneration
BuildDistinctPackageGenerationFixture(const std::string &package_version = "1.2.3", char digest_fill = 'c');

// Bounded compatibility oracle. Identity parameters vary only accepted package
// version/digest facts; the closed variant controls normalized structure.
duckdb_api::CompiledPackageGeneration BuildPackageCompatibilityFixture(PackageCompatibilityFixture variant,
                                                                       const std::string &package_version = "1.2.3",
                                                                       char digest_fill = 'd');

} // namespace duckdb_api_test
