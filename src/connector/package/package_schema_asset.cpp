#include "duckdb_api/internal/connector/package/package_compiler.hpp"

#include "duckdb_api/content_digest.hpp"

#include <string>

namespace duckdb_api {
namespace connector {

namespace {

const char CONNECTOR_PACKAGE_V1_SCHEMA[] =
#include "assets/connector-package-v1.schema.inc"
    ;

const char CONNECTOR_PACKAGE_V1_SCHEMA_DIGEST[] = "14d126bd0c540ba8c298480f71d8ba5c433943d4019b19c5e99ba29f0c8a4fdb";

} // namespace

const char *ConnectorPackageV1SchemaDigest() {
	return "sha256.14d126bd0c540ba8c298480f71d8ba5c433943d4019b19c5e99ba29f0c8a4fdb";
}

bool VerifyConnectorPackageV1SchemaAsset() {
	return ComputeSha256Hex(std::string(CONNECTOR_PACKAGE_V1_SCHEMA, sizeof(CONNECTOR_PACKAGE_V1_SCHEMA) - 1)) ==
	       CONNECTOR_PACKAGE_V1_SCHEMA_DIGEST;
}

} // namespace connector
} // namespace duckdb_api
