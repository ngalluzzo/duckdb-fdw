# Query Experience owns DuckDB adapter probes and the controlled end-to-end
# composition. The composition consumes Runtime's public test service.
set(QUERY_ADAPTER_TEST_SUPPORT_SOURCES
    test/cpp/query/support/query_runtime_scenarios.cpp
    test/cpp/query/support/duckdb_adapter_test_support.cpp)
set(QUERY_AUTH_ADAPTER_TEST_SUPPORT_SOURCES
    test/cpp/query/support/duckdb_adapter_auth_test_support.cpp)
set(QUERY_SECRET_TEST_SUPPORT_SOURCES
    test/cpp/query/support/duckdb_secret_test_support.cpp)
set(QUERY_CONTROLLED_COMPOSITION_SOURCES
    test/cpp/runtime/support/loopback_curl_runtime.cpp
    test/cpp/query/integration/support/controlled_product_composition.cpp
    test/cpp/query/integration/controlled_extension_entrypoint.cpp)
