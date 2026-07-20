# Connector Experience owns this production inventory. Keep only immutable
# connector metadata and native catalog composition in this package.
set(CONNECTOR_CATALOG_SOURCES
    src/connector/catalog_model.cpp
    src/connector/catalog_snapshot.cpp
    src/connector/compiled_package_generation.cpp
    src/connector/graphql_operation_declaration.cpp
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
