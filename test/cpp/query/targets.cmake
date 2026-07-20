add_executable(
  duckdb_api_scan_request_tests
  test/cpp/query/scan_request_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_scan_request_tests)
target_include_directories(duckdb_api_scan_request_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_scan_request_tests
  PRIVATE duckdb_api_connector_fixture_service
          duckdb_api_query_request_service)

add_executable(
  duckdb_api_typed_value_adapter_tests
  test/cpp/query/duckdb/typed_value_adapter_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_typed_value_adapter_tests)
target_include_directories(duckdb_api_typed_value_adapter_tests PRIVATE test/cpp src)
target_link_libraries(
  duckdb_api_typed_value_adapter_tests
  PRIVATE duckdb_api_query_typed_value_adapter_service)

add_executable(
  duckdb_api_duckdb_secret_tests
  test/cpp/query/duckdb/duckdb_secret_tests.cpp
  test/cpp/query/duckdb/duckdb_secret_creation_tests.cpp
  test/cpp/query/duckdb/duckdb_secret_resolution_tests.cpp
  ${QUERY_SECRET_TEST_SUPPORT_SOURCES}
  ${QUERY_DUCKDB_SECRET_SOURCES}
  ${QUERY_DUCKDB_ADAPTER_SUPPORT_SOURCES}
  src/query/duckdb/table_function_adapter.cpp)
target_link_libraries(
  duckdb_api_duckdb_secret_tests
  PRIVATE duckdb_api_relational_planning_service
          duckdb_api_runtime_interface_service
          dummy_static_extension_loader
          duckdb_static
          Threads::Threads)
target_include_directories(duckdb_api_duckdb_secret_tests PRIVATE test/cpp)
configure_duckdb_api_cpp_target(duckdb_api_duckdb_secret_tests)

add_executable(
  duckdb_api_adapter_tests
  test/cpp/query/duckdb/duckdb_adapter_tests.cpp
  test/cpp/query/duckdb/duckdb_adapter_auth_bind_tests.cpp
  test/cpp/query/duckdb/duckdb_adapter_auth_lifecycle_tests.cpp
  test/cpp/query/duckdb/complex_filter_adapter_tests.cpp
  test/cpp/query/duckdb/predicate_candidate_translation_tests.cpp
  test/cpp/query/duckdb/table_function_plan_state_tests.cpp
  ${QUERY_DUCKDB_SECRET_SOURCES}
  ${QUERY_DUCKDB_ADAPTER_SUPPORT_SOURCES}
  src/query/duckdb/table_function_adapter.cpp
  ${QUERY_ADAPTER_TEST_SUPPORT_SOURCES}
  ${QUERY_AUTH_ADAPTER_TEST_SUPPORT_SOURCES})
target_link_libraries(
  duckdb_api_adapter_tests
  PRIVATE duckdb_api_connector_fixture_service
          duckdb_api_relational_planning_service
          duckdb_api_runtime_interface_service
          dummy_static_extension_loader
          duckdb_static
          Threads::Threads)
target_include_directories(duckdb_api_adapter_tests PRIVATE test/cpp src/query/duckdb)
configure_duckdb_api_cpp_target(duckdb_api_adapter_tests)

add_executable(
  duckdb_api_adapter_stream_contract_tests
  test/cpp/query/duckdb/duckdb_adapter_stream_contract_tests.cpp
  ${QUERY_DUCKDB_SECRET_SOURCES}
  ${QUERY_DUCKDB_ADAPTER_SUPPORT_SOURCES}
  src/query/duckdb/table_function_adapter.cpp
  ${QUERY_ADAPTER_TEST_SUPPORT_SOURCES})
target_link_libraries(
  duckdb_api_adapter_stream_contract_tests
  PRIVATE duckdb_api_relational_planning_service
          duckdb_api_runtime_interface_service
          dummy_static_extension_loader
          duckdb_static
          Threads::Threads)
target_include_directories(duckdb_api_adapter_stream_contract_tests PRIVATE test/cpp)
configure_duckdb_api_cpp_target(duckdb_api_adapter_stream_contract_tests)
