#include "duckdb_api/product_composition.hpp"

#include "duckdb_api/http_runtime.hpp"

#include <utility>

namespace duckdb_api {

ProductComposition BuildProductComposition() {
	ProductComposition result;
	result.connector = BuildNativeGithubConnector();
	auto runtime = InitializeHttpRuntime();
	result.executor = std::move(runtime.executor);
	return result;
}

} // namespace duckdb_api
