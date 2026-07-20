#pragma once

#include "duckdb_api/scan_plan.hpp"

#include <string>

namespace duckdb_api_test {

// Closed Semantics provider API for focused Runtime consumers. Factories
// expose immutable plan values, never arbitrary builders, Connector catalogs,
// ScanRequest values, Runtime types, or credential bytes.
enum class GraphqlPlanCounterexample {
	OTHER_DOCUMENT_IDENTITY,
	OTHER_DOCUMENT_DIGEST,
	OTHER_DOMAIN,
	UNKNOWN_REPLAY_SAFETY,
	OTHER_CURSOR_PAGE_SIZE,
	ZERO_PAGE_BODY_BUDGET,
	RUNTIME_ORDERING_DELEGATED,
	PRIMARY_LANGUAGE_REQUIRED
};

duckdb_api::ScanPlan BuildValidGraphqlScanPlanFixture(const std::string &exact_logical_secret_name);
duckdb_api::ScanPlan BuildGraphqlPlanCounterexample(const std::string &exact_logical_secret_name,
                                                    GraphqlPlanCounterexample counterexample);

} // namespace duckdb_api_test
