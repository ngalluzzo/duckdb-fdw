# Runtime consumes only the immutable plan value; planner construction retains
# Connector and Query dependencies behind a separate Semantics service.
set(RELATIONAL_PLAN_VALUE_SOURCES
    src/semantics/scan_plan.cpp
    src/semantics/scan_plan_explain.cpp)
set(RELATIONAL_PLANNER_SOURCES
    src/semantics/scan_planner.cpp
    src/semantics/scan_planner_validation.cpp)
set(RELATIONAL_PLANNING_SOURCES
    ${RELATIONAL_PLAN_VALUE_SOURCES}
    ${RELATIONAL_PLANNER_SOURCES})
