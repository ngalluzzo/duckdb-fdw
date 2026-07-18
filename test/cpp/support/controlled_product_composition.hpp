#pragma once

#include "duckdb_api/product_composition.hpp"

#include <cstdint>

namespace duckdb_api_test {

// Builds the private, non-installable product from Runtime's typed loopback
// connector and anonymous-or-bearer executor service. Query neither mutates
// that connector nor learns the transport or credential profile; real DuckDB
// secret resolution and the production adapter perform the unchanged
// request-planning, bind-copy, authorization-envelope, and scan path.
duckdb_api::ProductComposition BuildControlledProductComposition(uint16_t port);

} // namespace duckdb_api_test
