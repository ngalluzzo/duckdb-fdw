# Query Experience owns request construction, installed composition, and the
# DuckDB adapter. Controlled composition remains test-owned and is listed by
# the corresponding test package.
set(QUERY_REQUEST_SOURCES
    src/query/scan_request.cpp)
set(QUERY_PRODUCT_COMPOSITION_SOURCES
    src/query/product_composition.cpp)
set(QUERY_DUCKDB_SECRET_SOURCES
    src/query/duckdb/secret_integration.cpp)
set(QUERY_DUCKDB_ADAPTER_SUPPORT_SOURCES
    src/query/duckdb/complex_filter_adapter.cpp
    src/query/duckdb/scan_plan_explanation.cpp
    src/query/duckdb/table_function_plan_state.cpp
    src/query/duckdb/typed_value_adapter.cpp)
set(QUERY_DUCKDB_ADAPTER_SOURCES
    ${QUERY_DUCKDB_ADAPTER_SUPPORT_SOURCES}
    src/query/duckdb/table_function_adapter.cpp
    src/query/duckdb/extension_entrypoint.cpp)
