#include "duckdb_api/package_generation_composition.hpp"

#include "support/require.hpp"

#include <cstdlib>
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

class UnopenedExecutor final : public duckdb_api::ScanExecutor {
public:
	std::unique_ptr<duckdb_api::BatchStream> Open(const duckdb_api::ScanPlan &,
	                                              duckdb_api::ExecutionControl &) const override {
		throw std::logic_error("composition contract unexpectedly opened Runtime execution");
	}
};

void TestCompileStagePublishReloadAndClose(const std::string &repository_root) {
	auto staging = duckdb_api::BuildPackageGenerationComposition(
	    std::shared_ptr<const duckdb_api::ScanExecutor>(new UnopenedExecutor()));
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
