#pragma once

namespace duckdb_api_test {

// Runs protocol-sum, canonical GraphQL identity/digest, exact profile,
// schema/nullability, cursor/resource, and safe snapshot laws.
void RunConnectorGraphqlContractTests();

} // namespace duckdb_api_test
