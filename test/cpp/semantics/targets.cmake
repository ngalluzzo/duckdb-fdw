# Closed fixture mutation shared by the broad and package-specific providers.
# Consumers cannot call this friend-only service; they receive only the safe
# factories exposed by those providers.
add_library(
  duckdb_api_semantics_protocol_replacement_fixture_service STATIC
  ${RELATIONAL_PROTOCOL_REPLACEMENT_TEST_SERVICE_SOURCES})
configure_duckdb_api_cpp_target(
  duckdb_api_semantics_protocol_replacement_fixture_service)
target_include_directories(
  duckdb_api_semantics_protocol_replacement_fixture_service
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_semantics_protocol_replacement_fixture_service
  PRIVATE duckdb_api_scan_plan_service)

# Real planner-produced positive plans live in a separate provider so the
# value-only Runtime fixture service below retains no Connector or Query
# dependency. Runtime consumers link this bounded Semantics API, never
# Connector-private construction or planner sources directly.
add_library(
  duckdb_api_semantics_materialized_fixture_service STATIC
  ${RELATIONAL_MATERIALIZED_PLAN_TEST_SERVICE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_semantics_materialized_fixture_service)
target_include_directories(
  duckdb_api_semantics_materialized_fixture_service
  PUBLIC test/cpp)
target_link_libraries(
  duckdb_api_semantics_materialized_fixture_service
  PUBLIC duckdb_api_relational_planning_service
         duckdb_api_package_generation_fixture_service)

# Real package GraphQL plans cross a narrower provider because their exact
# generation is compiled from repository evidence. Runtime consumers receive
# only ScanPlan and do not link Connector-private renderer or test access.
add_library(
  duckdb_api_semantics_package_graphql_fixture_service STATIC
  ${RELATIONAL_PACKAGE_GRAPHQL_PLAN_TEST_SERVICE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_semantics_package_graphql_fixture_service)
target_include_directories(
  duckdb_api_semantics_package_graphql_fixture_service
  PUBLIC test/cpp)
target_link_libraries(
  duckdb_api_semantics_package_graphql_fixture_service
  PRIVATE duckdb_api_package_compiler_fixture_service
          duckdb_api_package_bound_planning_service
          duckdb_api_content_digest_service
          duckdb_api_semantics_protocol_replacement_fixture_service)

# Non-installable Semantics observation boundary for package-fixture consumers.
# The provider accepts only immutable compiled facts and a typed ScanRequest,
# then invokes the production generation-bound planner. Its dependency graph
# intentionally contains no Runtime, transport, Connector source/compiler, or
# fixture-coverage service.
add_library(
  duckdb_api_semantics_input_resolution_observation_service STATIC
  ${RELATIONAL_INPUT_RESOLUTION_OBSERVATION_SERVICE_SOURCES})
configure_duckdb_api_cpp_target(
  duckdb_api_semantics_input_resolution_observation_service)
target_include_directories(
  duckdb_api_semantics_input_resolution_observation_service
  PUBLIC test/cpp
  PRIVATE src/semantics)
target_link_libraries(
  duckdb_api_semantics_input_resolution_observation_service
  PRIVATE duckdb_api_package_bound_planning_service)

# Focused provider oracle. Connector's typed generation fixture remains behind
# its own bounded service and supplies no YAML, source, or coverage-key facts.
add_executable(
  duckdb_api_semantics_input_resolution_observation_service_tests
  test/cpp/semantics/service/input_resolution_observation_service_tests.cpp)
configure_duckdb_api_cpp_target(
  duckdb_api_semantics_input_resolution_observation_service_tests)
target_include_directories(
  duckdb_api_semantics_input_resolution_observation_service_tests
  PRIVATE test/cpp)
target_compile_definitions(
  duckdb_api_semantics_input_resolution_observation_service_tests
  PRIVATE DUCKDB_API_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
  duckdb_api_semantics_input_resolution_observation_service_tests
  PRIVATE duckdb_api_semantics_input_resolution_observation_service
          duckdb_api_package_generation_fixture_service
          duckdb_api_package_compiler_fixture_service)

# Link-only Runtime-facing topology oracle. Its source includes only the
# bounded Semantics fixture header and the immutable plan contract.
add_executable(
  duckdb_api_repository_graphql_fixture_consumer_tests
  test/cpp/semantics/repository_graphql_scan_plan_fixture_consumer_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_repository_graphql_fixture_consumer_tests)
target_include_directories(
  duckdb_api_repository_graphql_fixture_consumer_tests
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_repository_graphql_fixture_consumer_tests
  PRIVATE duckdb_api_semantics_package_graphql_fixture_service)

# Link-only topology oracle for Runtime's bounded materialized-plan provider.
# It intentionally omits the broad friend-built fixture service so unresolved
# construction dependencies cannot hide behind aggregate Semantics targets.
add_executable(
  duckdb_api_runtime_rest_predicate_fixture_consumer_tests
  test/cpp/semantics/runtime_rest_predicate_plan_fixture_consumer_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_runtime_rest_predicate_fixture_consumer_tests)
target_include_directories(
  duckdb_api_runtime_rest_predicate_fixture_consumer_tests
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_runtime_rest_predicate_fixture_consumer_tests
  PRIVATE duckdb_api_semantics_materialized_fixture_service)

# Relational Semantics owns this non-installable plan-only fixture service.
# Focused Runtime tests consume its ScanPlan factories without compiling or
# importing Connector, Query, or planner internals.
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
  PUBLIC duckdb_api_scan_plan_service
  PRIVATE duckdb_api_semantics_protocol_replacement_fixture_service)

add_executable(
  duckdb_api_scan_planner_tests
  test/cpp/semantics/scan_planner_tests.cpp
  ${RELATIONAL_PREDICATE_PLANNER_TEST_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_scan_planner_tests)
target_include_directories(
  duckdb_api_scan_planner_tests
  PRIVATE test/cpp src/semantics)
target_compile_definitions(
  duckdb_api_scan_planner_tests
  PRIVATE DUCKDB_API_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
  duckdb_api_scan_planner_tests
  PRIVATE duckdb_api_semantics_fixture_service
          duckdb_api_semantics_materialized_fixture_service
          duckdb_api_connector_fixture_service
          duckdb_api_package_compiler_fixture_service
          duckdb_api_package_generation_fixture_service
          duckdb_api_package_bound_planning_service
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
  PRIVATE test/cpp src/semantics)
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
target_include_directories(duckdb_api_graphql_semantics_tests PRIVATE test/cpp src/semantics)
target_link_libraries(
  duckdb_api_graphql_semantics_tests
  PRIVATE duckdb_api_semantics_fixture_service
          duckdb_api_connector_fixture_service
          duckdb_api_package_compiler_fixture_service
          duckdb_api_package_bound_planning_service
          duckdb_api_semantics_package_graphql_fixture_service
          duckdb_api_content_digest_service
          duckdb_api_relational_planning_service)

# Closed REST planner oracle proving native/package plan parity for the three
# REST GitHub relations, mirroring duckdb_api_graphql_semantics_tests' native/
# package differential for the GraphQL relation.
add_executable(
  duckdb_api_package_rest_planning_tests
  test/cpp/semantics/package_rest_planning_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_package_rest_planning_tests)
target_include_directories(duckdb_api_package_rest_planning_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_package_rest_planning_tests
  PRIVATE duckdb_api_connector_fixture_service
          duckdb_api_package_compiler_fixture_service
          duckdb_api_package_bound_planning_service
          duckdb_api_semantics_package_graphql_fixture_service)
