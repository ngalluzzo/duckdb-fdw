# Runtime publishes narrow link targets so consumers take only the service
# layer they exercise. None of these targets imports provider-private sources.
add_library(
  duckdb_api_runtime_interface_service STATIC
  ${REMOTE_RUNTIME_INTERFACE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_runtime_interface_service)

# Runtime's immutable generation registry is a bounded lifecycle service. It
# consumes Connector's public opaque local-package custody and compatibility
# facades but has no source acquisition, YAML, compiler, Query, catalog, or
# DuckDB dependency.
add_library(
  duckdb_api_runtime_generation_service STATIC
  ${REMOTE_RUNTIME_GENERATION_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_runtime_generation_service)
target_link_libraries(
  duckdb_api_runtime_generation_service
  PUBLIC duckdb_api_runtime_interface_service
         duckdb_api_local_package_custody_service)

# Runtime's reactive resilience foundation is independently testable and owns
# no protocol, transport, credential, ScanPlan, Query, or DuckDB dependency.
add_library(
  duckdb_api_runtime_resilience_service STATIC
  ${REMOTE_RUNTIME_RESILIENCE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_runtime_resilience_service)
target_link_libraries(
  duckdb_api_runtime_resilience_service
  PRIVATE Threads::Threads)

add_library(
  duckdb_api_runtime_executor_service STATIC
  ${REMOTE_RUNTIME_EXECUTOR_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_runtime_executor_service)
target_link_libraries(
  duckdb_api_runtime_executor_service
  PUBLIC duckdb_api_runtime_interface_service
         duckdb_api_scan_plan_service
         duckdb_api_content_digest_service
  PRIVATE duckdb_api_runtime_resilience_service)
