# Connector's fixture service is a bounded test API. Other teams may link it;
# only Connector tests may include its private construction access.
add_library(
  duckdb_api_connector_fixture_service STATIC
  ${CONNECTOR_TEST_SERVICE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_connector_fixture_service)
target_include_directories(
  duckdb_api_connector_fixture_service
  PUBLIC test/cpp)
target_link_libraries(
  duckdb_api_connector_fixture_service
  PUBLIC duckdb_api_connector_metadata_service)

add_executable(
  duckdb_api_connector_tests
  test/cpp/connector/connector_contract_tests.cpp
  test/cpp/connector/connector_catalog_contract_tests.cpp
  test/cpp/connector/connector_graphql_contract_tests.cpp
  test/cpp/connector/connector_pagination_contract_tests.cpp
  test/cpp/connector/connector_predicate_contract_tests.cpp
  test/cpp/connector/connector_predicate_proof_contract_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_connector_tests)
target_include_directories(duckdb_api_connector_tests PRIVATE test/cpp)
target_compile_definitions(
  duckdb_api_connector_tests
  PRIVATE DUCKDB_API_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
  duckdb_api_connector_tests
  PRIVATE duckdb_api_connector_metadata_service)

add_executable(
  duckdb_api_connector_catalog_fixture_tests
  test/cpp/connector/connector_catalog_test_fixtures_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_connector_catalog_fixture_tests)
target_include_directories(
  duckdb_api_connector_catalog_fixture_tests
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_connector_catalog_fixture_tests
  PRIVATE duckdb_api_connector_fixture_service)
