#include "duckdb_api/internal/connector/package/package_compiler.hpp"

#include "duckdb_api/content_digest.hpp"

#include <string>

namespace duckdb_api {
namespace connector {

namespace {

const char CONNECTOR_PACKAGE_V1_SCHEMA[] =
#include "assets/connector-package-v1.schema.inc"
    ;

const char CONNECTOR_PACKAGE_V1_SCHEMA_DIGEST[] = "22f02d9627e6185bbe1eb40b1a4fa25bc34871df7c65cea53c3cd6f9c977da64";

} // namespace

const char *ConnectorPackageV1SchemaDigest() {
	return "sha256.22f02d9627e6185bbe1eb40b1a4fa25bc34871df7c65cea53c3cd6f9c977da64";
}

bool VerifyConnectorPackageV1SchemaAsset() {
	return ComputeSha256Hex(std::string(CONNECTOR_PACKAGE_V1_SCHEMA, sizeof(CONNECTOR_PACKAGE_V1_SCHEMA) - 1)) ==
	       CONNECTOR_PACKAGE_V1_SCHEMA_DIGEST;
}

} // namespace connector
} // namespace duckdb_api
