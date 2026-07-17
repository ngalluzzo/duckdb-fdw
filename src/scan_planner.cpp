#include "duckdb_api/scan_plan.hpp"

#include <sstream>
#include <stdexcept>

namespace duckdb_api {

namespace {

bool IsAcceptedColumn(const CompiledColumn &column, const std::string &name, const std::string &logical_type,
                      const std::string &extractor) {
	return column.name == name && column.logical_type == logical_type && !column.nullable &&
	       column.extractor == extractor;
}

bool IsAcceptedSchema(const std::vector<CompiledColumn> &columns) {
	return columns.size() == 3 && IsAcceptedColumn(columns[0], "id", "BIGINT", "$.id") &&
	       IsAcceptedColumn(columns[1], "name", "VARCHAR", "$.name") &&
	       IsAcceptedColumn(columns[2], "active", "BOOLEAN", "$.active");
}

} // namespace

bool ResourceBudgets::IsPreviewBudget() const {
	return fixture_bytes == MAX_FIXTURE_BYTES && decoded_records == MAX_DECODED_RECORDS &&
	       name_bytes == MAX_NAME_BYTES && json_nesting == MAX_JSON_NESTING && batch_rows == OUTPUT_BATCH_ROWS &&
	       wall_milliseconds == MAX_EXECUTION_MILLISECONDS && concurrency == 1;
}

std::string ScanPlan::Snapshot() const {
	std::ostringstream result;
	result << "operation=" << operation_name << ";executor=" << executor_name << ";method=" << method
	       << ";path=" << path << ";extractor=" << extractor << ";fixture=" << fixture_digest << ";projection=";
	for (std::size_t index = 0; index < output_columns.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << output_columns[index];
	}
	result << ";remote_predicate=" << remote_predicate << ";runtime_residual=" << runtime_residual_predicate
	       << ";duckdb_owns=";
	for (std::size_t index = 0; index < duckdb_owned_operations.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << duckdb_owned_operations[index];
	}
	result << ";remote_ordering=" << (remote_ordering.empty() ? "[]" : "unexpected")
	       << ";runtime_ordering=" << (runtime_ordering.empty() ? "[]" : "unexpected")
	       << ";remote_limit=" << (has_remote_limit ? "set" : "unset")
	       << ";remote_offset=" << (has_remote_offset ? "set" : "unset")
	       << ";runtime_limit=" << (has_runtime_limit ? "set" : "unset")
	       << ";runtime_offset=" << (has_runtime_offset ? "set" : "unset")
	       << ";pagination=" << (pagination_enabled ? "enabled" : "disabled")
	       << ";providers=" << (providers_enabled ? "enabled" : "disabled")
	       << ";retry=" << (retry_enabled ? "enabled" : "disabled")
	       << ";cache=" << (cache_enabled ? "enabled" : "disabled")
	       << ";network=" << (network_enabled ? "enabled" : "disabled")
	       << ";budgets=fixture_bytes:" << budgets.fixture_bytes << ",records:" << budgets.decoded_records
	       << ",name_bytes:" << budgets.name_bytes << ",json_nesting:" << budgets.json_nesting
	       << ",batch_rows:" << budgets.batch_rows << ",wall_ms:" << budgets.wall_milliseconds
	       << ",concurrency:" << budgets.concurrency;
	return result.str();
}

ScanPlan BuildConservativeScanPlan(const CompiledConnector &connector, const ScanRequest &request) {
	const std::vector<std::string> expected_projection = {"id", "name", "active"};
	if (connector.connector_name != "example" || connector.version != "0.1.0" || connector.relation_name != "items" ||
	    !IsAcceptedSchema(connector.columns) || connector.operation_name != "items_list" || connector.method != "GET" ||
	    connector.path != "/items" || connector.extractor != "$.items[*]") {
		throw std::logic_error("native preview received unsupported compiled connector metadata");
	}
	if (request.connector_name != connector.connector_name || request.relation_name != connector.relation_name ||
	    !request.explicit_inputs.empty() || request.projected_columns != expected_projection ||
	    request.predicate != "TRUE" || !request.orderings.empty() || request.has_limit || request.has_offset ||
	    !request.capabilities.IsConservativePreview()) {
		throw std::logic_error("native preview received a non-conservative scan request");
	}
	ScanPlan result;
	result.operation_name = connector.operation_name;
	result.executor_name = "fixture_rest";
	result.method = connector.method;
	result.path = connector.path;
	result.extractor = connector.extractor;
	result.fixture_digest = connector.fixture_digest;
	result.output_columns = request.projected_columns;
	result.remote_predicate = "TRUE";
	result.runtime_residual_predicate = "TRUE";
	result.has_remote_limit = false;
	result.has_remote_offset = false;
	result.has_runtime_limit = false;
	result.has_runtime_offset = false;
	result.duckdb_owned_operations = {"filter", "ordering", "limit", "offset"};
	result.pagination_enabled = false;
	result.providers_enabled = false;
	result.retry_enabled = false;
	result.cache_enabled = false;
	result.network_enabled = false;
	result.budgets = {MAX_FIXTURE_BYTES,
	                  MAX_DECODED_RECORDS,
	                  MAX_NAME_BYTES,
	                  MAX_JSON_NESTING,
	                  OUTPUT_BATCH_ROWS,
	                  MAX_EXECUTION_MILLISECONDS,
	                  1};
	return result;
}

} // namespace duckdb_api
