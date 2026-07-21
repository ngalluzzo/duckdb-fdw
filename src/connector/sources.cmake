# Connector Experience owns this production inventory. Keep only immutable
# connector metadata and native catalog composition in this package.
set(CONNECTOR_CATALOG_SOURCES
    src/connector/catalog_model.cpp
    src/connector/catalog_snapshot.cpp
    src/connector/compiled_package_generation.cpp
    src/connector/graphql_operation_declaration.cpp
    src/connector/graphql_query_recipe.cpp
    src/connector/operation_selector.cpp
    src/connector/package_compatibility.cpp
    src/connector/package_semver.cpp
    src/connector/pagination_declaration.cpp
    src/connector/predicate_declaration.cpp
    src/connector/predicate_proof_profile.cpp
    src/connector/protocol_operation_declaration.cpp
    src/connector/resource_ceiling_declaration.cpp)
set(CONNECTOR_CONTENT_DIGEST_SOURCES
    src/connector/content_digest.cpp)
set(CONNECTOR_PACKAGE_YAML_SOURCES
    src/connector/package/failsafe_yaml.cpp
    src/connector/package/failsafe_yaml_lexical.cpp
    src/connector/package/failsafe_yaml_parser.cpp)
set(CONNECTOR_LOCAL_PACKAGE_CUSTODY_SOURCES
    src/connector/package/compiled_local_package.cpp
    src/connector/package/package_source_snapshot.cpp)
set(CONNECTOR_PACKAGE_SOURCE_SOURCES
    src/connector/package/package_digest.cpp
    src/connector/package/package_source.cpp
    src/connector/package/package_source_filesystem.cpp)
set(CONNECTOR_PACKAGE_COMPILER_SOURCES
    src/connector/package/package_compile_helpers.cpp
    src/connector/package/package_compiler.cpp
    src/connector/package/package_diagnostics.cpp
    src/connector/package/package_graphql_renderer.cpp
    src/connector/package/package_graphql_schema.cpp
    src/connector/package/package_http_schema.cpp
    src/connector/package/package_manifest_schema.cpp
    src/connector/package/package_model_compiler.cpp
    src/connector/package/package_operation_compiler.cpp
    src/connector/package/package_operation_schema.cpp
    src/connector/package/package_predicate_compiler.cpp
    src/connector/package/package_predicate_schema.cpp
    src/connector/package/package_relation_compiler.cpp
    src/connector/package/package_relation_schema.cpp
    src/connector/package/package_rest_schema.cpp
    src/connector/package/package_schema_asset.cpp
    src/connector/package/package_schema_helpers.cpp
    src/connector/package/package_schema_reader.cpp)
set(CONNECTOR_NATIVE_PROFILE_SOURCES
    src/connector/native_github_composition.cpp)
set(CONNECTOR_METADATA_IMPLEMENTATION_SOURCES
    ${CONNECTOR_CATALOG_SOURCES}
    ${CONNECTOR_NATIVE_PROFILE_SOURCES})
# Root product targets compose source inventories directly rather than linking
# package services, so their Connector inventory includes the neutral digest
# dependency. The focused metadata service below compiles only Connector-owned
# implementation and links the digest service instead.
set(CONNECTOR_METADATA_SOURCES
    ${CONNECTOR_CONTENT_DIGEST_SOURCES}
    ${CONNECTOR_METADATA_IMPLEMENTATION_SOURCES})
