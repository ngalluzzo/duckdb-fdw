cmake_minimum_required(VERSION 3.10)

set(TARGET_NAME duckdb_api)
set(EXTENSION_NAME ${TARGET_NAME}_extension)

project(${TARGET_NAME})

if(NOT APPLE)
  message(FATAL_ERROR "duckdb_api 0.4.0 supports only the pinned macOS arm64 cell")
endif()

find_program(DUCKDB_API_XCRUN xcrun REQUIRED)
execute_process(
  COMMAND "${DUCKDB_API_XCRUN}" --sdk macosx --show-sdk-path
  RESULT_VARIABLE DUCKDB_API_XCRUN_RESULT
  OUTPUT_VARIABLE DUCKDB_API_XCRUN_SDK_ROOT
  ERROR_VARIABLE DUCKDB_API_XCRUN_ERROR
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT DUCKDB_API_XCRUN_RESULT EQUAL 0 OR DUCKDB_API_XCRUN_SDK_ROOT STREQUAL "")
  message(FATAL_ERROR "xcrun could not derive the macOS SDK: ${DUCKDB_API_XCRUN_ERROR}")
endif()
get_filename_component(DUCKDB_API_SDK_ROOT "${DUCKDB_API_XCRUN_SDK_ROOT}" REALPATH)
if(DEFINED DUCKDB_API_VERIFIED_SDK_ROOT)
  get_filename_component(
    DUCKDB_API_CALLER_SDK_ROOT "${DUCKDB_API_VERIFIED_SDK_ROOT}" REALPATH)
  if(NOT DUCKDB_API_CALLER_SDK_ROOT STREQUAL DUCKDB_API_SDK_ROOT)
    message(FATAL_ERROR
            "verified SDK does not match xcrun: ${DUCKDB_API_CALLER_SDK_ROOT} != ${DUCKDB_API_SDK_ROOT}")
  endif()
endif()

# The 0.4.0 cell consumes only the verified SDK headers and text stub. Disabling
# curl's config-package mode prevents an ambient Homebrew or package-manager
# installation from replacing those inputs before FindCURL creates its target.
set(CURL_NO_CURL_CMAKE ON CACHE BOOL "Disable ambient curl config packages" FORCE)
set(CURL_INCLUDE_DIR "${DUCKDB_API_SDK_ROOT}/usr/include" CACHE PATH
    "Verified macOS SDK curl include directory" FORCE)
set(CURL_LIBRARY "${DUCKDB_API_SDK_ROOT}/usr/lib/libcurl.4.tbd" CACHE FILEPATH
    "Verified macOS SDK curl text stub" FORCE)
find_package(CURL 8.7.1 EXACT REQUIRED)
if(DEFINED CURL_VERSION)
  set(DUCKDB_API_CURL_VERSION "${CURL_VERSION}")
else()
  set(DUCKDB_API_CURL_VERSION "${CURL_VERSION_STRING}")
endif()
if(NOT DUCKDB_API_CURL_VERSION STREQUAL "8.7.1")
  message(FATAL_ERROR "configured libcurl version drifted: ${DUCKDB_API_CURL_VERSION}")
endif()
get_target_property(
  DUCKDB_API_CURL_LOCATION CURL::libcurl IMPORTED_LOCATION)
if(NOT DUCKDB_API_CURL_LOCATION STREQUAL CURL_LIBRARY)
  message(FATAL_ERROR
          "CURL::libcurl resolved outside the verified SDK stub: ${DUCKDB_API_CURL_LOCATION}")
endif()
get_target_property(
  DUCKDB_API_CURL_INCLUDE_DIRS CURL::libcurl INTERFACE_INCLUDE_DIRECTORIES)
list(LENGTH DUCKDB_API_CURL_INCLUDE_DIRS DUCKDB_API_CURL_INCLUDE_COUNT)
if(NOT DUCKDB_API_CURL_INCLUDE_COUNT EQUAL 1 OR
   NOT DUCKDB_API_CURL_INCLUDE_DIRS STREQUAL CURL_INCLUDE_DIR)
  message(FATAL_ERROR
          "CURL::libcurl includes resolved outside the verified SDK: ${DUCKDB_API_CURL_INCLUDE_DIRS}")
