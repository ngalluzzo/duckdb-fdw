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

# Connector's narrow opaque custody provider. Runtime can retain and inspect a
# CompiledLocalPackage through its public generation API without linking YAML,
# source acquisition, schema decoding, or compiler implementation objects.
add_library(
  duckdb_api_local_package_custody_service STATIC
  ${CONNECTOR_LOCAL_PACKAGE_CUSTODY_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_local_package_custody_service)
target_link_libraries(
  duckdb_api_local_package_custody_service
  PUBLIC duckdb_api_connector_metadata_service)

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
  PUBLIC duckdb_api_local_package_custody_service
  PRIVATE duckdb_api_content_digest_service)

# Connector's compiler consumes a complete source snapshot and publishes one
# immutable generation. Source custody and metadata remain bounded services;
# consumers link this target instead of compiling implementation files.
add_library(
  duckdb_api_package_compiler_service STATIC
  ${CONNECTOR_PACKAGE_COMPILER_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_package_compiler_service)
target_link_libraries(
  duckdb_api_package_compiler_service
  PUBLIC duckdb_api_package_source_service
         duckdb_api_connector_metadata_service)

# Connector's author-evidence service derives the immutable coverage contract
# without source or execution access. Fixture custody and orchestration extend
# this target; consumers never compile Connector implementation sources.
add_library(
  duckdb_api_package_fixture_service STATIC
  ${CONNECTOR_PACKAGE_FIXTURE_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_package_fixture_service)
target_link_libraries(
  duckdb_api_package_fixture_service
  PUBLIC duckdb_api_package_compiler_service)
