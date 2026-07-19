# Runtime publishes narrow link targets so consumers take only the service
# layer they exercise. None of these targets imports provider-private sources.
add_library(
  duckdb_api_runtime_interface_service STATIC
  ${REMOTE_RUNTIME_INTERFACE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_runtime_interface_service)

add_library(
  duckdb_api_runtime_executor_service STATIC
  ${REMOTE_RUNTIME_EXECUTOR_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_runtime_executor_service)
target_link_libraries(
  duckdb_api_runtime_executor_service
  PUBLIC duckdb_api_runtime_interface_service
         duckdb_api_scan_plan_service)
