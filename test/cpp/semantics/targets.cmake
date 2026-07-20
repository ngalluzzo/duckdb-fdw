# Relational Semantics owns this non-installable fixture service. Focused
# Runtime tests consume its ScanPlan factories without compiling or importing
# Connector, Query, or planner internals.
add_library(
  duckdb_api_semantics_fixture_service STATIC
  ${RELATIONAL_PLAN_TEST_SERVICE_SOURCES}
  ${RELATIONAL_PLAN_PAGINATION_TEST_SERVICE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_semantics_fixture_service)
target_include_directories(
  duckdb_api_semantics_fixture_service
  PUBLIC test/cpp)
target_link_libraries(
  duckdb_api_semantics_fixture_service
  PUBLIC duckdb_api_scan_plan_service)

add_executable(
  duckdb_api_scan_planner_tests
  test/cpp/semantics/scan_planner_tests.cpp
  ${RELATIONAL_PREDICATE_PLANNER_TEST_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_scan_planner_tests)
target_include_directories(
  duckdb_api_scan_planner_tests
  PRIVATE test/cpp src/semantics)
target_link_libraries(
  duckdb_api_scan_planner_tests
  PRIVATE duckdb_api_semantics_fixture_service
          duckdb_api_connector_fixture_service
          duckdb_api_package_generation_fixture_service
          duckdb_api_relational_planning_service
          dummy_static_extension_loader
          duckdb_static)

add_executable(
  duckdb_api_scan_plan_contract_tests
  test/cpp/semantics/scan_plan_contract_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_scan_plan_contract_tests)
target_include_directories(duckdb_api_scan_plan_contract_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_scan_plan_contract_tests
  PRIVATE duckdb_api_connector_fixture_service
          duckdb_api_relational_planning_service)

add_executable(
  duckdb_api_scan_plan_pagination_contract_tests
  test/cpp/semantics/scan_plan_pagination_contract_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_scan_plan_pagination_contract_tests)
target_include_directories(
  duckdb_api_scan_plan_pagination_contract_tests
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_scan_plan_pagination_contract_tests
  PRIVATE duckdb_api_semantics_fixture_service
          duckdb_api_connector_fixture_service
          duckdb_api_relational_planning_service)

add_executable(
  duckdb_api_scan_plan_fixture_tests
  test/cpp/semantics/scan_plan_test_fixtures_tests.cpp
  ${RELATIONAL_PLAN_TEST_CONTRACT_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_scan_plan_fixture_tests)
target_include_directories(duckdb_api_scan_plan_fixture_tests PRIVATE test/cpp)
target_compile_definitions(
  duckdb_api_scan_plan_fixture_tests
  PRIVATE DUCKDB_API_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
  duckdb_api_scan_plan_fixture_tests
  PRIVATE duckdb_api_semantics_fixture_service
          duckdb_api_connector_fixture_service
          duckdb_api_relational_planning_service)

# Closed GraphQL planner oracle. It consumes Connector's public fixture service
# and Semantics' public planning/fixture services without compiling provider
# production sources or importing provider-private construction.
add_executable(
  duckdb_api_graphql_semantics_tests
  ${GRAPHQL_SEMANTICS_TEST_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_graphql_semantics_tests)
target_include_directories(duckdb_api_graphql_semantics_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_graphql_semantics_tests
  PRIVATE duckdb_api_semantics_fixture_service
          duckdb_api_connector_fixture_service
          duckdb_api_content_digest_service
          duckdb_api_relational_planning_service)
