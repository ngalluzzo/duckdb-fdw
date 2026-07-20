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

# Connector-owned package generation fixtures are a separate provider service.
# Future Semantics tests link this target and cannot reach the private compiler
# builder or Connector implementation sources through its public includes.
add_library(
  duckdb_api_package_generation_fixture_service STATIC
  ${CONNECTOR_PACKAGE_TEST_SERVICE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_package_generation_fixture_service)
target_include_directories(
  duckdb_api_package_generation_fixture_service
  PUBLIC test/cpp)
target_link_libraries(
  duckdb_api_package_generation_fixture_service
  PUBLIC duckdb_api_connector_metadata_service)

# Query consumers use the real repository package compiler through this
# Connector-owned bounded fixture API. The public surface exposes only the
# registration projection and its opaque generation handle.
add_library(
  duckdb_api_package_compiler_fixture_service STATIC
  ${CONNECTOR_PACKAGE_COMPILER_TEST_SERVICE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_package_compiler_fixture_service)
target_include_directories(
  duckdb_api_package_compiler_fixture_service
  PUBLIC test/cpp)
target_link_libraries(
  duckdb_api_package_compiler_fixture_service
  PUBLIC duckdb_api_connector_metadata_service
  PRIVATE duckdb_api_package_compiler_service)

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
  duckdb_api_compiled_package_generation_tests
  test/cpp/connector/compiled_package_generation_contract_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_compiled_package_generation_tests)
target_include_directories(
  duckdb_api_compiled_package_generation_tests
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_compiled_package_generation_tests
  PRIVATE duckdb_api_connector_metadata_service)

add_executable(
  duckdb_api_package_compatibility_tests
  test/cpp/connector/package_compatibility_contract_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_package_compatibility_tests)
target_include_directories(
  duckdb_api_package_compatibility_tests
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_package_compatibility_tests
  PRIVATE duckdb_api_package_generation_fixture_service)

add_executable(
  duckdb_api_package_compiler_fixture_tests
  test/cpp/connector/package/package_compiler_fixture_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_package_compiler_fixture_tests)
target_include_directories(
  duckdb_api_package_compiler_fixture_tests
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_package_compiler_fixture_tests
  PRIVATE duckdb_api_package_compiler_fixture_service)

add_executable(
  duckdb_api_failsafe_yaml_tests
  test/cpp/connector/package/failsafe_yaml_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_failsafe_yaml_tests)
target_include_directories(
  duckdb_api_failsafe_yaml_tests
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_failsafe_yaml_tests
  PRIVATE duckdb_api_package_source_service)

add_executable(
  duckdb_api_package_digest_tests
  test/cpp/connector/package/package_digest_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_package_digest_tests)
target_include_directories(
  duckdb_api_package_digest_tests
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_package_digest_tests
  PRIVATE duckdb_api_package_source_service)

add_executable(
  duckdb_api_package_source_tests
  test/cpp/connector/package/package_source_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_package_source_tests)
target_include_directories(
  duckdb_api_package_source_tests
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_package_source_tests
  PRIVATE duckdb_api_package_source_service)

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

foreach(package_test
    package_compiler_contract
    package_graphql_renderer
    package_predicate_compiler
    package_schema_contract)
  add_executable(
    duckdb_api_${package_test}_tests
    test/cpp/connector/package/${package_test}_tests.cpp)
  configure_duckdb_api_cpp_target(duckdb_api_${package_test}_tests)
  target_include_directories(
    duckdb_api_${package_test}_tests
    PRIVATE test/cpp)
  target_link_libraries(
    duckdb_api_${package_test}_tests
    PRIVATE duckdb_api_package_compiler_service)
endforeach()
