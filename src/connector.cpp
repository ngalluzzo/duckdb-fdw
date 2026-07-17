#include "duckdb_api/connector.hpp"

#include <utility>

namespace duckdb_api {

namespace {

std::string SchemaSnapshot(const std::vector<CompiledColumn> &columns) {
	std::string result;
	for (std::size_t index = 0; index < columns.size(); index++) {
		if (index > 0) {
			result += ',';
		}
		const auto &column = columns[index];
		result += column.name + ':' + column.logical_type + (column.nullable ? "?" : "!") + ':' + column.extractor;
	}
	return result;
}

} // namespace

std::string CompiledConnector::Snapshot() const {
	return "connector=" + connector_name + ";version=" + version + ";relation=" + relation_name +
	       ";schema=" + SchemaSnapshot(columns) + ";operation=" + operation_name + ":fallback:many:REST:" + method +
	       ':' + path + ':' + extractor + ";fixture=" + fixture_digest;
}

CompiledConnector BuildCompiledConnector(const std::string &fixture_digest) {
	CompiledConnector result;
	result.connector_name = "example";
	result.version = "0.1.0";
	result.relation_name = "items";
	result.columns = {{"id", "BIGINT", false, "$.id"},
	                  {"name", "VARCHAR", false, "$.name"},
	                  {"active", "BOOLEAN", false, "$.active"}};
	result.operation_name = "items_list";
	result.method = "GET";
	result.path = "/items";
	result.extractor = "$.items[*]";
	result.fixture_digest = fixture_digest;
	return result;
}

} // namespace duckdb_api
