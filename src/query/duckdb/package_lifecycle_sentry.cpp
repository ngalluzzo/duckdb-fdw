#include "duckdb_api_extension.hpp"

#include "catalog_generation_coordinator.hpp"
#include "package_lifecycle_sentry.hpp"
#include "package_catalog_snapshot.hpp"
#include "package_introspection_functions.hpp"
#include "package_management_functions.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/planner/extension_callback.hpp"
#include "duckdb_api/query_generation.hpp"

#include <memory>
#include <utility>

namespace duckdb {
namespace {

// DuckDB 1.5.4 retains DatabaseInstance through every connection and active
// query. ExtensionCallback destruction is therefore the pinned lifecycle hook
// reached only after publication work is quiescent. It closes the coordinator
// before releasing the DatabaseInstance-owned reference; no DSO-unload claim
// is made.
class PackageCatalogLifecycleSentry final : public ExtensionCallback {
public:
	explicit PackageCatalogLifecycleSentry(
	    std::shared_ptr<duckdb_api_query_internal::CatalogGenerationCoordinator> coordinator_p)
	    : coordinator(std::move(coordinator_p)) {
	}

	~PackageCatalogLifecycleSentry() override {
		coordinator->BeginClose();
	}

private:
	std::shared_ptr<duckdb_api_query_internal::CatalogGenerationCoordinator> coordinator;
};

} // namespace

std::shared_ptr<duckdb_api_query_internal::CatalogGenerationCoordinator>
duckdb_api_query_internal::RegisterPackageSurfaceInternal(
    ExtensionLoader &loader, std::shared_ptr<const duckdb_api::QueryPackageStagingService> staging) {
	if (!staging) {
		throw InternalException("duckdb_api package registration requires a staging service");
	}
	auto coordinator = std::make_shared<duckdb_api_query_internal::CatalogGenerationCoordinator>(std::move(staging));
	auto snapshot = std::make_shared<const duckdb_api_query_internal::PackageCatalogSnapshot>();
	loader.RegisterFunction(duckdb_api_query_internal::BuildLoadConnectorFunction(coordinator, snapshot));
	loader.RegisterFunction(duckdb_api_query_internal::BuildReloadConnectorFunction(coordinator, snapshot));
	loader.RegisterFunction(duckdb_api_query_internal::BuildLoadedConnectorsFunction(coordinator, snapshot));
	loader.RegisterFunction(duckdb_api_query_internal::BuildLoadedRelationsFunction(coordinator, snapshot));
	loader.RegisterFunction(duckdb_api_query_internal::BuildRelationArgumentsFunction(coordinator, snapshot));
	ExtensionCallback::Register(loader.GetDatabaseInstance().config,
	                            make_shared_ptr<PackageCatalogLifecycleSentry>(coordinator));
	return coordinator;
}

void RegisterDuckdbApiPackageSurface(ExtensionLoader &loader,
                                     std::shared_ptr<const duckdb_api::QueryPackageStagingService> staging) {
	(void)duckdb_api_query_internal::RegisterPackageSurfaceInternal(loader, std::move(staging));
}

} // namespace duckdb
