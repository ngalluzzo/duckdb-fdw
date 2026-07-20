#pragma once

#include "duckdb_api/connector_catalog.hpp"

#include <iosfwd>

namespace duckdb_api {
namespace internal {

// Connector-private dispatch for the permanent REST/GraphQL operation sum.
// It validates the selected payload exhaustively and supplies the typed HTTP
// destination used by relation authentication and catalog network checks.
// Consumers branch through the public const alternative and never receive a
// fallback, downcast, mutable request, or lifecycle hook.
void ValidateProtocolOperation(const CompiledOperation &operation);
const CompiledHttpOrigin &OperationOrigin(const CompiledOperation &operation);
void AppendProtocolOperation(std::ostream &result, const CompiledOperation &operation);

} // namespace internal
} // namespace duckdb_api
