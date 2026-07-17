cmake_minimum_required(VERSION 3.10)

set(TARGET_NAME duckdb_api)
set(EXTENSION_NAME ${TARGET_NAME}_extension)

project(${TARGET_NAME})

find_package(Threads REQUIRED)

include_directories(src/include)

set(PRODUCTION_SOURCES
    src/duckdb_api_extension.cpp
    src/duckdb_api_core.cpp)

build_static_extension(${TARGET_NAME} ${PRODUCTION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${PRODUCTION_SOURCES})

foreach(target ${TARGET_NAME}_extension ${TARGET_NAME}_loadable_extension)
  set_target_properties(
    ${target}
    PROPERTIES CXX_STANDARD 11
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO)
  # DuckDB 1.5.4 headers intentionally contain unused default parameters.
  # Keep project warnings fatal while exempting that upstream header pattern.
  target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endforeach()

add_executable(
  duckdb_api_contract_tests
  test/cpp/duckdb_api_contract_tests.cpp
  ${PRODUCTION_SOURCES})
target_link_libraries(
  duckdb_api_contract_tests
  dummy_static_extension_loader
  duckdb_static
  Threads::Threads)
set_target_properties(
  duckdb_api_contract_tests
  PROPERTIES CXX_STANDARD 11
             CXX_STANDARD_REQUIRED YES
             CXX_EXTENSIONS NO)
target_compile_options(duckdb_api_contract_tests PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
target_compile_definitions(
  duckdb_api_contract_tests
  PRIVATE DUCKDB_API_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
