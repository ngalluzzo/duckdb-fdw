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

# Query's bounded package-catalog provider owns the actual DuckDB publication
# path. Consumers link this target; they do not list or include its private
# production sources. Runtime and Connector implementations remain behind the
# public QueryPackageStagingService port.
add_library(
  duckdb_api_query_package_catalog_service STATIC
  ${QUERY_PACKAGE_CATALOG_SOURCES}
  ${QUERY_DUCKDB_SECRET_SOURCES}
  ${QUERY_DUCKDB_ADAPTER_SUPPORT_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_query_package_catalog_service)
target_include_directories(
  duckdb_api_query_package_catalog_service
  PRIVATE src/query/duckdb)
target_link_libraries(
  duckdb_api_query_package_catalog_service
  PUBLIC duckdb_api_query_request_service
         duckdb_api_scan_plan_service
         duckdb_api_runtime_interface_service
         duckdb_static)

# Lead product composition adapts bounded Connector, Semantics, and Runtime
# generation services to Query's staging port. It owns no DuckDB catalog or
# transport implementation and accepts only Runtime's ScanExecutor interface.
add_library(
  duckdb_api_package_generation_composition_service STATIC
  ${QUERY_PACKAGE_GENERATION_COMPOSITION_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_package_generation_composition_service)
target_link_libraries(
  duckdb_api_package_generation_composition_service
  PUBLIC duckdb_api_query_request_service
         duckdb_api_package_compiler_service
         duckdb_api_package_bound_planning_service
         duckdb_api_runtime_generation_service)
