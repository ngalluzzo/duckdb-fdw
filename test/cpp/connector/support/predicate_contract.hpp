#pragma once

namespace duckdb_api_test {

// Runs the closed predicate declaration, relation-binding, conflict,
// immutability, and safe-explanation laws independently of native inventory.
void RunConnectorPredicateContractTests();

// Runs accepted proof-profile cross-field validation, including exact identity,
// base-domain, occurrence, and operation-scoped encoding counterexamples.
void RunConnectorPredicateProofContractTests();

} // namespace duckdb_api_test
