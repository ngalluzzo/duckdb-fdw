add_executable(
  duckdb_api_execution_contract_tests
  test/cpp/runtime/api/execution_contract_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_execution_contract_tests)
target_include_directories(duckdb_api_execution_contract_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_execution_contract_tests
  PRIVATE duckdb_api_runtime_interface_service)

add_executable(
  duckdb_api_authorization_contract_tests
  test/cpp/runtime/api/authorization_contract_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_authorization_contract_tests)
target_include_directories(duckdb_api_authorization_contract_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_authorization_contract_tests
  PRIVATE duckdb_api_runtime_interface_service
          duckdb_api_semantics_fixture_service)

add_executable(
  duckdb_api_network_policy_tests
  test/cpp/runtime/policy/network_policy_tests.cpp
  src/runtime/policy/network_policy.cpp)
configure_duckdb_api_cpp_target(duckdb_api_network_policy_tests)
target_include_directories(duckdb_api_network_policy_tests PRIVATE test/cpp)

add_executable(
  duckdb_api_uri_reference_tests
  test/cpp/runtime/pagination/uri_reference_tests.cpp
  src/runtime/pagination/uri_reference.cpp)
configure_duckdb_api_cpp_target(duckdb_api_uri_reference_tests)
target_include_directories(duckdb_api_uri_reference_tests PRIVATE test/cpp)

add_executable(
  duckdb_api_link_pagination_tests
  test/cpp/runtime/pagination/link_pagination_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_link_pagination_tests)
target_include_directories(duckdb_api_link_pagination_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_link_pagination_tests
  PRIVATE duckdb_api_runtime_executor_service
          duckdb_api_semantics_fixture_service)

add_executable(
  duckdb_api_scan_resource_accounting_tests
  test/cpp/runtime/policy/scan_resource_accounting_tests.cpp
  src/runtime/policy/scan_resource_accounting.cpp)
configure_duckdb_api_cpp_target(duckdb_api_scan_resource_accounting_tests)
target_include_directories(duckdb_api_scan_resource_accounting_tests PRIVATE test/cpp)

add_executable(
  duckdb_api_request_validation_tests
  test/cpp/runtime/policy/request_validation_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_request_validation_tests)
target_include_directories(duckdb_api_request_validation_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_request_validation_tests
  PRIVATE duckdb_api_runtime_executor_service)

add_executable(
  duckdb_api_http_transport_contract_tests
  test/cpp/runtime/transport/http_transport_contract_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_http_transport_contract_tests)
target_include_directories(duckdb_api_http_transport_contract_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_http_transport_contract_tests
  PRIVATE duckdb_api_runtime_interface_service)

add_executable(
  duckdb_api_decoded_page_buffer_tests
  test/cpp/runtime/decoding/decoded_page_buffer_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_decoded_page_buffer_tests)
target_include_directories(duckdb_api_decoded_page_buffer_tests PRIVATE test/cpp)

add_executable(
  duckdb_api_json_decoder_tests
  test/cpp/runtime/decoding/json_decoder_tests.cpp
  src/runtime/decoding/json_decoder.cpp)
configure_duckdb_api_cpp_target(duckdb_api_json_decoder_tests)
target_include_directories(duckdb_api_json_decoder_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_json_decoder_tests
  PRIVATE duckdb_api_runtime_interface_service)

add_executable(
  duckdb_api_json_root_array_decoder_tests
  test/cpp/runtime/decoding/json_root_array_decoder_tests.cpp
  src/runtime/decoding/json_decoder.cpp)
configure_duckdb_api_cpp_target(duckdb_api_json_root_array_decoder_tests)
target_include_directories(duckdb_api_json_root_array_decoder_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_json_root_array_decoder_tests
  PRIVATE duckdb_api_runtime_interface_service)

add_executable(
  duckdb_api_graphql_response_decoder_tests
  test/cpp/runtime/decoding/graphql_response_decoder_tests.cpp
  src/runtime/decoding/graphql_response_decoder.cpp
  src/runtime/decoding/strict_json_reader.cpp
  src/runtime/execution/graphql_plan_admission.cpp
  src/runtime/policy/request_validation.cpp)
