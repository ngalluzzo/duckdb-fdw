#pragma once

#include "duckdb_api/internal/fixture_runtime.hpp"

#include <vector>

namespace duckdb_api {
namespace internal {

// Decode the one supported response shape while using the buffer as the shared
// cancellation, deadline, and resource checkpoint.
std::vector<ItemRow> DecodeFixtureItems(FixtureReadBuffer &buffer, const ResourceBudgets &budgets);

} // namespace internal
} // namespace duckdb_api
