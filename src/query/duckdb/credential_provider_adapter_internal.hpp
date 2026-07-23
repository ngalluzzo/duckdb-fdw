#pragma once

namespace duckdb {

class ExtensionLoader;

namespace duckdb_api_query_internal {

void RegisterDuckdbApiCredentialProviders(ExtensionLoader &loader);

} // namespace duckdb_api_query_internal
} // namespace duckdb