configure_duckdb_api_cpp_target(duckdb_api_graphql_response_decoder_tests)
target_include_directories(duckdb_api_graphql_response_decoder_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_graphql_response_decoder_tests
  PRIVATE duckdb_api_runtime_interface_service
          duckdb_api_scan_plan_service
          duckdb_api_content_digest_service
          duckdb_api_semantics_fixture_service)

add_executable(
  duckdb_api_graphql_cursor_pagination_tests
  test/cpp/runtime/pagination/graphql_cursor_pagination_tests.cpp
  src/runtime/pagination/graphql_cursor_pagination.cpp)
configure_duckdb_api_cpp_target(duckdb_api_graphql_cursor_pagination_tests)
target_include_directories(duckdb_api_graphql_cursor_pagination_tests PRIVATE test/cpp)

# Runtime-private programmable transport. Focused Runtime tests may author and
# inspect exact synthetic request/response bytes; no cross-team consumer links
# this target or receives its include surface.
add_library(
  duckdb_api_runtime_programmable_test_service STATIC
  test/cpp/runtime/support/controlled_http_transport.cpp)
configure_duckdb_api_cpp_target(duckdb_api_runtime_programmable_test_service)
target_include_directories(
  duckdb_api_runtime_programmable_test_service
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_runtime_programmable_test_service
  PRIVATE duckdb_api_runtime_executor_service
          Threads::Threads)

# The sole Query-facing Runtime test service. Only this dedicated include
# directory is public; its implementation selects Runtime-owned named scenarios
# and returns a public ScanExecutor plus safe counters/stages.
add_library(
  duckdb_api_runtime_controlled_service STATIC
  test/cpp/runtime/support/controlled_runtime_scenario.cpp)
configure_duckdb_api_cpp_target(duckdb_api_runtime_controlled_service)
target_include_directories(
  duckdb_api_runtime_controlled_service
  PUBLIC test/cpp/runtime/service
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_runtime_controlled_service
  PUBLIC duckdb_api_runtime_interface_service
  PRIVATE duckdb_api_runtime_programmable_test_service
          Threads::Threads)

add_executable(
  duckdb_api_controlled_runtime_scenario_tests
  test/cpp/runtime/execution/controlled_runtime_scenario_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_controlled_runtime_scenario_tests)
target_include_directories(duckdb_api_controlled_runtime_scenario_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_controlled_runtime_scenario_tests
  PRIVATE duckdb_api_runtime_controlled_service
          duckdb_api_semantics_fixture_service
          Threads::Threads)

function(add_duckdb_api_runtime_executor_test target test_source)
  add_executable(
    ${target}
    ${test_source}
    ${REMOTE_RUNTIME_EXECUTOR_TEST_SUPPORT_SOURCES})
  configure_duckdb_api_cpp_target(${target})
  target_include_directories(${target} PRIVATE test/cpp)
  target_link_libraries(
    ${target}
    PRIVATE duckdb_api_runtime_programmable_test_service
            duckdb_api_semantics_fixture_service
            Threads::Threads)
endfunction()

add_duckdb_api_runtime_executor_test(
  duckdb_api_http_scan_executor_tests
  test/cpp/runtime/execution/http_scan_executor_tests.cpp)
add_duckdb_api_runtime_executor_test(
  duckdb_api_http_scan_pagination_tests
  test/cpp/runtime/execution/http_scan_pagination_tests.cpp)
target_link_libraries(
  duckdb_api_http_scan_pagination_tests
  PRIVATE duckdb_api_semantics_materialized_fixture_service)
add_duckdb_api_runtime_executor_test(
  duckdb_api_http_scan_executor_policy_tests
  test/cpp/runtime/execution/http_scan_executor_policy_tests.cpp)
add_executable(
  duckdb_api_rest_plan_admission_tests
  test/cpp/runtime/execution/rest_plan_admission_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_rest_plan_admission_tests)
target_include_directories(duckdb_api_rest_plan_admission_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_rest_plan_admission_tests
  PRIVATE duckdb_api_runtime_programmable_test_service
          duckdb_api_semantics_fixture_service
          duckdb_api_semantics_materialized_fixture_service)
add_duckdb_api_runtime_executor_test(
  duckdb_api_graphql_paginated_scan_tests
  test/cpp/runtime/execution/graphql_paginated_scan_tests.cpp)

add_executable(
  duckdb_api_graphql_plan_admission_tests
  test/cpp/runtime/execution/graphql_plan_admission_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_graphql_plan_admission_tests)
