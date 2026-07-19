# The immutable plan is the narrow X-as-a-Service contract consumed by Runtime.
add_library(
  duckdb_api_scan_plan_service STATIC
  ${RELATIONAL_PLAN_VALUE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_scan_plan_service)

# Planner construction consumes bounded Connector and Query services. Query
# and Semantics fixtures use this service; Runtime does not.
add_library(
  duckdb_api_relational_planning_service STATIC
  ${RELATIONAL_PLANNER_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_relational_planning_service)
target_link_libraries(
  duckdb_api_relational_planning_service
  PUBLIC duckdb_api_scan_plan_service
         duckdb_api_connector_metadata_service
         duckdb_api_query_request_service)
