# Connector Experience's immutable production service. Consumers link this
# target and include the public facade; they do not compile Connector sources.
add_library(
  duckdb_api_connector_metadata_service STATIC
  ${CONNECTOR_METADATA_SOURCES})
configure_duckdb_api_cpp_target(duckdb_api_connector_metadata_service)