target_include_directories(duckdb_api_graphql_plan_admission_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_graphql_plan_admission_tests
  PRIVATE duckdb_api_runtime_executor_service
          duckdb_api_semantics_fixture_service)

# Curl probes require a test-only observer compiled into Runtime. Keeping that
# variant here prevents the observer from entering a provider or product target.
add_library(
  duckdb_api_runtime_private_curl_service STATIC
  ${REMOTE_RUNTIME_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_runtime_private_curl_service)
target_compile_definitions(
  duckdb_api_runtime_private_curl_service
  PRIVATE DUCKDB_API_PRIVATE_CURL_TESTS)
target_link_libraries(
  duckdb_api_runtime_private_curl_service
  PUBLIC duckdb_api_runtime_interface_service
         duckdb_api_scan_plan_service
         duckdb_api_content_digest_service
         CURL::libcurl
         Threads::Threads)

# Runtime-private real-curl loopback composition. Runtime transport targets use
# this bounded executor/socket-counter fixture; Query consumes named scenarios
# instead and never receives this target's broad support include directory.
add_library(
  duckdb_api_runtime_loopback_curl_test_service STATIC
  ${REMOTE_RUNTIME_LOOPBACK_PRODUCT_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_runtime_loopback_curl_test_service)
target_include_directories(
  duckdb_api_runtime_loopback_curl_test_service
  PRIVATE test/cpp)
target_compile_definitions(
  duckdb_api_runtime_loopback_curl_test_service
  PRIVATE DUCKDB_API_PRIVATE_CURL_TESTS)
target_link_libraries(
  duckdb_api_runtime_loopback_curl_test_service
  PRIVATE duckdb_api_runtime_private_curl_service
          CURL::libcurl
          Threads::Threads)

function(add_duckdb_api_private_curl_test target source)
  add_executable(
    ${target}
    ${source}
    ${REMOTE_RUNTIME_CURL_TEST_SUPPORT_SOURCES})
  configure_duckdb_api_cpp_target(${target})
  target_include_directories(${target} PRIVATE test/cpp)
  target_compile_definitions(${target} PRIVATE DUCKDB_API_PRIVATE_CURL_TESTS)
  target_link_libraries(
    ${target}
    PRIVATE duckdb_api_runtime_loopback_curl_test_service
            duckdb_api_semantics_fixture_service
            CURL::libcurl
            Threads::Threads)
endfunction()

add_duckdb_api_private_curl_test(
  duckdb_api_curl_http_transport_tests
  test/cpp/runtime/transport/curl_http_transport_tests.cpp)
add_duckdb_api_private_curl_test(
  duckdb_api_curl_http_budget_tests
  test/cpp/runtime/transport/curl_http_budget_tests.cpp)
add_duckdb_api_private_curl_test(
  duckdb_api_curl_http_lifecycle_tests
  test/cpp/runtime/transport/curl_http_lifecycle_tests.cpp)
add_duckdb_api_private_curl_test(
  duckdb_api_curl_transfer_policy_tests
  test/cpp/runtime/transport/curl_transfer_policy_tests.cpp)
add_duckdb_api_private_curl_test(
  duckdb_api_curl_link_metadata_tests
  test/cpp/runtime/transport/curl_link_metadata_tests.cpp)
add_duckdb_api_private_curl_test(
  duckdb_api_curl_http_pagination_tests
  test/cpp/runtime/transport/curl_http_pagination_tests.cpp)

add_executable(
  duckdb_api_curl_tls_security_tests
  test/cpp/runtime/transport/curl_tls_security_tests.cpp
  test/cpp/runtime/support/runtime_http_test_support.cpp
  test/cpp/runtime/support/private_curl_probe.cpp)
configure_duckdb_api_cpp_target(duckdb_api_curl_tls_security_tests)
target_include_directories(duckdb_api_curl_tls_security_tests PRIVATE test/cpp)
target_compile_definitions(
  duckdb_api_curl_tls_security_tests PRIVATE DUCKDB_API_PRIVATE_CURL_TESTS)
target_link_libraries(
  duckdb_api_curl_tls_security_tests
  PRIVATE duckdb_api_runtime_loopback_curl_test_service
          duckdb_api_semantics_fixture_service
          CURL::libcurl
          Threads::Threads)
