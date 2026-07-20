# Relational Semantics owns the ScanPlan fixture service consumed by focused
# Runtime tests. Consumers link this boundary instead of compiling provider
# internals into their targets.
set(RELATIONAL_PLAN_TEST_SERVICE_SOURCES
    test/cpp/semantics/support/scan_plan_test_fixtures.cpp
    test/cpp/semantics/support/scan_plan_operation_test_fixtures.cpp
    test/cpp/semantics/support/scan_plan_auth_test_fixtures.cpp
    test/cpp/semantics/support/scan_plan_response_test_fixtures.cpp
    test/cpp/semantics/support/scan_plan_network_test_fixtures.cpp
    test/cpp/semantics/support/scan_plan_feature_test_fixtures.cpp
    test/cpp/semantics/support/scan_plan_resource_test_fixtures.cpp
    test/cpp/semantics/support/scan_plan_repository_test_fixtures.cpp
    test/cpp/semantics/support/scan_plan_fixture_consumer_probe.cpp)
set(RELATIONAL_PLAN_PAGINATION_TEST_SERVICE_SOURCES
    test/cpp/semantics/support/scan_plan_pagination_test_fixtures.cpp)
set(RELATIONAL_PREDICATE_PLANNER_TEST_SOURCES
    test/cpp/semantics/relational_predicate_tests.cpp
    test/cpp/semantics/predicate_planner_tests.cpp
    test/cpp/semantics/predicate_composition_law_tests.cpp)
set(RELATIONAL_PLAN_TEST_CONTRACT_SOURCES
    test/cpp/semantics/support/scan_plan_test_fixture_test_support.cpp
    test/cpp/semantics/scan_plan_operation_test_fixture_tests.cpp
    test/cpp/semantics/scan_plan_auth_test_fixture_tests.cpp
    test/cpp/semantics/scan_plan_response_test_fixture_tests.cpp
    test/cpp/semantics/scan_plan_network_test_fixture_tests.cpp
    test/cpp/semantics/scan_plan_feature_test_fixture_tests.cpp
    test/cpp/semantics/scan_plan_resource_test_fixture_tests.cpp
    test/cpp/semantics/scan_plan_fixture_consumer_boundary_tests.cpp)
