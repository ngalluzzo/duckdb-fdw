# Connector Experience owns this production inventory. Keep only immutable
# connector metadata and native catalog composition in this package.
set(CONNECTOR_CATALOG_SOURCES
    src/connector/catalog_model.cpp
    src/connector/pagination_declaration.cpp
    src/connector/predicate_declaration.cpp
    src/connector/resource_ceiling_declaration.cpp)
set(CONNECTOR_NATIVE_PROFILE_SOURCES
    src/connector/native_github_composition.cpp)
set(CONNECTOR_METADATA_SOURCES
    ${CONNECTOR_CATALOG_SOURCES}
    ${CONNECTOR_NATIVE_PROFILE_SOURCES})
