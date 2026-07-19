#include "support/controlled_product_composition.hpp"

#include "support/loopback_curl_runtime.hpp"

#include <utility>

namespace duckdb_api_test {

duckdb_api::ProductComposition BuildControlledProductComposition(uint16_t port) {
	auto connector = duckdb_api::BuildNativeGithubConnector();
	auto runtime = BuildLoopbackCurlRuntime(port);
	return {std::move(connector), runtime->Executor()};
}

} // namespace duckdb_api_test
