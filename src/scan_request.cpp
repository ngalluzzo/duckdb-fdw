#include "duckdb_api/scan_request.hpp"

#include <sstream>

namespace duckdb_api {

bool AdapterCapabilities::IsConservativePreview() const {
	return !projection && !filter && !ordering && !limit && !offset && !progress && cancellation && !secrets;
}

std::string ScanRequest::Snapshot() const {
	std::ostringstream result;
	result << "connector=" << connector_name << ";relation=" << relation_name
	       << ";inputs=" << (explicit_inputs.empty() ? "[]" : "unexpected") << ";projection=";
	for (std::size_t index = 0; index < projected_columns.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << projected_columns[index];
	}
	result << ";predicate=" << predicate << ";ordering=" << (orderings.empty() ? "[]" : "unexpected")
	       << ";limit=" << (has_limit ? "set" : "unset") << ";offset=" << (has_offset ? "set" : "unset")
	       << ";capabilities=projection:" << (capabilities.projection ? "available" : "unavailable")
	       << ",filter:" << (capabilities.filter ? "available" : "unavailable")
	       << ",ordering:" << (capabilities.ordering ? "available" : "unavailable")
	       << ",limit:" << (capabilities.limit ? "available" : "unavailable")
	       << ",offset:" << (capabilities.offset ? "available" : "unavailable")
	       << ",progress:" << (capabilities.progress ? "available" : "unavailable")
	       << ",cancellation:" << (capabilities.cancellation ? "verified" : "unavailable")
	       << ",secrets:" << (capabilities.secrets ? "available" : "unavailable");
	return result.str();
}

ScanRequest BuildConservativeScanRequest() {
	ScanRequest result;
	result.connector_name = "example";
	result.relation_name = "items";
	result.explicit_inputs.clear();
	result.projected_columns = {"id", "name", "active"};
	result.predicate = "TRUE";
	result.has_limit = false;
	result.has_offset = false;
	result.capabilities = {false, false, false, false, false, false, true, false};
	return result;
}

} // namespace duckdb_api
