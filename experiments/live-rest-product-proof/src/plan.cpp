#include "live_rest/plan.hpp"

#include <sstream>
#include <stdexcept>

namespace live_rest {

namespace {

const char *const PUBLIC_AUTHORITY = "https://api.github.com";
const char *const LOOPBACK_PREFIX = "http://127.0.0.1:";
const char *const REQUEST_TARGET = "/search/users?q=duckdb+in%3Alogin&per_page=3";

const char *ColumnTypeName(ColumnType type) {
	switch (type) {
	case ColumnType::BIGINT:
		return "BIGINT";
	case ColumnType::VARCHAR:
		return "VARCHAR";
	case ColumnType::BOOLEAN:
		return "BOOLEAN";
	}
	throw std::logic_error("live REST plan contains an unknown column type");
}

bool IsDecimalDigit(char value) {
	return value >= '0' && value <= '9';
}

bool IsValidLoopbackAuthority(const std::string &authority) {
	const std::string prefix(LOOPBACK_PREFIX);
	if (authority.compare(0, prefix.size(), prefix) != 0) {
		return false;
	}

	const std::string port_text = authority.substr(prefix.size());
	if (port_text.empty() || port_text.size() > 5) {
		return false;
	}
	if (port_text.size() > 1 && port_text[0] == '0') {
		return false;
	}

	uint32_t port = 0;
	for (std::size_t index = 0; index < port_text.size(); index++) {
		if (!IsDecimalDigit(port_text[index])) {
			return false;
		}
		port = port * 10 + static_cast<uint32_t>(port_text[index] - '0');
	}
	return port >= 1 && port <= 65535;
}

void ValidateAuthority(const std::string &authority) {
	if (authority != PUBLIC_AUTHORITY && !IsValidLoopbackAuthority(authority)) {
		throw std::invalid_argument("live REST plan rejected unsupported base authority");
	}
}

} // namespace

std::string LiveScanPlan::Snapshot() const {
	std::ostringstream result;
	result << "connector=" << connector_name << ";relation=" << relation_name << ";schema=";
	for (std::size_t index = 0; index < columns.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << columns[index].name << ':' << ColumnTypeName(columns[index].type) << "!:$."
		       << columns[index].json_field;
	}
	result << ";method=" << method << ";url=" << url << ";response_array=" << response_array_field
	       << ";remote_predicate=TRUE;runtime_residual=TRUE;duckdb_owns=filter,ordering,limit,offset"
	       << ";redirects=" << (redirects_enabled ? "enabled" : "disabled")
	       << ";retries=" << (retries_enabled ? "enabled" : "disabled")
	       << ";authentication=" << (authentication_enabled ? "enabled" : "disabled")
	       << ";pagination=" << (pagination_enabled ? "enabled" : "disabled")
	       << ";budgets=response_bytes:" << max_response_bytes << ",records:" << max_records
	       << ",string_bytes:" << max_string_bytes << ",wall_ms:" << wall_milliseconds
	       << ",batch_rows:" << batch_rows;
	return result.str();
}

LiveScanPlan BuildLiveScanPlan(const std::string &authority) {
	ValidateAuthority(authority);

	LiveScanPlan result;
	result.connector_name = "github";
	result.relation_name = "users";
	result.method = "GET";
	result.url = authority + REQUEST_TARGET;
	result.response_array_field = "items";
	result.columns = {{"id", ColumnType::BIGINT, "id"},
	                  {"login", ColumnType::VARCHAR, "login"},
	                  {"site_admin", ColumnType::BOOLEAN, "site_admin"}};
	result.max_response_bytes = 65536;
	result.max_records = 3;
	result.max_string_bytes = 256;
	result.wall_milliseconds = 5000;
	result.batch_rows = 2;
	result.redirects_enabled = false;
	result.retries_enabled = false;
	result.authentication_enabled = false;
	result.pagination_enabled = false;
	return result;
}

} // namespace live_rest
