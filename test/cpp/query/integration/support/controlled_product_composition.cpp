#include "query/integration/support/controlled_product_composition.hpp"

#include "connector/support/connector_catalog_test_fixtures.hpp"
#include "duckdb_api/package_generation_composition.hpp"
#include "runtime/support/loopback_curl_runtime.hpp"

#include <utility>

namespace duckdb_api_test {

duckdb_api::ProductComposition BuildControlledProductComposition(uint16_t port, bool predicate_mapping_available) {
	auto connector = predicate_mapping_available ? duckdb_api::BuildNativeGithubConnector()
	                                             : BuildPredicateMappingAbsentCatalogFixture();
	auto runtime = BuildLoopbackCurlRuntime(port);
	auto executor = runtime->Executor();
	auto package_staging = duckdb_api::BuildPackageGenerationComposition(executor);
	return {std::move(connector), std::move(executor), std::move(package_staging)};
}

} // namespace duckdb_api_test
