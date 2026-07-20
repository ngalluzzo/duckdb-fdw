#pragma once

namespace duckdb_api {
namespace connector {

// Call-scoped cancellation view for bounded local package work. Connector
// never retains the view. Implementations must be safe to query repeatedly;
// cancellation is terminal for the current acquisition or parse and is
// reported by that operation's own typed error boundary.
class PackageCancellation {
public:
	virtual ~PackageCancellation() noexcept {
	}
	virtual bool IsCancellationRequested() const noexcept = 0;
};

} // namespace connector
} // namespace duckdb_api
