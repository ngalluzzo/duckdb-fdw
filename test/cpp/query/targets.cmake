add_executable(
  duckdb_api_scan_request_tests
  test/cpp/query/scan_request_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_scan_request_tests)
target_include_directories(duckdb_api_scan_request_tests PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_scan_request_tests
  PRIVATE duckdb_api_connector_fixture_service
          duckdb_api_query_request_service)

add_executable(
  duckdb_api_typed_value_adapter_tests
  test/cpp/query/duckdb/typed_value_adapter_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_typed_value_adapter_tests)
target_include_directories(duckdb_api_typed_value_adapter_tests PRIVATE test/cpp src)
target_link_libraries(
  duckdb_api_typed_value_adapter_tests
  PRIVATE duckdb_api_query_typed_value_adapter_service
          dummy_static_extension_loader)

add_executable(
  duckdb_api_duckdb_secret_tests
  test/cpp/query/duckdb/duckdb_secret_tests.cpp
  test/cpp/query/duckdb/duckdb_secret_creation_tests.cpp
  test/cpp/query/duckdb/duckdb_secret_resolution_tests.cpp
  ${QUERY_SECRET_TEST_SUPPORT_SOURCES}
  ${QUERY_DUCKDB_ADAPTER_SUPPORT_SOURCES}
  src/query/duckdb/table_function_adapter.cpp)
target_link_libraries(
  duckdb_api_duckdb_secret_tests
  PRIVATE duckdb_api_relational_planning_service
          duckdb_api_semantics_fixture_service
          duckdb_api_query_credential_service
          duckdb_api_runtime_interface_service
          dummy_static_extension_loader
          duckdb_static
          Threads::Threads)
target_include_directories(duckdb_api_duckdb_secret_tests PRIVATE test/cpp src/query/duckdb)
configure_duckdb_api_cpp_target(duckdb_api_duckdb_secret_tests)

add_executable(
  duckdb_api_adapter_tests
  test/cpp/query/duckdb/duckdb_adapter_tests.cpp
  test/cpp/query/duckdb/duckdb_adapter_auth_bind_tests.cpp
  test/cpp/query/duckdb/duckdb_adapter_auth_lifecycle_tests.cpp
  test/cpp/query/duckdb/complex_filter_adapter_tests.cpp
  test/cpp/query/duckdb/predicate_candidate_translation_tests.cpp
  test/cpp/query/duckdb/table_function_plan_state_tests.cpp
  ${QUERY_DUCKDB_ADAPTER_SUPPORT_SOURCES}
  src/query/duckdb/table_function_adapter.cpp
  ${QUERY_ADAPTER_TEST_SUPPORT_SOURCES}
  ${QUERY_AUTH_ADAPTER_TEST_SUPPORT_SOURCES})
target_link_libraries(
  duckdb_api_adapter_tests
  PRIVATE duckdb_api_connector_fixture_service
          duckdb_api_query_credential_service
          duckdb_api_relational_planning_service
          duckdb_api_runtime_interface_service
          dummy_static_extension_loader
          duckdb_static
          Threads::Threads)
target_include_directories(duckdb_api_adapter_tests PRIVATE test/cpp src/query/duckdb)
configure_duckdb_api_cpp_target(duckdb_api_adapter_tests)

# Query's focused GraphQL boundary oracle composes only public provider
# services with the unchanged DuckDB registration path. Runtime's eventual
# controlled product remains the sole whole-graph execution target.
add_executable(
  duckdb_api_graphql_query_contract_tests
  test/cpp/query/duckdb/graphql_adapter_contract_tests.cpp
  ${QUERY_DUCKDB_ADAPTER_SUPPORT_SOURCES}
  src/query/duckdb/table_function_adapter.cpp
  ${QUERY_ADAPTER_TEST_SUPPORT_SOURCES})
target_link_libraries(
  duckdb_api_graphql_query_contract_tests
  PRIVATE duckdb_api_relational_planning_service
          duckdb_api_query_credential_service
          duckdb_api_runtime_interface_service
          dummy_static_extension_loader
          duckdb_static
          Threads::Threads)
