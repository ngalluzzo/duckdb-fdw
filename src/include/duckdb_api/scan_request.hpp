#pragma once

#include "duckdb_api/connector.hpp"

#include <string>
#include <vector>

namespace duckdb_api {

// Query Experience's immutable credential selector for the Semantics handoff.
// It contains only an exact DuckDB secret name copied from bind input: no
// secret value, provider/type/storage fact, catalog handle, or execution
// authority. An absent reference is the default and anonymous state.
class LogicalSecretReference {
public:
	LogicalSecretReference();

	static LogicalSecretReference Named(std::string exact_duckdb_secret_name);

	bool IsPresent() const noexcept;
	const std::string &Name() const;

	// Deterministic safe structural rendering. Hex escapes exact name bytes so
	// delimiters and non-printing bytes cannot alter a containing snapshot; it
	// is not encryption, hashing, or a secrecy boundary.
	std::string Snapshot() const;

private:
	explicit LogicalSecretReference(std::string exact_duckdb_secret_name);

	std::string exact_duckdb_secret_name;
};

// Capabilities actually exposed by the accepted DuckDB 1.5.4 native adapter.
// A false value is a conservative absence, never permission to reconstruct SQL
// text or infer unavailable query structure.
struct AdapterCapabilities {
	bool projection;
	bool filter;
	bool ordering;
	bool limit;
	bool offset;
	bool progress;
	bool cancellation;
	// Query may resolve one exact logical name during execution initialization.
	// This does not perform lookup here, select authentication, or grant secret
	// or network authority to Query request construction or Semantics.
	bool secret_manager;

	// Classifies only relational metadata and cancellation behavior. Secret
	// Manager availability is independently validated by Semantics when a
	// selected relation requires a logical reference.
	bool HasConservativeRelationalProfile() const;
};

// Protocol-neutral input from Query Experience to Relational Semantics.
// Construction is deterministic, side-effect free, and grants no I/O
// authority. DuckDB retains every unavailable relational operation.
struct ScanRequest {
	std::string connector_name;
	std::string relation_name;
	std::vector<std::string> explicit_inputs;
	std::vector<std::string> projected_columns;
	std::string predicate;
	std::vector<std::string> orderings;
	bool has_limit;
	bool has_offset;
	AdapterCapabilities capabilities;
	LogicalSecretReference secret_reference;

	std::string Snapshot() const;
};

// Builds Query's protocol-neutral handoff for one exact relation. Selection is
// case-sensitive and never falls back. The builder copies only relation
// identity and the complete declared schema, records the supported native
// execution-initialization Secret Manager capability, and validates only
// required-versus-absent logical-reference presence. It does not inspect the
// Connector logical credential/binding policy, call DuckDB, resolve a secret,
// read environment/files, perform I/O, or plan relational/auth behavior.
ScanRequest BuildConservativeScanRequest(const CompiledConnector &connector, const std::string &relation_name,
                                         LogicalSecretReference secret_reference);

} // namespace duckdb_api
