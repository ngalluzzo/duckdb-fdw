#pragma once

#include "duckdb_api/query_generation.hpp"

#include <atomic>
#include <memory>
#include <mutex>

namespace duckdb {
class ClientContext;

namespace duckdb_api_query_internal {

class PackageCatalogSnapshot;

enum class PackagePublicationIntent { LOAD, RELOAD };

// Query's DatabaseInstance-scoped publication serializer. Staging is complete
// before this service is called. It owns collision checks and one system-
// catalog transaction, holds its guard through commit/rollback, and never
// exposes a mutable registry separate from DuckDB catalog MVCC.
class CatalogGenerationCoordinator final : public std::enable_shared_from_this<CatalogGenerationCoordinator> {
public:
	explicit CatalogGenerationCoordinator(std::shared_ptr<const duckdb_api::QueryPackageStagingService> staging);

	void RecordManagementBind(ClientContext &context);
	void Publish(ClientContext &context, const std::shared_ptr<const PackageCatalogSnapshot> &base,
	             duckdb_api::QueryStagedGeneration &staged, PackagePublicationIntent intent);

	const std::shared_ptr<const duckdb_api::QueryPackageStagingService> &Staging() const noexcept;
	void BeginClose() noexcept;
	bool IsClosing() const noexcept;

private:
	std::shared_ptr<const duckdb_api::QueryPackageStagingService> staging;
	std::timed_mutex publication_mutex;
	std::atomic<bool> closing {false};
};

} // namespace duckdb_api_query_internal
} // namespace duckdb
