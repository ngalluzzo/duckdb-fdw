# Query Experience owns request construction, installed composition, and the
# DuckDB adapter. Controlled composition remains test-owned and is listed by
# the corresponding test package.
set(QUERY_REQUEST_SOURCES
    src/query/scan_request.cpp
    src/query/query_generation.cpp)
set(QUERY_PACKAGE_GENERATION_COMPOSITION_SOURCES
    src/query/package_generation_composition.cpp)
set(QUERY_PRODUCT_COMPOSITION_SOURCES
    ${QUERY_PACKAGE_GENERATION_COMPOSITION_SOURCES}
    src/query/product_composition.cpp)
set(QUERY_DUCKDB_SECRET_SOURCES
    src/query/duckdb/credential_secret.cpp
    src/query/duckdb/credential_storage.cpp
    src/query/duckdb/credential_provider_adapter.cpp
    src/query/duckdb/secret_integration.cpp)
set(QUERY_DUCKDB_ADAPTER_SUPPORT_SOURCES
    src/query/duckdb/complex_filter_adapter.cpp
    src/query/duckdb/relation_execution.cpp
    src/query/duckdb/scan_plan_explanation.cpp
    src/query/duckdb/table_function_plan_state.cpp
    src/query/duckdb/typed_value_adapter.cpp)
# Query's package-catalog boundary owns publication, generated registration,
# management, introspection, and database-lifetime coordination. Keeping this
# inventory separate prevents focused dispatcher consumers from compiling or
# reaching package-management implementation files.
set(QUERY_PACKAGE_CATALOG_SOURCES
    src/query/duckdb/catalog_generation_coordinator.cpp
    src/query/duckdb/generated_relation_adapter.cpp
    src/query/duckdb/package_catalog_snapshot.cpp
    src/query/duckdb/package_introspection_functions.cpp
    src/query/duckdb/package_lifecycle_sentry.cpp
    src/query/duckdb/package_management_functions.cpp)
set(QUERY_DUCKDB_ADAPTER_SOURCES
    ${QUERY_DUCKDB_ADAPTER_SUPPORT_SOURCES}
    ${QUERY_PACKAGE_CATALOG_SOURCES}
    src/query/duckdb/table_function_adapter.cpp
    src/query/duckdb/extension_entrypoint.cpp)