target_include_directories(
  duckdb_api_graphql_query_contract_tests
  PRIVATE test/cpp src src/query/duckdb)
configure_duckdb_api_cpp_target(duckdb_api_graphql_query_contract_tests)

# Query's whole-product GraphQL oracle uses actual DuckDB registration and
# consumes only Runtime's named scenario service. Runtime owns all scripted
# protocol material and exposes only ScanExecutor plus safe counters/stages.
add_executable(
  duckdb_api_graphql_product_contract_tests
  test/cpp/query/integration/graphql_product_contract_tests.cpp
  ${QUERY_DUCKDB_ADAPTER_SUPPORT_SOURCES}
  src/query/duckdb/table_function_adapter.cpp)
target_link_libraries(
  duckdb_api_graphql_product_contract_tests
  PRIVATE duckdb_api_relational_planning_service
          duckdb_api_query_credential_service
          duckdb_api_runtime_controlled_service
          dummy_static_extension_loader
          duckdb_static
          Threads::Threads)
target_include_directories(
  duckdb_api_graphql_product_contract_tests
  PRIVATE test/cpp src src/query/duckdb)
configure_duckdb_api_cpp_target(duckdb_api_graphql_product_contract_tests)

add_executable(
  duckdb_api_adapter_stream_contract_tests
  test/cpp/query/duckdb/duckdb_adapter_stream_contract_tests.cpp
  ${QUERY_DUCKDB_ADAPTER_SUPPORT_SOURCES}
  src/query/duckdb/table_function_adapter.cpp
  ${QUERY_ADAPTER_TEST_SUPPORT_SOURCES})
target_link_libraries(
  duckdb_api_adapter_stream_contract_tests
  PRIVATE duckdb_api_relational_planning_service
          duckdb_api_query_credential_service
          duckdb_api_runtime_interface_service
          dummy_static_extension_loader
          duckdb_static
          Threads::Threads)
target_include_directories(duckdb_api_adapter_stream_contract_tests PRIVATE test/cpp)
configure_duckdb_api_cpp_target(duckdb_api_adapter_stream_contract_tests)

