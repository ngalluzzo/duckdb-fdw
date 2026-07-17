#pragma once

#include <string>
#include <vector>

namespace duckdb_api {

// One output column in the repository-owned 0.1.0 example connector. This is
// private preview metadata, not a public connector-authoring or package ABI.
// The extractor is evaluated relative to one item selected by the relation's
// response extractor.
struct CompiledColumn {
	std::string name;
	std::string logical_type;
	bool nullable;
	std::string extractor;
};

// Immutable planning metadata for the internal example.items relation. Build
// this value once during example composition and retain it by value for active
// scans; consumers may rely on identifiers, schema, operation metadata, and
// fixture provenance remaining stable for the scan lifetime.
struct CompiledConnector {
	std::string connector_name;
	std::string version;
	std::string relation_name;
	std::vector<CompiledColumn> columns;
	std::string operation_name;
	std::string method;
	std::string path;
	std::string extractor;
	std::string fixture_digest;

	// Produces the tracked, source-explainable preview snapshot. `!` after a
	// logical type means the column is non-nullable.
	std::string Snapshot() const;
};

CompiledConnector BuildCompiledConnector(const std::string &fixture_digest);

} // namespace duckdb_api
