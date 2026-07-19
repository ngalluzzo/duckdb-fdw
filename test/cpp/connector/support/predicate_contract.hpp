#pragma once

namespace duckdb_api_test {

// Runs the closed predicate declaration, relation-binding, conflict,
// immutability, and safe-explanation laws independently of native inventory.
void RunConnectorPredicateContractTests();

} // namespace duckdb_api_test