endif()
file(
  WRITE "${CMAKE_CURRENT_BINARY_DIR}/duckdb_api_native_dependencies.json"
  "{\n"
  "  \"curl_include_dir\": \"${DUCKDB_API_CURL_INCLUDE_DIRS}\",\n"
  "  \"curl_library\": \"${DUCKDB_API_CURL_LOCATION}\",\n"
  "  \"curl_no_curl_cmake\": true,\n"
  "  \"curl_target\": \"CURL::libcurl\",\n"
  "  \"curl_version\": \"${DUCKDB_API_CURL_VERSION}\",\n"
  "  \"sdk_root\": \"${DUCKDB_API_SDK_ROOT}\"\n"
  "}\n")

find_package(Threads REQUIRED)

include_directories(src/include)

set(CONNECTOR_CATALOG_SOURCES src/connector_catalog.cpp)
set(CONNECTOR_NATIVE_PROFILE_SOURCES src/connector.cpp)
set(CONNECTOR_METADATA_SOURCES
    ${CONNECTOR_CATALOG_SOURCES}
    ${CONNECTOR_NATIVE_PROFILE_SOURCES})
set(CONNECTOR_TEST_SERVICE_SOURCES
    test/cpp/support/connector_catalog_test_fixtures.cpp)
set(QUERY_REQUEST_SOURCES src/scan_request.cpp)
set(RELATIONAL_PLANNING_SOURCES
    src/scan_plan.cpp
    src/scan_planner.cpp)
set(RELATIONAL_PLAN_TEST_SERVICE_SOURCES
    test/cpp/support/scan_plan_test_fixtures.cpp
    test/cpp/support/scan_plan_operation_test_fixtures.cpp
    test/cpp/support/scan_plan_auth_test_fixtures.cpp
    test/cpp/support/scan_plan_response_test_fixtures.cpp
    test/cpp/support/scan_plan_network_test_fixtures.cpp
    test/cpp/support/scan_plan_feature_test_fixtures.cpp
    test/cpp/support/scan_plan_resource_test_fixtures.cpp
    test/cpp/support/scan_plan_fixture_consumer_probe.cpp)
set(RELATIONAL_PLAN_TEST_CONTRACT_SOURCES
    test/cpp/support/scan_plan_test_fixture_test_support.cpp
    test/cpp/scan_plan_operation_test_fixture_tests.cpp
    test/cpp/scan_plan_auth_test_fixture_tests.cpp
    test/cpp/scan_plan_response_test_fixture_tests.cpp
    test/cpp/scan_plan_network_test_fixture_tests.cpp
    test/cpp/scan_plan_feature_test_fixture_tests.cpp
    test/cpp/scan_plan_resource_test_fixture_tests.cpp
    test/cpp/scan_plan_fixture_consumer_boundary_tests.cpp)
set(REMOTE_RUNTIME_INTERFACE_SOURCES
    src/authorization.cpp
    src/execution_error.cpp)
set(REMOTE_RUNTIME_SOURCES
    src/fixed_github_user_bearer_authenticator.cpp
    src/json_decoder.cpp
    src/http_scan_executor.cpp
    src/network_policy.cpp
    src/curl_transfer.cpp
    src/curl_http_transport.cpp
    src/http_runtime.cpp)
set(REMOTE_RUNTIME_CURL_TEST_SUPPORT_SOURCES
    test/cpp/support/controlled_socket_service.cpp
    test/cpp/support/loopback_curl_runtime.cpp
    test/cpp/support/runtime_http_test_support.cpp
    test/cpp/support/private_curl_probe.cpp)
