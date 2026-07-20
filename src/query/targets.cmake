# The protocol-neutral request is Query Experience's provider API to
# Relational Semantics and its own DuckDB adapter.
add_library(
  duckdb_api_query_request_service STATIC
  ${QUERY_REQUEST_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_query_request_service)
target_link_libraries(
  duckdb_api_query_request_service
  PUBLIC duckdb_api_connector_metadata_service
         duckdb_api_relational_predicate_service)

# Query's DuckDB value-boundary service consumes only the immutable plan and
# Runtime row interfaces. It owns strict nullability/kind enforcement and has
# no protocol decoder or provider construction dependency.
add_library(
  duckdb_api_query_typed_value_adapter_service STATIC
  src/query/duckdb/typed_value_adapter.cpp)
configure_duckdb_api_cpp_target(duckdb_api_query_typed_value_adapter_service)
target_include_directories(
  duckdb_api_query_typed_value_adapter_service
  PUBLIC src/query/duckdb)
target_link_libraries(
  duckdb_api_query_typed_value_adapter_service
  PUBLIC duckdb_api_scan_plan_service
         duckdb_api_runtime_interface_service
         duckdb_static)
