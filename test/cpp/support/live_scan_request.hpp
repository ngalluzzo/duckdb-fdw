#pragma once

#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_request.hpp"

namespace duckdb_api_test {

// Builds the conservative request supplied by the native DuckDB adapter. Test
// families share this helper so planner and immutable-plan oracles exercise
// the same Query-to-Semantics boundary without constructing ScanPlan fields.
inline duckdb_api::ScanRequest BuildLiveScanRequest(const duckdb_api::CompiledConnector &connector) {
	duckdb_api::ScanRequest result;
	result.connector_name = connector.connector_name;
	result.relation_name = connector.relation_name;
	for (const auto &column : connector.columns) {
		result.projected_columns.push_back(column.name);
	}
	result.predicate = "TRUE";
	result.has_limit = false;
	result.has_offset = false;
	result.capabilities = {false, false, false, false, false, false, true, false};
	return result;
}

} // namespace duckdb_api_test
