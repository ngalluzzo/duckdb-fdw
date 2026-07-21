#include "duckdb_api/package_fixture_runner.hpp"

#include "duckdb_api/content_digest.hpp"

#include <string>

namespace duckdb_api {
namespace connector {
namespace {

const char FIXTURE_INDEX_V1_SCHEMA[] =
#include "assets/fixture-index-v1.schema.inc"
    ;

const char FIXTURE_COVERAGE_V1_MAPPING[] =
#include "assets/fixture-coverage-v1.inc"
    ;

const char *INDEX_DIGEST = "3dd9ec84a5e0cbaddf23dba187e983d80aecfda83d8075c1109f6f032b0cf7c9";
const char *COVERAGE_DIGEST = "8a19c50eaa87e3655ae8c5f2a5511bfc6d7cfa3499e9f14583af40c1c54f36f8";

} // namespace

const char *PackageFixtureIndexV1SchemaDigest() {
	return "sha256.3dd9ec84a5e0cbaddf23dba187e983d80aecfda83d8075c1109f6f032b0cf7c9";
}

const char *PackageFixtureCoverageV1MappingDigest() {
	return "sha256.8a19c50eaa87e3655ae8c5f2a5511bfc6d7cfa3499e9f14583af40c1c54f36f8";
}

bool VerifyPackageFixtureContractAssets() {
	return ComputeSha256Hex(std::string(FIXTURE_INDEX_V1_SCHEMA, sizeof(FIXTURE_INDEX_V1_SCHEMA) - 1)) ==
	           INDEX_DIGEST &&
	       ComputeSha256Hex(std::string(FIXTURE_COVERAGE_V1_MAPPING, sizeof(FIXTURE_COVERAGE_V1_MAPPING) - 1)) ==
	           COVERAGE_DIGEST;
}

} // namespace connector
} // namespace duckdb_api
