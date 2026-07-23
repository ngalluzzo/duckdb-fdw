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

const char *INDEX_DIGEST = "fae0e6f22ca0c8ed2ea8ffae61a4ac7d982907be1ce033270a93f0024cc04841";
const char *COVERAGE_DIGEST = "6107a7131196bfcfb359b410b10f28c2f20f3ca3ddbc690bc72b874cd744e5da";

} // namespace

const char *PackageFixtureIndexV1SchemaDigest() {
	return "sha256.fae0e6f22ca0c8ed2ea8ffae61a4ac7d982907be1ce033270a93f0024cc04841";
}

const char *PackageFixtureCoverageV1MappingDigest() {
	return "sha256.6107a7131196bfcfb359b410b10f28c2f20f3ca3ddbc690bc72b874cd744e5da";
}

bool VerifyPackageFixtureContractAssets() {
	return ComputeSha256Hex(std::string(FIXTURE_INDEX_V1_SCHEMA, sizeof(FIXTURE_INDEX_V1_SCHEMA) - 1)) ==
	           INDEX_DIGEST &&
	       ComputeSha256Hex(std::string(FIXTURE_COVERAGE_V1_MAPPING, sizeof(FIXTURE_COVERAGE_V1_MAPPING) - 1)) ==
	           COVERAGE_DIGEST;
}

} // namespace connector
} // namespace duckdb_api
