cmake_minimum_required(VERSION 3.10)

set(TARGET_NAME duckdb_api)
set(EXTENSION_NAME ${TARGET_NAME}_extension)

project(${TARGET_NAME})

if(NOT APPLE)
  message(FATAL_ERROR "duckdb_api 0.3.0 supports only the pinned macOS arm64 cell")
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

# The 0.3.0 cell consumes only the verified SDK headers and text stub. Disabling
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

set(CONNECTOR_SOURCES src/connector.cpp)
set(PLANNING_SOURCES
    src/scan_request.cpp
    src/scan_planner.cpp)
set(RUNTIME_SOURCES
    src/execution_error.cpp
    src/fixture_decoder.cpp
    src/fixture_runtime.cpp)
set(COMPOSITION_SOURCES src/example_composition.cpp)
set(ADAPTER_SOURCES src/duckdb_api_extension.cpp)
set(TEST_SUPPORT_SOURCES test/cpp/support/fixture_scenarios.cpp)
set(PRODUCTION_SOURCES
    ${CONNECTOR_SOURCES}
    ${PLANNING_SOURCES}
    ${RUNTIME_SOURCES}
    ${COMPOSITION_SOURCES}
    ${ADAPTER_SOURCES})

build_static_extension(${TARGET_NAME} ${PRODUCTION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${PRODUCTION_SOURCES})

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
endforeach()

add_executable(
  duckdb_api_connector_tests
  test/cpp/connector_contract_tests.cpp
  ${CONNECTOR_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_connector_tests)
target_compile_definitions(
  duckdb_api_connector_tests
  PRIVATE DUCKDB_API_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(
  duckdb_api_scan_planner_tests
  test/cpp/scan_planner_tests.cpp
  ${CONNECTOR_SOURCES}
  ${PLANNING_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_scan_planner_tests)

add_executable(
  duckdb_api_fixture_decoder_tests
  test/cpp/fixture_decoder_tests.cpp
  ${CONNECTOR_SOURCES}
  ${PLANNING_SOURCES}
  ${RUNTIME_SOURCES}
  ${TEST_SUPPORT_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_fixture_decoder_tests)
target_link_libraries(duckdb_api_fixture_decoder_tests Threads::Threads)

add_executable(
  duckdb_api_fixture_stream_tests
  test/cpp/fixture_stream_tests.cpp
  ${CONNECTOR_SOURCES}
  ${PLANNING_SOURCES}
  ${RUNTIME_SOURCES}
  ${TEST_SUPPORT_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_fixture_stream_tests)
target_link_libraries(duckdb_api_fixture_stream_tests Threads::Threads)

add_executable(
  duckdb_api_adapter_tests
  test/cpp/duckdb_adapter_tests.cpp
  ${PRODUCTION_SOURCES})
target_link_libraries(
  duckdb_api_adapter_tests
  dummy_static_extension_loader
  duckdb_static
  Threads::Threads)
target_sources(duckdb_api_adapter_tests PRIVATE ${TEST_SUPPORT_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_adapter_tests)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
