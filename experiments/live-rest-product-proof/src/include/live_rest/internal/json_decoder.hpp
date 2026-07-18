#pragma once

#include "live_rest/runtime.hpp"

#include <string>
#include <vector>

namespace live_rest {
namespace internal {

// Decode one already bounded response body into the immutable plan's fixed
// row shape. The decoder owns JSON validity, exact conversion, required-field
// uniqueness, row/string budgets, and cancellation checkpoints; it has no
// transport authority and never handles URLs, headers, or HTTP status.
std::vector<LiveRow> DecodeResponseRows(const std::string &body, const LiveScanPlan &plan,
                                        const CancellationView &cancellation);

} // namespace internal
} // namespace live_rest
