cmake_minimum_required(VERSION 3.10)

set(TARGET_NAME duckdb_api)
set(EXTENSION_NAME ${TARGET_NAME}_extension)

project(${TARGET_NAME})

find_package(Threads REQUIRED)

include_directories(src/include)

set(CONNECTOR_SOURCES src/connector.cpp)
set(PLANNING_SOURCES
    src/scan_request.cpp
    src/scan_planner.cpp)
set(RUNTIME_SOURCES src/duckdb_api_core.cpp)
set(ADAPTER_SOURCES src/duckdb_api_extension.cpp)
set(PRODUCTION_SOURCES
    ${CONNECTOR_SOURCES}
    ${PLANNING_SOURCES}
    ${RUNTIME_SOURCES}
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
  duckdb_api_contract_tests
  test/cpp/duckdb_api_contract_tests.cpp
  ${PRODUCTION_SOURCES})
target_link_libraries(
  duckdb_api_contract_tests
  dummy_static_extension_loader
  duckdb_static
  Threads::Threads)
configure_duckdb_api_cpp_target(duckdb_api_contract_tests)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