set(QUERY_PRODUCT_COMPOSITION_SOURCES src/product_composition.cpp)
set(QUERY_DUCKDB_SECRET_SOURCES src/duckdb_secret.cpp)
set(QUERY_DUCKDB_ADAPTER_SOURCES
    src/duckdb_api_adapter.cpp
    src/duckdb_api_extension.cpp)
set(QUERY_TEST_SUPPORT_SOURCES
    test/cpp/support/query_runtime_scenarios.cpp
    test/cpp/support/duckdb_adapter_auth_test_support.cpp)
set(QUERY_SECRET_TEST_SUPPORT_SOURCES
    test/cpp/support/duckdb_secret_test_support.cpp)
set(REMOTE_RUNTIME_EXECUTOR_TEST_SUPPORT_SOURCES
    test/cpp/support/controlled_http_transport.cpp
    test/cpp/support/http_scan_executor_test_support.cpp)
set(QUERY_CONTROLLED_COMPOSITION_SOURCES
    test/cpp/support/loopback_curl_runtime.cpp
    test/cpp/support/controlled_product_composition.cpp
    test/cpp/controlled_duckdb_api_extension.cpp)
set(PRODUCTION_SOURCES
    ${CONNECTOR_METADATA_SOURCES}
    ${QUERY_REQUEST_SOURCES}
    ${RELATIONAL_PLANNING_SOURCES}
    ${REMOTE_RUNTIME_INTERFACE_SOURCES}
    ${REMOTE_RUNTIME_SOURCES}
    ${QUERY_PRODUCT_COMPOSITION_SOURCES}
    ${QUERY_DUCKDB_SECRET_SOURCES}
    ${QUERY_DUCKDB_ADAPTER_SOURCES})
set(CONTROLLED_PRODUCT_SOURCES
    ${CONNECTOR_METADATA_SOURCES}
    ${QUERY_REQUEST_SOURCES}
    ${RELATIONAL_PLANNING_SOURCES}
    ${REMOTE_RUNTIME_INTERFACE_SOURCES}
    ${REMOTE_RUNTIME_SOURCES}
    ${QUERY_DUCKDB_SECRET_SOURCES}
    src/duckdb_api_adapter.cpp
    ${QUERY_CONTROLLED_COMPOSITION_SOURCES})

# The release identity verifier owns the expected path set; this configure-time
# observation proves that the actual public and private product compositions
# match their recorded translation-unit sets. Keep paths repository-relative so
# a different worktree root does not alter product identity.
function(duckdb_api_source_array_json output)
  set(result "[")
  set(separator "")
  foreach(source IN LISTS ARGN)
    if(NOT source MATCHES "^[A-Za-z0-9_./-]+$")
      message(FATAL_ERROR "product source path cannot be represented safely: ${source}")
    endif()
    string(APPEND result "${separator}\"${source}\"")
    set(separator ",")
  endforeach()
  string(APPEND result "]")
  set(${output} "${result}" PARENT_SCOPE)
endfunction()
duckdb_api_source_array_json(
  DUCKDB_API_PUBLIC_TRANSLATION_UNITS_JSON ${PRODUCTION_SOURCES})
duckdb_api_source_array_json(
  DUCKDB_API_CONTROLLED_TRANSLATION_UNITS_JSON ${CONTROLLED_PRODUCT_SOURCES})
file(
  WRITE "${CMAKE_CURRENT_BINARY_DIR}/duckdb_api_product_sources.json"
  "{\n"
  "  \"controlled_translation_units\": ${DUCKDB_API_CONTROLLED_TRANSLATION_UNITS_JSON},\n"
  "  \"public_translation_units\": ${DUCKDB_API_PUBLIC_TRANSLATION_UNITS_JSON}\n"
  "}\n")

