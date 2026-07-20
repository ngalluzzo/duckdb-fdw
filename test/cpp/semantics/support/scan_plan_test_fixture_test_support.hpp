#ifndef DUCKDB_API_TEST_SCAN_PLAN_TEST_FIXTURE_TEST_SUPPORT_HPP
#define DUCKDB_API_TEST_SCAN_PLAN_TEST_FIXTURE_TEST_SUPPORT_HPP

#include "duckdb_api/scan_plan.hpp"

#include <string>

namespace duckdb_api_test {
namespace scan_plan_fixture_contract {

void RequireCanaryAbsent(const duckdb_api::ScanPlan &plan, const std::string &canary);

void TestValidFactoriesUsePublicPlanningPath();
void TestOperationCounterexamples(const std::string &canary);
void TestAuthenticationCounterexamples(const std::string &canary);
void TestResponseCounterexamples(const std::string &canary);
void TestNetworkCounterexamples(const std::string &canary);
void TestFeatureCounterexamples(const std::string &canary);
void TestResourceCounterexamples(const std::string &canary);
void TestRestQueryPathFixture(const std::string &canary);
void TestSafeConsumerHeaderBoundary();

} // namespace scan_plan_fixture_contract
} // namespace duckdb_api_test

#endif