# Query's package surface oracle uses real DuckDB catalog transactions and
# only bounded public provider fixtures. Production catalog sources arrive
# through Query's service target, never through a consumer-side source list.
add_executable(
  duckdb_api_package_query_surface_tests
  ${QUERY_PACKAGE_TEST_SOURCES}
  ${QUERY_PACKAGE_TEST_SUPPORT_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_package_query_surface_tests)
target_include_directories(
  duckdb_api_package_query_surface_tests
  PRIVATE test/cpp src/query/duckdb)
target_compile_definitions(
  duckdb_api_package_query_surface_tests
  PRIVATE DUCKDB_API_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
  duckdb_api_package_query_surface_tests
  PRIVATE duckdb_api_query_package_catalog_service
          duckdb_api_package_compiler_fixture_service
          duckdb_api_package_generation_fixture_service
          duckdb_api_relational_planning_service
          duckdb_api_semantics_materialized_fixture_service
          dummy_static_extension_loader
          duckdb_static
          Threads::Threads)

# Query's reusable publication fixture owns real isolated DuckDB catalogs and
# exposes only closed scenarios plus safe aggregate observations. Whole-product
# fixture consumers link this service instead of constructing Query catalog or
# coordinator internals.
add_library(
  duckdb_api_query_package_fixture_publication_service STATIC
  ${QUERY_PACKAGE_FIXTURE_PUBLICATION_SERVICE_SOURCES}
  ${QUERY_PACKAGE_TEST_SUPPORT_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_query_package_fixture_publication_service)
target_include_directories(
  duckdb_api_query_package_fixture_publication_service
  PUBLIC test/cpp/query/service
  PRIVATE src/query/duckdb
          test/cpp)
target_link_libraries(
  duckdb_api_query_package_fixture_publication_service
  PRIVATE duckdb_api_query_package_catalog_service
          duckdb_api_package_compiler_fixture_service
          duckdb_api_package_generation_fixture_service
          duckdb_api_relational_planning_service
          dummy_static_extension_loader
          duckdb_static
          Threads::Threads)

add_executable(
  duckdb_api_query_package_fixture_publication_tests
  test/cpp/query/packages/query_fixture_publication_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_query_package_fixture_publication_tests)
target_include_directories(
  duckdb_api_query_package_fixture_publication_tests
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_query_package_fixture_publication_tests
  PRIVATE duckdb_api_query_package_fixture_publication_service)

# The lead-composition oracle exercises only public provider services and
# proves that real compiler custody, Semantics planning, Runtime registry
# admission, publication leases, no-op reload, and close form one generation.
add_executable(
  duckdb_api_package_generation_composition_tests
  test/cpp/query/package_generation_composition_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_package_generation_composition_tests)
target_include_directories(
  duckdb_api_package_generation_composition_tests
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_package_generation_composition_tests
  PRIVATE duckdb_api_package_generation_composition_service)

# Actual DuckDB composes the real compiler/planner/registry lifecycle with
# Runtime's named public scenarios. Query sees only ScanExecutor and safe
# observations; no provider-private source enters this consumer target.
add_executable(
  duckdb_api_package_product_contract_tests
  test/cpp/query/integration/package_product_contract_tests.cpp)
configure_duckdb_api_cpp_target(duckdb_api_package_product_contract_tests)
target_include_directories(
  duckdb_api_package_product_contract_tests
  PRIVATE test/cpp)
target_link_libraries(
  duckdb_api_package_product_contract_tests
  PRIVATE duckdb_api_package_generation_composition_service
          duckdb_api_package_compiler_fixture_service
          duckdb_api_query_package_catalog_service
          duckdb_api_runtime_controlled_service
          dummy_static_extension_loader
          duckdb_static
          Threads::Threads)

# RFC 0012 is accepted, but this opt-in executable remains bounded decision
# evidence rather than product code or a release gate. Its sources stay outside
# every production inventory.
add_executable(
  duckdb_api_rfc0012_native_coordinator_trial EXCLUDE_FROM_ALL
  test/cpp/query/rfc0012/native_coordinator_trial.cpp
  test/cpp/query/rfc0012/coordinator_trial_support.cpp)
target_link_libraries(
  duckdb_api_rfc0012_native_coordinator_trial
  PRIVATE dummy_static_extension_loader
          duckdb_static
          Threads::Threads)
target_include_directories(
  duckdb_api_rfc0012_native_coordinator_trial
  PRIVATE test/cpp)
configure_duckdb_api_cpp_target(duckdb_api_rfc0012_native_coordinator_trial)

# RFC 0026 decision evidence exercises DuckDB's real multi-connection adapter
# scheduling with one configured execution thread. It remains opt-in until the
# admission contract is accepted and production oracles replace the trial.
add_executable(
  duckdb_api_rfc0026_worker_isolation_trial EXCLUDE_FROM_ALL
  test/cpp/query/rfc0026/worker_isolation_trial.cpp
  ${QUERY_DUCKDB_ADAPTER_SUPPORT_SOURCES}
  src/query/duckdb/table_function_adapter.cpp
  ${QUERY_ADAPTER_TEST_SUPPORT_SOURCES}
  ${QUERY_AUTH_ADAPTER_TEST_SUPPORT_SOURCES})
target_link_libraries(
  duckdb_api_rfc0026_worker_isolation_trial
  PRIVATE duckdb_api_connector_fixture_service
          duckdb_api_query_credential_service
          duckdb_api_relational_planning_service
          duckdb_api_runtime_interface_service
          dummy_static_extension_loader
          duckdb_static
          Threads::Threads)
target_include_directories(
  duckdb_api_rfc0026_worker_isolation_trial
  PRIVATE test/cpp src/query/duckdb)
configure_duckdb_api_cpp_target(duckdb_api_rfc0026_worker_isolation_trial)
