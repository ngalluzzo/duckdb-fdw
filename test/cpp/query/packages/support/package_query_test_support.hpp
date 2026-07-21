#pragma once

#include "duckdb_api/compiled_package_generation.hpp"
#include "duckdb_api/query_generation.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace duckdb {
class Connection;
class DuckDB;
namespace duckdb_api_query_internal {
class CatalogGenerationCoordinator;
}
} // namespace duckdb

namespace duckdb_api_test {

struct PackageQueryProbe final {
	PackageQueryProbe();

	std::atomic<std::uint64_t> load_stages;
	std::atomic<std::uint64_t> reload_stages;
	std::atomic<std::uint64_t> plans;
	std::atomic<std::uint64_t> streams_opened;
	std::atomic<std::uint64_t> streams_closed;
	std::atomic<std::uint64_t> rows;
	std::atomic<std::uint64_t> generation_owners_destroyed;
	std::atomic<std::uint64_t> publication_commits;
	std::atomic<std::uint64_t> publication_discards;
};

// Honest Query consumer double: executable cases supply complete immutable
// Connector generations. Registration-only cases deliberately install a
// planning trap, proving catalog publication and introspection do not cross the
// planning port. Neither mode constructs or mutates provider descriptors.
class PackageQueryStagingService final : public duckdb_api::QueryPackageStagingService {
public:
	PackageQueryStagingService(duckdb_api::CompiledQueryRegistrationView initial_registration,
	                           duckdb_api::CompiledConnector initial_connector,
	                           duckdb_api::CompiledQueryRegistrationView replacement_registration,
	                           duckdb_api::CompiledConnector replacement_connector, std::string accepted_root,
	                           std::shared_ptr<PackageQueryProbe> probe);
	PackageQueryStagingService(duckdb_api::CompiledQueryRegistrationView initial_registration,
	                           duckdb_api::CompiledQueryRegistrationView replacement_registration,
	                           std::string accepted_root, std::shared_ptr<PackageQueryProbe> probe);

	duckdb_api::QueryStagedGeneration StageLoad(const std::string &absolute_root,
	                                            duckdb_api::ExecutionControl &control) const override;
	duckdb_api::QueryStagedGeneration
	StageReload(const std::string &connector, const std::shared_ptr<const duckdb_api::QueryPublishedGeneration> &active,
	            duckdb_api::ExecutionControl &control) const override;

	void SetReloadChanged(bool changed) noexcept;
	std::weak_ptr<const duckdb_api::QueryPublishedGeneration> LastCandidate() const;

private:
	std::shared_ptr<const duckdb_api::QueryPublishedGeneration>
	BuildPublished(const std::shared_ptr<const duckdb_api::CompiledQueryRegistrationView> &registration,
	               const std::shared_ptr<const duckdb_api::CompiledConnector> &connector,
	               const std::string &marker) const;

	const std::shared_ptr<const duckdb_api::CompiledQueryRegistrationView> initial_registration;
	const std::shared_ptr<const duckdb_api::CompiledConnector> initial_connector;
	const std::shared_ptr<const duckdb_api::CompiledQueryRegistrationView> replacement_registration;
	const std::shared_ptr<const duckdb_api::CompiledConnector> replacement_connector;
	const std::string accepted_root;
	const std::shared_ptr<PackageQueryProbe> probe;
	mutable std::atomic<bool> reload_changed;
	mutable std::mutex candidate_mutex;
	mutable std::weak_ptr<const duckdb_api::QueryPublishedGeneration> last_candidate;
};

std::shared_ptr<PackageQueryStagingService>
BuildCompatibilityPackageQueryStaging(const std::string &absolute_root,
                                      const std::shared_ptr<PackageQueryProbe> &probe);
std::shared_ptr<PackageQueryStagingService>
BuildGithubPackageQueryStaging(const std::string &absolute_repository_root,
                               const std::shared_ptr<PackageQueryProbe> &probe);

std::shared_ptr<duckdb::duckdb_api_query_internal::CatalogGenerationCoordinator>
RegisterPackageQuerySurface(duckdb::DuckDB &database,
                            const std::shared_ptr<const duckdb_api::QueryPackageStagingService> &staging);

std::string PackageQueryError(duckdb::Connection &connection, const std::string &sql);
void RequirePackageQuerySuccess(duckdb::Connection &connection, const std::string &sql);

} // namespace duckdb_api_test
