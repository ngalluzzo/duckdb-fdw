#include "duckdb_api/product_composition.hpp"

#include "duckdb_api/http_runtime.hpp"

#include <utility>

namespace duckdb_api {

ProductComposition BuildProductComposition() {
	auto connector = BuildNativeGithubConnector();
	auto runtime = InitializeHttpRuntime();
	return {std::move(connector), std::move(runtime.executor)};
}

} // namespace duckdb_api
