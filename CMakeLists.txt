cmake_minimum_required(VERSION 3.19)

set(TARGET_NAME duckdb_api)
set(EXTENSION_NAME ${TARGET_NAME}_extension)

project(${TARGET_NAME})

include(cmake/DuckDBApiProductSources.cmake)

if(NOT APPLE)
  message(FATAL_ERROR "duckdb_api 0.5.0 supports only the pinned macOS arm64 cell")
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

# The 0.5.0 cell consumes only the verified SDK headers and text stub. Disabling
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

# Each topology team owns its production and focused-test inventory. The root
# build composes those bounded inventories into product and integration targets.
include(src/connector/sources.cmake)
include(src/query/sources.cmake)
include(src/semantics/sources.cmake)
include(src/runtime/sources.cmake)
include(test/cpp/connector/sources.cmake)
include(test/cpp/query/sources.cmake)
include(test/cpp/semantics/sources.cmake)
include(test/cpp/runtime/sources.cmake)
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
    src/query/duckdb/table_function_adapter.cpp
    ${QUERY_CONTROLLED_COMPOSITION_SOURCES})

build_static_extension(${TARGET_NAME} ${PRODUCTION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${PRODUCTION_SOURCES})
# This private artifact is a deterministic product oracle, not an installable
# extension. Its entry/composition replace the installed entry/composition and
# are intentionally omitted from every install rule.
set(DUCKDB_EXTENSION_DUCKDB_API_CONTROLLED_EXT_VERSION "0.5.0")
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

# Package-owned targets make source and test dependencies reviewable without a
# shared CMake editing hotspot. Provider services precede their consumers.
include(src/connector/targets.cmake)
include(src/query/targets.cmake)
include(src/semantics/targets.cmake)
include(src/runtime/targets.cmake)
include(test/cpp/connector/targets.cmake)
include(test/cpp/semantics/targets.cmake)
include(test/cpp/runtime/targets.cmake)
include(test/cpp/query/targets.cmake)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")

# Keep this as the literal final command. CMake executes deferred calls in
# scheduling order, so the product-graph observer must follow every possible
# deferred target mutation in this directory.
duckdb_api_defer_product_source_record(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/duckdb_api_product_sources.json"
  PUBLIC_STATIC "${TARGET_NAME}_extension"
  PUBLIC_LOADABLE "${TARGET_NAME}_loadable_extension"
  CONTROLLED "duckdb_api_controlled_loadable_extension")
