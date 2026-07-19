#pragma once

#include "support/scan_plan_test_fixtures.hpp"

namespace duckdb_api_test {

// Implementation-only friend of ScanPlan. Runtime consumers must never include
// this header; the safe fixture header exposes only closed factories. Every
// method below applies one named invalid state and accepts no arbitrary value.
class ScanPlanTestAccess {
public:
	static duckdb_api::ScanPlan Operation(duckdb_api::ScanPlan plan, OperationPlanCounterexample counterexample);
	static duckdb_api::ScanPlan Authenticated(duckdb_api::ScanPlan plan,
	                                          AuthenticatedPlanCounterexample counterexample);
	static duckdb_api::ScanPlan AnonymousAuth(duckdb_api::ScanPlan plan,
	                                          AnonymousAuthPlanCounterexample counterexample);
	static duckdb_api::ScanPlan AnonymousSecretReference(duckdb_api::ScanPlan plan,
	                                                     const std::string &exact_logical_secret_name);
	static duckdb_api::ScanPlan Response(duckdb_api::ScanPlan plan, ResponsePlanCounterexample counterexample);
	static duckdb_api::ScanPlan Network(duckdb_api::ScanPlan plan, NetworkPlanCounterexample counterexample);
	static duckdb_api::ScanPlan Feature(duckdb_api::ScanPlan plan, FeaturePlanCounterexample counterexample);
	static duckdb_api::ScanPlan Pagination(duckdb_api::ScanPlan plan, PaginationPlanCounterexample counterexample);
	static duckdb_api::ScanPlan Resource(duckdb_api::ScanPlan plan, ResourcePlanCounterexample counterexample);
};

} // namespace duckdb_api_test
