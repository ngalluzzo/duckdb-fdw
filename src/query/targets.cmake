# The protocol-neutral request is Query Experience's provider API to
# Relational Semantics and its own DuckDB adapter.
add_library(
  duckdb_api_query_request_service STATIC
  ${QUERY_REQUEST_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_query_request_service)
target_link_libraries(
  duckdb_api_query_request_service
  PUBLIC duckdb_api_connector_metadata_service)
