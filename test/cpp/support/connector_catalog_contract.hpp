#pragma once

namespace duckdb_api_test {

// Runs representation-level immutability, credential-policy, structural URL,
// and validation counterexamples independently of the native GitHub catalog.
void RunConnectorCatalogContractTests();

} // namespace duckdb_api_test
