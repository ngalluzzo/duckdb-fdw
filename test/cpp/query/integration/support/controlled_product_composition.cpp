#include "query/integration/support/controlled_product_composition.hpp"

#include "connector/support/connector_catalog_test_fixtures.hpp"
#include "runtime/support/loopback_curl_runtime.hpp"

#include <utility>

namespace duckdb_api_test {

duckdb_api::ProductComposition BuildControlledProductComposition(uint16_t port, bool predicate_mapping_available) {
	auto connector = predicate_mapping_available ? duckdb_api::BuildNativeGithubConnector()
	                                             : BuildPredicateMappingAbsentCatalogFixture();
	auto runtime = BuildLoopbackCurlRuntime(port);
	return {std::move(connector), runtime->Executor()};
}

} // namespace duckdb_api_test
