#pragma once

#include "duckdb_api/scan_plan.hpp"
#include "semantics/support/graphql_scan_plan_test_fixtures.hpp"

#include <string>

namespace duckdb_api_test {
namespace graphql_semantics {

duckdb_api::ScanPlan BuildProductionPlan();
duckdb_api::ScanPlan BuildProductionPlan(GraphqlLocalResidualProfile profile);
void TestOperationPlan();
void TestBaseDomain();
void TestCursorResources();
void TestNullability();
void TestFixtureBoundary();
void TestPackageGraphqlPlanning(const std::string &absolute_repository_root);

} // namespace graphql_semantics
} // namespace duckdb_api_test
