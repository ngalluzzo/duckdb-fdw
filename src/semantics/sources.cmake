# Runtime consumes only the immutable plan value; planner construction retains
# Connector and Query dependencies behind a separate Semantics service.
set(RELATIONAL_PREDICATE_SOURCES
    src/semantics/relational_predicate.cpp)
set(RELATIONAL_PLAN_VALUE_SOURCES
    src/semantics/planned_protocol_operation.cpp
    src/semantics/scan_plan.cpp
    src/semantics/scan_plan_explain.cpp)
set(RELATIONAL_PLANNER_SOURCES
    src/semantics/graphql_operation_planner.cpp
    src/semantics/input_resolution.cpp
    src/semantics/operation_selection.cpp
    src/semantics/predicate_classifier.cpp
    src/semantics/rest_operation_planner.cpp
    src/semantics/scan_planner.cpp
    src/semantics/scan_planner_validation.cpp)
set(RELATIONAL_PACKAGE_BOUND_PLANNER_SOURCES
    src/semantics/package_bound_scan_planner.cpp)
set(RELATIONAL_PLANNING_SOURCES
    ${RELATIONAL_PREDICATE_SOURCES}
    ${RELATIONAL_PLAN_VALUE_SOURCES}
    ${RELATIONAL_PLANNER_SOURCES}
    ${RELATIONAL_PACKAGE_BOUND_PLANNER_SOURCES})
