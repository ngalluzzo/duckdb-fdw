# Query and Semantics share only the closed protocol-neutral predicate value.
# Keeping it below Query request construction avoids a Query/Semantics target
# cycle while leaving interpretation owned by Semantics.
add_library(
  duckdb_api_relational_predicate_service STATIC
  ${RELATIONAL_PREDICATE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_relational_predicate_service)

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
         duckdb_api_relational_predicate_service
         duckdb_api_connector_metadata_service
         duckdb_api_query_request_service)
