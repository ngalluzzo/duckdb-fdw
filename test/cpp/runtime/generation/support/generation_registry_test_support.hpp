#pragma once

#include "connector/support/package_compiler_test_fixtures.hpp"
#include "duckdb_api/runtime_generation_registry.hpp"
#include "support/require.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace duckdb_api_test {

class ManualExecutionControl final : public duckdb_api::ExecutionControl {
public:
	ManualExecutionControl() : cancelled(false) {
	}

	bool IsCancellationRequested() const noexcept override {
		return cancelled.load(std::memory_order_acquire);
	}

	void Cancel() noexcept {
		cancelled.store(true, std::memory_order_release);
	}

private:
	std::atomic<bool> cancelled;
};

// Runtime tests consume Connector's real compiler fixture, then retain only
// public immutable package/decision values. The provider fixture and its
// temporary-path API are gone before Runtime staging begins, so no Runtime
// oracle imports source, YAML, or compiler-private construction knowledge.
class PreparedLocalPackageReload final {
public:
	explicit PreparedLocalPackageReload(const LocalPackageReloadFixture &fixture)
	    : active(fixture.Active()), candidate(fixture.Candidate()), decision(fixture.Decision()) {
	}

	PreparedLocalPackageReload(const PreparedLocalPackageReload &) = default;
	PreparedLocalPackageReload(PreparedLocalPackageReload &&) = default;
	PreparedLocalPackageReload &operator=(const PreparedLocalPackageReload &) = delete;
	PreparedLocalPackageReload &operator=(PreparedLocalPackageReload &&) = delete;

	duckdb_api::CompiledLocalPackage TakeActive() {
		return std::move(active);
	}

	duckdb_api::CompiledLocalPackage TakeCandidate() {
		return std::move(candidate);
	}

	const duckdb_api::CompiledLocalPackage &Active() const noexcept {
		return active;
	}

	const duckdb_api::CompiledLocalPackage &Candidate() const noexcept {
		return candidate;
	}

	const duckdb_api::PackageReloadDecision &Decision() const noexcept {
		return decision;
	}

private:
	duckdb_api::CompiledLocalPackage active;
	duckdb_api::CompiledLocalPackage candidate;
	duckdb_api::PackageReloadDecision decision;
};

inline PreparedLocalPackageReload PrepareLocalPackageReload(const std::string &repository_root,
                                                            LocalPackageReloadFixtureVariant variant) {
	const auto fixture = BuildRepositoryGithubLocalPackageReloadFixture(repository_root, variant);
	return PreparedLocalPackageReload(fixture);
}

template <class Callable>
void RequireGenerationFailure(Callable callable, duckdb_api::RuntimeGenerationFailure expected,
                              const std::string &message) {
	try {
		callable();
	} catch (const duckdb_api::RuntimeGenerationError &error) {
		Require(error.Failure() == expected, message + " (wrong failure)");
		return;
	}
	throw std::runtime_error(message);
}

inline void WaitUntil(const std::function<bool()> &condition, const std::string &message) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!condition()) {
		if (std::chrono::steady_clock::now() >= deadline) {
			throw std::runtime_error(message);
		}
		std::this_thread::yield();
	}
}

} // namespace duckdb_api_test
