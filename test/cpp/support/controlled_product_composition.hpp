#pragma once

#include "duckdb_api/product_composition.hpp"

#include <cstdint>

namespace duckdb_api_test {

// Builds the private, non-installable product from Runtime's typed loopback
// connector and executor service. Query neither mutates that connector nor
// learns the transport profile; the production adapter performs the unchanged
// request construction, planning, bind-copy, and scan path.
duckdb_api::ProductComposition BuildControlledProductComposition(uint16_t port);

} // namespace duckdb_api_test
