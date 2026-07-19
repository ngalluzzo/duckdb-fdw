#pragma once

#include "duckdb_api/product_composition.hpp"

#include <cstdint>

namespace duckdb_api_test {

// Builds the private, non-installable product by assembling Connector's native
// catalog and Runtime's executor-only loopback service. Query neither mutates
// the catalog nor learns the transport or credential profile; real DuckDB
// secret resolution and the production adapter perform the unchanged
// request-planning, bind-copy, authorization-envelope, and scan path.
duckdb_api::ProductComposition BuildControlledProductComposition(uint16_t port, bool predicate_mapping_available);

} // namespace duckdb_api_test
