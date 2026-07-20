#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace duckdb {
class DuckDB;
}

namespace duckdb_api_test {
namespace rfc0012 {

class CatalogGenerationCoordinator;
class CoordinatorDatabaseLifecycle;
struct RegistryGeneration;

// Test-only control for the RFC 0012 native-catalog feasibility trial. The
// production coordinator must not expose pause or fault-injection authority.
class CoordinatorTrialControl final {
public:
	CoordinatorTrialControl();

	void PauseNextPublicationAfterPreflight();
	void PauseNextPublicationAfterMutation(std::uint64_t mutation);
	bool WaitForPublicationPause(std::chrono::milliseconds timeout);
	void ResumePublication();
	void BeginShutdown();
	void PauseNextRelationScan();
	bool WaitForRelationScanPause(std::chrono::milliseconds timeout);
	void ResumeRelationScan();
	void PauseRelationScanIfRequested();

	bool LastCandidateReleased() const;
	bool DatabaseShutdownObserved() const;
	std::uint64_t LiveGenerationCount() const;
	std::uint64_t WaitingPublicationCount() const;

private:
	friend class CatalogGenerationCoordinator;
	friend class CoordinatorDatabaseLifecycle;
	friend struct RegistryGeneration;
	friend std::shared_ptr<CoordinatorTrialControl> RegisterNativeCoordinatorTrial(duckdb::DuckDB &database);

	void PauseAfterPreflightIfRequested();
	void PauseAfterMutationIfRequested(std::uint64_t mutation);
	void ObserveCandidate(const std::shared_ptr<const RegistryGeneration> &candidate);
	void GenerationCreated();
	void GenerationDestroyed();
	void WaitingPublicationStarted();
	void WaitingPublicationEnded();
	void AttachCoordinator(const std::shared_ptr<CatalogGenerationCoordinator> &coordinator);
	void ObserveDatabaseShutdown();

	mutable std::mutex pause_mutex_;
	std::condition_variable pause_condition_;
	bool pause_requested_;
	std::uint64_t pause_after_mutation_;
	bool pause_reached_;
	bool resume_requested_;

	mutable std::mutex candidate_mutex_;
	std::weak_ptr<const RegistryGeneration> last_candidate_;
	std::atomic<std::uint64_t> live_generations_;
	std::atomic<std::uint64_t> waiting_publications_;
	std::atomic<bool> database_shutdown_observed_;
	std::weak_ptr<CatalogGenerationCoordinator> coordinator_;

	std::mutex scan_mutex_;
	std::condition_variable scan_condition_;
	bool scan_pause_requested_;
	bool scan_pause_reached_;
	bool scan_resume_requested_;
};

// Registers only the controlled management and introspection functions. Every
// relation generation is published later by the management table function in
// the caller's real system-catalog transaction.
std::shared_ptr<CoordinatorTrialControl> RegisterNativeCoordinatorTrial(duckdb::DuckDB &database);

// Adds an unrelated system table-function owner for deterministic collision
// testing. It is intentionally unavailable outside this trial target.
void RegisterUnrelatedSystemFunction(duckdb::DuckDB &database, const std::string &name);
void RegisterUnrelatedSystemOverload(duckdb::DuckDB &database, const std::string &name);

} // namespace rfc0012
} // namespace duckdb_api_test
