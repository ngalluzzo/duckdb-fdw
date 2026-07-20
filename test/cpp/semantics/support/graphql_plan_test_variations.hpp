#pragma once

#include "duckdb_api/scan_plan.hpp"

#include <string>

namespace duckdb_api_test {

// Semantics-internal equality probes. Each probe mutates one plan fact and need
// not describe a coherent executable plan. These are not rejection authority
// for Runtime consumers.
enum class GraphqlPlanVariation {
	OTHER_CONNECTOR_NAME,
	OTHER_CONNECTOR_VERSION,
	OTHER_RELATION_NAME,
	OTHER_SECRET_REFERENCE,
	OTHER_CLASSIFICATION_REASON,
	COUNT
};

duckdb_api::ScanPlan BuildGraphqlPlanVariation(const std::string &exact_logical_secret_name,
                                               GraphqlPlanVariation variation);

} // namespace duckdb_api_test
