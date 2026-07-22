#include "duckdb_api/internal/connector/package/package_compiler.hpp"

#include "duckdb_api/content_digest.hpp"

#include <string>

namespace duckdb_api {
namespace connector {

namespace {

const char CONNECTOR_PACKAGE_V1_SCHEMA[] =
#include "assets/connector-package-v1.schema.inc"
    ;

const char CONNECTOR_PACKAGE_V1_SCHEMA_DIGEST[] = "d1b7d022b028d48eb1e6a25912b19a8cdba9a56b0470598a057dfe08e6490fee";

} // namespace

const char *ConnectorPackageV1SchemaDigest() {
	return "sha256.d1b7d022b028d48eb1e6a25912b19a8cdba9a56b0470598a057dfe08e6490fee";
}

bool VerifyConnectorPackageV1SchemaAsset() {
	return ComputeSha256Hex(std::string(CONNECTOR_PACKAGE_V1_SCHEMA, sizeof(CONNECTOR_PACKAGE_V1_SCHEMA) - 1)) ==
	       CONNECTOR_PACKAGE_V1_SCHEMA_DIGEST;
}

} // namespace connector
} // namespace duckdb_api
