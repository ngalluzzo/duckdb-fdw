#include "connector/support/package_compiler_test_fixtures.hpp"

#include "support/require.hpp"

#include <iostream>

int main(int argc, char **argv) {
	try {
		duckdb_api_test::Require(argc == 2, "usage: package_compiler_fixture_tests ABSOLUTE_REPOSITORY_ROOT");
		const auto registration = duckdb_api_test::CompileRepositoryGithubRegistrationFixture(argv[1]);
		duckdb_api_test::Require(registration.Identity().ConnectorId() == "github" &&
		                             registration.Relations().size() == 4 &&
		                             registration.Relations()[0].Name() == "duckdb_login_search_page" &&
		                             registration.Relations()[1].Name() == "authenticated_user" &&
		                             registration.Relations()[2].Name() == "authenticated_repositories" &&
		                             registration.Relations()[3].Name() == "viewer_repository_metrics" &&
		                             registration.GenerationHandle().IsValid(),
		                         "Connector compiler fixture did not expose the real four-relation registration view");
		std::cout << "package compiler fixture tests passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
