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

const char CONNECTOR_PACKAGE_V2_SCHEMA[] =
#include "assets/connector-package-v2.schema.inc"
    ;

const char CONNECTOR_PACKAGE_V2_SCHEMA_DIGEST[] = "cc932d2db340f8de6ad462749287177ac6ad54879742d30db3d8eb5df07e10c5";

const char CONNECTOR_PACKAGE_V3_SCHEMA[] =
#include "assets/connector-package-v3.schema.inc"
    ;

const char CONNECTOR_PACKAGE_V3_SCHEMA_DIGEST[] = "1f076cd2c6f186d6bd005ae93ecbbec24f17d466a4000b575ac9d2000569ad25";

} // namespace

const char *ConnectorPackageV1SchemaDigest() {
	return "sha256.589774ff75876c13d6bd52a243fd470c172e67d8121bc6ba8f11e2af0b451d41";
}

bool VerifyConnectorPackageV1SchemaAsset() {
	return ComputeSha256Hex(std::string(CONNECTOR_PACKAGE_V1_SCHEMA, sizeof(CONNECTOR_PACKAGE_V1_SCHEMA) - 1)) ==
	       CONNECTOR_PACKAGE_V1_SCHEMA_DIGEST;
}

const char *ConnectorPackageV2SchemaDigest() {
	return "sha256.cc932d2db340f8de6ad462749287177ac6ad54879742d30db3d8eb5df07e10c5";
}

bool VerifyConnectorPackageV2SchemaAsset() {
	return ComputeSha256Hex(std::string(CONNECTOR_PACKAGE_V2_SCHEMA, sizeof(CONNECTOR_PACKAGE_V2_SCHEMA) - 1)) ==
	       CONNECTOR_PACKAGE_V2_SCHEMA_DIGEST;
}

const char *ConnectorPackageV3SchemaDigest() {
	return "sha256.1f076cd2c6f186d6bd005ae93ecbbec24f17d466a4000b575ac9d2000569ad25";
}

bool VerifyConnectorPackageV3SchemaAsset() {
	return ComputeSha256Hex(std::string(CONNECTOR_PACKAGE_V3_SCHEMA, sizeof(CONNECTOR_PACKAGE_V3_SCHEMA) - 1)) ==
	       CONNECTOR_PACKAGE_V3_SCHEMA_DIGEST;
}

} // namespace connector
} // namespace duckdb_api
