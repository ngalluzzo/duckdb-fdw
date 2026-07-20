#pragma once

#include "duckdb_api/scan_plan.hpp"

namespace duckdb_api_test {

// Semantics-only inspection of test construction. Runtime consumers must prove
// rejection through PlannedProtocolOperation's public exhaustive accessors.
struct GraphqlProtocolEnvelopeShape {
	duckdb_api::PlannedProtocol protocol;
	bool rest_present;
	bool graphql_present;
};

GraphqlProtocolEnvelopeShape InspectGraphqlProtocolEnvelope(const duckdb_api::ScanPlan &plan);

} // namespace duckdb_api_test
