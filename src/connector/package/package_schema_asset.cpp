#include "duckdb_api/internal/connector/package/package_compiler.hpp"

#include "duckdb_api/content_digest.hpp"

#include <string>

namespace duckdb_api {
namespace connector {

namespace {

const char CONNECTOR_PACKAGE_V1_SCHEMA[] =
#include "assets/connector-package-v1.schema.inc"
    ;

const char CONNECTOR_PACKAGE_V1_SCHEMA_DIGEST[] = "589774ff75876c13d6bd52a243fd470c172e67d8121bc6ba8f11e2af0b451d41";

} // namespace

const char *ConnectorPackageV1SchemaDigest() {
	return "sha256.589774ff75876c13d6bd52a243fd470c172e67d8121bc6ba8f11e2af0b451d41";
}

bool VerifyConnectorPackageV1SchemaAsset() {
	return ComputeSha256Hex(std::string(CONNECTOR_PACKAGE_V1_SCHEMA, sizeof(CONNECTOR_PACKAGE_V1_SCHEMA) - 1)) ==
	       CONNECTOR_PACKAGE_V1_SCHEMA_DIGEST;
}

} // namespace connector
} // namespace duckdb_api