build_static_extension(${TARGET_NAME} ${PRODUCTION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${PRODUCTION_SOURCES})
# This private artifact is a deterministic product oracle, not an installable
# extension. Its entry/composition replace the installed entry/composition and
# are intentionally omitted from every install rule.
set(DUCKDB_EXTENSION_DUCKDB_API_CONTROLLED_EXT_VERSION "0.4.0")
build_loadable_extension(duckdb_api_controlled " " ${CONTROLLED_PRODUCT_SOURCES})

function(configure_duckdb_api_cpp_target target)
  set_target_properties(
    ${target}
    PROPERTIES CXX_STANDARD 11
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO)
  # DuckDB 1.5.4 headers intentionally contain unused default parameters.
  # Keep project warnings fatal while exempting that upstream header pattern.
  target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endfunction()

add_executable(
  duckdb_api_native_dependency_identity
  test/cpp/native_dependency_identity.cpp)
configure_duckdb_api_cpp_target(duckdb_api_native_dependency_identity)
target_link_libraries(
  duckdb_api_native_dependency_identity
  CURL::libcurl)

foreach(target ${TARGET_NAME}_extension ${TARGET_NAME}_loadable_extension)
  configure_duckdb_api_cpp_target(${target})
  target_link_libraries(${target} CURL::libcurl Threads::Threads)
endforeach()

configure_duckdb_api_cpp_target(duckdb_api_controlled_loadable_extension)
set_target_properties(
  duckdb_api_controlled_loadable_extension
  PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/private")
target_include_directories(duckdb_api_controlled_loadable_extension PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_controlled_loadable_extension
  CURL::libcurl
  Threads::Threads)

add_executable(
  duckdb_api_connector_tests
  test/cpp/connector_contract_tests.cpp
  test/cpp/connector_catalog_contract_tests.cpp
  ${CONNECTOR_METADATA_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_connector_tests)
target_include_directories(duckdb_api_connector_tests PRIVATE test/cpp)
target_compile_definitions(
  duckdb_api_connector_tests
  PRIVATE DUCKDB_API_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(
  duckdb_api_connector_catalog_fixture_tests
  test/cpp/connector_catalog_test_fixtures_tests.cpp
  ${CONNECTOR_CATALOG_SOURCES}
  ${CONNECTOR_TEST_SERVICE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_connector_catalog_fixture_tests)
target_include_directories(duckdb_api_connector_catalog_fixture_tests PRIVATE test/cpp)

add_executable(
  duckdb_api_scan_request_tests
  test/cpp/scan_request_tests.cpp
  ${CONNECTOR_METADATA_SOURCES}
  ${CONNECTOR_TEST_SERVICE_SOURCES}
  ${QUERY_REQUEST_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_scan_request_tests)
target_include_directories(duckdb_api_scan_request_tests PRIVATE test/cpp)

add_executable(
  duckdb_api_scan_planner_tests
  test/cpp/scan_planner_tests.cpp
  ${CONNECTOR_METADATA_SOURCES}
  ${CONNECTOR_TEST_SERVICE_SOURCES}
  ${QUERY_REQUEST_SOURCES}
  ${RELATIONAL_PLANNING_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_scan_planner_tests)
target_include_directories(duckdb_api_scan_planner_tests PRIVATE test/cpp)

add_executable(
  duckdb_api_scan_plan_contract_tests
  test/cpp/scan_plan_contract_tests.cpp
  ${CONNECTOR_METADATA_SOURCES}
  ${CONNECTOR_TEST_SERVICE_SOURCES}
  ${QUERY_REQUEST_SOURCES}
  ${RELATIONAL_PLANNING_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_scan_plan_contract_tests)
target_include_directories(duckdb_api_scan_plan_contract_tests PRIVATE test/cpp)

add_executable(
  duckdb_api_scan_plan_fixture_tests
  test/cpp/scan_plan_test_fixtures_tests.cpp
  ${CONNECTOR_METADATA_SOURCES}
  ${QUERY_REQUEST_SOURCES}
  ${RELATIONAL_PLANNING_SOURCES}
  ${RELATIONAL_PLAN_TEST_SERVICE_SOURCES}
  ${RELATIONAL_PLAN_TEST_CONTRACT_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_scan_plan_fixture_tests)
target_include_directories(duckdb_api_scan_plan_fixture_tests PRIVATE test/cpp)
target_compile_definitions(
  duckdb_api_scan_plan_fixture_tests
  PRIVATE DUCKDB_API_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(
  duckdb_api_execution_contract_tests
  test/cpp/execution_contract_tests.cpp
  ${REMOTE_RUNTIME_INTERFACE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_execution_contract_tests)

add_executable(
  duckdb_api_authorization_contract_tests
  test/cpp/authorization_contract_tests.cpp
  ${CONNECTOR_METADATA_SOURCES}
  ${QUERY_REQUEST_SOURCES}
  ${RELATIONAL_PLANNING_SOURCES}
  test/cpp/support/scan_plan_test_fixtures.cpp
  ${REMOTE_RUNTIME_INTERFACE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_authorization_contract_tests)
target_include_directories(duckdb_api_authorization_contract_tests PRIVATE test/cpp)

add_executable(
  duckdb_api_network_policy_tests
  test/cpp/network_policy_tests.cpp
  src/network_policy.cpp)
configure_duckdb_api_cpp_target(duckdb_api_network_policy_tests)
target_include_directories(duckdb_api_network_policy_tests PRIVATE test/cpp)

add_executable(
  duckdb_api_json_decoder_tests
  test/cpp/json_decoder_tests.cpp
  ${REMOTE_RUNTIME_INTERFACE_SOURCES}
  src/json_decoder.cpp)
configure_duckdb_api_cpp_target(duckdb_api_json_decoder_tests)
target_include_directories(duckdb_api_json_decoder_tests PRIVATE test/cpp)

add_executable(
  duckdb_api_http_scan_executor_tests
  test/cpp/http_scan_executor_tests.cpp
  ${REMOTE_RUNTIME_EXECUTOR_TEST_SUPPORT_SOURCES}
  ${CONNECTOR_METADATA_SOURCES}
  ${QUERY_REQUEST_SOURCES}
  ${RELATIONAL_PLANNING_SOURCES}
  test/cpp/support/scan_plan_test_fixtures.cpp
  ${REMOTE_RUNTIME_INTERFACE_SOURCES}
  src/fixed_github_user_bearer_authenticator.cpp
  src/json_decoder.cpp
  src/http_scan_executor.cpp)
configure_duckdb_api_cpp_target(duckdb_api_http_scan_executor_tests)
target_include_directories(duckdb_api_http_scan_executor_tests PRIVATE test/cpp)
target_link_libraries(duckdb_api_http_scan_executor_tests Threads::Threads)

add_executable(
  duckdb_api_http_scan_executor_policy_tests
  test/cpp/http_scan_executor_policy_tests.cpp
  ${REMOTE_RUNTIME_EXECUTOR_TEST_SUPPORT_SOURCES}
  ${CONNECTOR_METADATA_SOURCES}
  ${QUERY_REQUEST_SOURCES}
  ${RELATIONAL_PLANNING_SOURCES}
  ${RELATIONAL_PLAN_TEST_SERVICE_SOURCES}
  ${REMOTE_RUNTIME_INTERFACE_SOURCES}
  src/fixed_github_user_bearer_authenticator.cpp
  src/json_decoder.cpp
  src/http_scan_executor.cpp)
configure_duckdb_api_cpp_target(duckdb_api_http_scan_executor_policy_tests)
target_include_directories(duckdb_api_http_scan_executor_policy_tests PRIVATE test/cpp)
target_link_libraries(duckdb_api_http_scan_executor_policy_tests Threads::Threads)

function(add_duckdb_api_private_curl_test target source)
  add_executable(
    ${target}
    ${source}
    ${REMOTE_RUNTIME_CURL_TEST_SUPPORT_SOURCES}
    ${CONNECTOR_METADATA_SOURCES}
    ${QUERY_REQUEST_SOURCES}
    ${RELATIONAL_PLANNING_SOURCES}
    ${REMOTE_RUNTIME_INTERFACE_SOURCES}
    ${REMOTE_RUNTIME_SOURCES})
  configure_duckdb_api_cpp_target(${target})
  target_include_directories(${target} PRIVATE test/cpp)
  target_compile_definitions(${target} PRIVATE DUCKDB_API_PRIVATE_CURL_TESTS)
  target_link_libraries(${target} CURL::libcurl Threads::Threads)
endfunction()

add_duckdb_api_private_curl_test(
  duckdb_api_curl_http_transport_tests
  test/cpp/curl_http_transport_tests.cpp)
add_duckdb_api_private_curl_test(
  duckdb_api_curl_http_budget_tests
  test/cpp/curl_http_budget_tests.cpp)
add_duckdb_api_private_curl_test(
  duckdb_api_curl_http_lifecycle_tests
  test/cpp/curl_http_lifecycle_tests.cpp)
add_duckdb_api_private_curl_test(
  duckdb_api_curl_transfer_policy_tests
  test/cpp/curl_transfer_policy_tests.cpp)

add_executable(
  duckdb_api_curl_tls_security_tests
  test/cpp/curl_tls_security_tests.cpp
  test/cpp/support/runtime_http_test_support.cpp
  test/cpp/support/private_curl_probe.cpp
  ${CONNECTOR_METADATA_SOURCES}
  ${QUERY_REQUEST_SOURCES}
  ${RELATIONAL_PLANNING_SOURCES}
  ${REMOTE_RUNTIME_INTERFACE_SOURCES}
  ${REMOTE_RUNTIME_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_curl_tls_security_tests)
target_include_directories(duckdb_api_curl_tls_security_tests PRIVATE test/cpp)
target_compile_definitions(
  duckdb_api_curl_tls_security_tests PRIVATE DUCKDB_API_PRIVATE_CURL_TESTS)
target_link_libraries(
  duckdb_api_curl_tls_security_tests CURL::libcurl Threads::Threads)

add_executable(
  duckdb_api_duckdb_secret_tests
  test/cpp/duckdb_secret_tests.cpp
  test/cpp/duckdb_secret_creation_tests.cpp
  test/cpp/duckdb_secret_resolution_tests.cpp
  ${QUERY_SECRET_TEST_SUPPORT_SOURCES}
  ${CONNECTOR_METADATA_SOURCES}
  ${QUERY_REQUEST_SOURCES}
  ${RELATIONAL_PLANNING_SOURCES}
  ${REMOTE_RUNTIME_INTERFACE_SOURCES}
  ${QUERY_DUCKDB_SECRET_SOURCES}
  src/duckdb_api_adapter.cpp)
target_link_libraries(
  duckdb_api_duckdb_secret_tests
  dummy_static_extension_loader
  duckdb_static
  Threads::Threads)
target_include_directories(duckdb_api_duckdb_secret_tests PRIVATE test/cpp)
configure_duckdb_api_cpp_target(duckdb_api_duckdb_secret_tests)

add_executable(
  duckdb_api_adapter_tests
  test/cpp/duckdb_adapter_tests.cpp
  test/cpp/duckdb_adapter_auth_bind_tests.cpp
  test/cpp/duckdb_adapter_auth_lifecycle_tests.cpp
  ${CONNECTOR_METADATA_SOURCES}
  ${CONNECTOR_TEST_SERVICE_SOURCES}
  ${QUERY_REQUEST_SOURCES}
  ${RELATIONAL_PLANNING_SOURCES}
  ${REMOTE_RUNTIME_INTERFACE_SOURCES}
  ${QUERY_DUCKDB_SECRET_SOURCES}
  src/duckdb_api_adapter.cpp
  ${QUERY_TEST_SUPPORT_SOURCES})
target_link_libraries(
  duckdb_api_adapter_tests
  dummy_static_extension_loader
  duckdb_static
  Threads::Threads)
target_include_directories(duckdb_api_adapter_tests PRIVATE test/cpp)
configure_duckdb_api_cpp_target(duckdb_api_adapter_tests)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
