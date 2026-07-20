#include "connector/support/package_compiler_test_fixtures.hpp"

#include "duckdb_api/internal/connector/package/package_compiler.hpp"

#include <stdexcept>
#include <string>

namespace duckdb_api_test {

namespace {

class NeverCancel final : public duckdb_api::connector::PackageCancellation {
public:
	bool IsCancellationRequested() const noexcept override {
		return false;
	}
};

} // namespace

duckdb_api::CompiledQueryRegistrationView
CompileRepositoryGithubRegistrationFixture(const std::string &absolute_repository_root) {
	NeverCancel cancellation;
	const auto result = duckdb_api::connector::CompileLocalPackageRoot(
	    absolute_repository_root + "/docs/rfcs/evidence/0013/github", cancellation);
	if (!result.Succeeded() || result.Generation() == nullptr) {
		throw std::runtime_error("repository GitHub connector package fixture did not compile");
	}
	return result.Generation()->QueryRegistration();
}

} // namespace duckdb_api_test
