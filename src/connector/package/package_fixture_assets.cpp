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

const char *INDEX_DIGEST = "3d341f49be5b150f53fee68278643f0e35b29ff725b93e5cf3df215c09ff6133";
const char *COVERAGE_DIGEST = "e4ae603e800bf436dbe66a866f15eaba0c3f0f881fcb210e9a95bb8939172063";

} // namespace

const char *PackageFixtureIndexV1SchemaDigest() {
	return "sha256.3d341f49be5b150f53fee68278643f0e35b29ff725b93e5cf3df215c09ff6133";
}

const char *PackageFixtureCoverageV1MappingDigest() {
	return "sha256.e4ae603e800bf436dbe66a866f15eaba0c3f0f881fcb210e9a95bb8939172063";
}

bool VerifyPackageFixtureContractAssets() {
	return ComputeSha256Hex(std::string(FIXTURE_INDEX_V1_SCHEMA, sizeof(FIXTURE_INDEX_V1_SCHEMA) - 1)) ==
	           INDEX_DIGEST &&
	       ComputeSha256Hex(std::string(FIXTURE_COVERAGE_V1_MAPPING, sizeof(FIXTURE_COVERAGE_V1_MAPPING) - 1)) ==
	           COVERAGE_DIGEST;
}

} // namespace connector
} // namespace duckdb_api
