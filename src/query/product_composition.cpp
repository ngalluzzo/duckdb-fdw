#include "duckdb_api/product_composition.hpp"

#include "duckdb_api/http_runtime.hpp"
#include "duckdb_api/package_generation_composition.hpp"

#include <utility>

namespace duckdb_api {

ProductComposition BuildProductComposition() {
	auto connector = BuildNativeGithubConnector();
	auto runtime = InitializeHttpRuntime();
	auto package_staging = BuildPackageGenerationComposition(runtime.executor);
	return {std::move(connector), std::move(runtime.executor), std::move(package_staging)};
}

} // namespace duckdb_api
