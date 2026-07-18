cmake_minimum_required(VERSION 3.10)

set(TARGET_NAME live_rest_product_proof)
set(EXTENSION_NAME ${TARGET_NAME}_extension)

project(${TARGET_NAME})

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

include_directories(src/include)

set(PLAN_SOURCES src/plan.cpp)
set(RUNTIME_ERROR_SOURCES src/runtime_error.cpp)
set(DECODER_SOURCES src/json_decoder.cpp)
set(HTTP_SCAN_RUNTIME_SOURCES src/http_scan_runtime.cpp)
set(NETWORK_POLICY_SOURCES src/network_policy.cpp)
set(RUNTIME_SOURCES
    ${RUNTIME_ERROR_SOURCES}
    ${DECODER_SOURCES}
    ${HTTP_SCAN_RUNTIME_SOURCES})
set(ADAPTER_SOURCES
    ${NETWORK_POLICY_SOURCES}
    src/curl_http_transport.cpp
    src/live_rest_extension.cpp)
set(PRODUCTION_SOURCES
    ${PLAN_SOURCES}
    ${RUNTIME_SOURCES}
    ${ADAPTER_SOURCES})

build_static_extension(${TARGET_NAME} ${PRODUCTION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${PRODUCTION_SOURCES})

function(configure_live_rest_target target)
  set_target_properties(
    ${target}
    PROPERTIES CXX_STANDARD 11
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO)
  # DuckDB 1.5.4 headers intentionally contain unused default parameters.
  target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endfunction()

foreach(target ${TARGET_NAME}_extension ${TARGET_NAME}_loadable_extension)
  configure_live_rest_target(${target})
  target_include_directories(${target} PRIVATE ${CURL_INCLUDE_DIRS})
  target_link_libraries(${target} ${CURL_LIBRARIES})
endforeach()

add_executable(
  live_rest_plan_tests
  test/plan_tests.cpp
  ${PLAN_SOURCES})
configure_live_rest_target(live_rest_plan_tests)

add_executable(
  live_rest_json_decoder_tests
  test/json_decoder_tests.cpp
  ${PLAN_SOURCES}
  ${RUNTIME_ERROR_SOURCES}
  ${DECODER_SOURCES})
configure_live_rest_target(live_rest_json_decoder_tests)

add_executable(
  live_rest_http_scan_runtime_tests
  test/http_scan_runtime_tests.cpp
  ${PLAN_SOURCES}
  ${RUNTIME_SOURCES})
configure_live_rest_target(live_rest_http_scan_runtime_tests)
target_link_libraries(live_rest_http_scan_runtime_tests Threads::Threads)

add_executable(
  live_rest_network_policy_tests
  test/network_policy_tests.cpp
  ${NETWORK_POLICY_SOURCES})
configure_live_rest_target(live_rest_network_policy_tests)

add_executable(live_rest_curl_identity test/curl_identity.cpp)
configure_live_rest_target(live_rest_curl_identity)
target_include_directories(live_rest_curl_identity PRIVATE ${CURL_INCLUDE_DIRS})
target_link_libraries(live_rest_curl_identity ${CURL_LIBRARIES})

if(DEFINED CURL_VERSION)
  set(LIVE_REST_CURL_VERSION "${CURL_VERSION}")
elseif(DEFINED CURL_VERSION_STRING)
  set(LIVE_REST_CURL_VERSION "${CURL_VERSION_STRING}")
else()
  message(FATAL_ERROR "FindCURL did not report the configured dependency version")
endif()
file(
  WRITE "${CMAKE_BINARY_DIR}/live_rest_dependency_evidence.txt"
  "libcurl_version=${LIVE_REST_CURL_VERSION}\nlibcurl_include_dirs=${CURL_INCLUDE_DIRS}\nlibcurl_libraries=${CURL_LIBRARIES}\n")

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
