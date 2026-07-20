# Protocol-neutral digest service. Runtime may recompute admitted document
# bytes by linking this target without acquiring Connector metadata authority.
add_library(
  duckdb_api_content_digest_service STATIC
  ${CONNECTOR_CONTENT_DIGEST_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_content_digest_service)

# Connector Experience's immutable production service. Consumers link this
# target and include the public facade; they do not compile Connector sources.
add_library(
  duckdb_api_connector_metadata_service STATIC
  ${CONNECTOR_METADATA_IMPLEMENTATION_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_connector_metadata_service)
target_link_libraries(
  duckdb_api_connector_metadata_service
  PUBLIC duckdb_api_content_digest_service)

# Connector's source-custody service acquires and parses package bytes without
# compiling or publishing a generation. Keeping it separate from metadata
# makes the later compiler and Query publication boundaries explicit.
add_library(
  duckdb_api_package_source_service STATIC
  ${CONNECTOR_PACKAGE_YAML_SOURCES}
  ${CONNECTOR_PACKAGE_SOURCE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_package_source_service)
target_link_libraries(
  duckdb_api_package_source_service
  PRIVATE duckdb_api_content_digest_service)
