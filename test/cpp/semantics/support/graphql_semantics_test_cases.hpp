#pragma once

#include "duckdb_api/scan_plan.hpp"

namespace duckdb_api_test {
namespace graphql_semantics {

duckdb_api::ScanPlan BuildProductionPlan();
void TestOperationPlan();
void TestBaseDomain();
void TestCursorResources();
void TestNullability();
void TestFixtureBoundary();

} // namespace graphql_semantics
} // namespace duckdb_api_test
