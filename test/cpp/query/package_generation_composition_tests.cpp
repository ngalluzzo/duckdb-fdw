#include "duckdb_api/package_generation_composition.hpp"

#include "support/require.hpp"

#include <cstdlib>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

using duckdb_api_test::Require;

class NeverCancelled final : public duckdb_api::ExecutionControl {
public:
	bool IsCancellationRequested() const noexcept override {
		return false;
	}
};

class CloseProbeExecutor final : public duckdb_api::ScanExecutor {
public:
	CloseProbeExecutor() : closed(false), registry_closed_before_executor(false) {
	}

	std::unique_ptr<duckdb_api::BatchStream> Open(const duckdb_api::ScanPlan &,
	                                              duckdb_api::ExecutionControl &) const override {
		throw std::logic_error("composition contract unexpectedly opened Runtime execution");
	}

	void Observe(const std::shared_ptr<const duckdb_api::QueryPackageStagingService> &staging_p, std::string root_p) {
		staging = staging_p;
		root = std::move(root_p);
	}

	void Close() const noexcept override {
		if (closed.exchange(true, std::memory_order_acq_rel)) {
			return;
		}
		auto observed = staging.lock();
		if (!observed) {
			return;
		}
		NeverCancelled control;
		try {
			(void)observed->StageLoad(root, control);
		} catch (const duckdb_api::QueryStagingError &error) {
			registry_closed_before_executor.store(error.Code() == "DUCKDB_API_PUBLICATION_CONFLICT" &&
			                                          error.Phase() == "publication",
			                                      std::memory_order_release);
		} catch (...) {
		}
	}

	bool ClosedInOrder() const noexcept {
		return closed.load(std::memory_order_acquire) &&
		       registry_closed_before_executor.load(std::memory_order_acquire);
	}

private:
	mutable std::atomic<bool> closed;
	mutable std::atomic<bool> registry_closed_before_executor;
	std::weak_ptr<const duckdb_api::QueryPackageStagingService> staging;
	std::string root;
};

void TestCompileStagePublishReloadAndClose(const std::string &repository_root) {
	auto executor = std::shared_ptr<CloseProbeExecutor>(new CloseProbeExecutor());
	auto staging = duckdb_api::BuildPackageGenerationComposition(executor);
	executor->Observe(staging, repository_root + "/connectors/github");
	NeverCancelled control;
	auto load = staging->StageLoad(repository_root + "/connectors/github", control);
	Require(load.Changed() && load.PublicationLease(), "real package load did not produce one changed Runtime lease");
	Require(load.Generation()->Registration().Identity().ConnectorId() == "github" &&
	            load.Generation()->Registration().Relations().size() == 4,
	        "real package load did not compose the compiler-produced GitHub generation");
	auto load_lease = load.TakePublicationLease();
	load_lease->Commit();

	auto reload = staging->StageReload("github", load.Generation(), control);
	Require(!reload.Changed() && !reload.PublicationLease() && reload.Generation() == load.Generation(),
	        "identical real package reload did not retain the exact published Query generation");

	staging->Close();
	staging->Close();
	Require(executor->ClosedInOrder(),
	        "package composition did not close Runtime generation admission before the shared executor");
	bool rejected = false;
	try {
		(void)staging->StageReload("github", load.Generation(), control);
	} catch (const duckdb_api::QueryStagingError &error) {
		rejected = error.Code() == "DUCKDB_API_PUBLICATION_CONFLICT" && error.Phase() == "publication" &&
		           error.File().empty() && !error.HasLineAndColumn() && error.YamlPath().empty();
	}
	Require(rejected, "closed package composition did not reject staging through the public publication diagnostic");
}

} // namespace

int main(int argc, char **argv) {
	try {
		if (argc != 2 || argv[1][0] != '/') {
			throw std::invalid_argument("usage: package_generation_composition_tests ABSOLUTE_REPOSITORY_ROOT");
		}
		TestCompileStagePublishReloadAndClose(argv[1]);
		std::cout << "package generation composition tests passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "package generation composition tests failed: " << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
