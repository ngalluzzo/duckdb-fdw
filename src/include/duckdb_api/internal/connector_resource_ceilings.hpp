#pragma once

#include <iosfwd>

namespace duckdb_api {

class CompiledResourceCeilings;

namespace internal {

// Connector-private intrinsic value service. Relation construction separately
// owns compatibility between these source declarations and an operation's
// pagination profile. No execution counter or enforcement state enters here.
void ValidateResourceCeilingsValue(const CompiledResourceCeilings &ceilings);
void AppendResourceCeilings(std::ostream &result, const CompiledResourceCeilings &ceilings);

} // namespace internal
} // namespace duckdb_api
