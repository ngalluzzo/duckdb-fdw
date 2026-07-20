#include "connector/support/package_compiler_test_fixtures.hpp"

#include "support/require.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

using duckdb_api::CompiledRegistrationAuthentication;
using duckdb_api::CompiledRegistrationRelation;
using duckdb_api::CompiledScalarType;
using duckdb_api_test::Require;

struct ExpectedColumn {
	const char *name;
	CompiledScalarType type;
	bool nullable;
};

void RequireRelation(const CompiledRegistrationRelation &relation, const char *name,
                     CompiledRegistrationAuthentication authentication,
                     const std::vector<ExpectedColumn> &expected_columns) {
	Require(relation.Name() == name, std::string("registration relation order/name drifted at ") + name);
	Require(relation.Inputs().empty(), std::string("registration unexpectedly exposed relation inputs for ") + name);
	Require(relation.Authentication() == authentication,
	        std::string("registration authentication shape drifted for ") + name);
	Require(relation.Columns().size() == expected_columns.size(),
	        std::string("registration column count drifted for ") + name);
	for (std::size_t index = 0; index < expected_columns.size(); index++) {
		const auto &actual = relation.Columns()[index];
		const auto &expected = expected_columns[index];
		Require(actual.Name() == expected.name && actual.Type() == expected.type &&
		            actual.Nullable() == expected.nullable,
		        std::string("registration column shape drifted for ") + name + "." + expected.name);
	}
}

} // namespace

int main(int argc, char **argv) {
	try {
		Require(argc == 2, "usage: package_compiler_fixture_tests ABSOLUTE_REPOSITORY_ROOT");
		const auto registration = duckdb_api_test::CompileRepositoryGithubRegistrationFixture(argv[1]);
		const auto &identity = registration.Identity();
		Require(identity.SpecIdentifier() == "duckdb_api/v1" && identity.ConnectorId() == "github" &&
		            identity.PackageVersion() == "1.0.0" &&
		            identity.PackageDigest() ==
		                "sha256.b286e6f7481b437b243dfe2ce017a59d601d909272b9d2b35788fb78753ff23b" &&
		            registration.GenerationHandle().IsValid(),
		        "Connector compiler fixture did not expose the exact real package identity and generation");
		Require(registration.Relations().size() == 4,
		        "Connector compiler fixture did not expose the real four-relation registration view");

		const auto anonymous = CompiledRegistrationAuthentication::ANONYMOUS;
		const auto required = CompiledRegistrationAuthentication::LOGICAL_SECRET_REQUIRED;
		const auto bigint = CompiledScalarType::BIGINT;
		const auto varchar = CompiledScalarType::VARCHAR;
		const auto boolean = CompiledScalarType::BOOLEAN;
		RequireRelation(registration.Relations()[0], "duckdb_login_search_page", anonymous,
		                {{"id", bigint, false}, {"login", varchar, false}, {"site_admin", boolean, false}});
		RequireRelation(registration.Relations()[1], "authenticated_user", required,
		                {{"id", bigint, false}, {"login", varchar, false}, {"site_admin", boolean, false}});
		RequireRelation(registration.Relations()[2], "authenticated_repositories", required,
		                {{"id", bigint, false},
		                 {"full_name", varchar, false},
		                 {"private", boolean, false},
		                 {"fork", boolean, false},
		                 {"archived", boolean, false},
		                 {"visibility", varchar, false}});
		RequireRelation(registration.Relations()[3], "viewer_repository_metrics", required,
		                {{"id", varchar, false},
		                 {"full_name", varchar, false},
		                 {"owner_login", varchar, false},
		                 {"stars", bigint, false},
		                 {"primary_language", varchar, true},
		                 {"private", boolean, false},
		                 {"archived", boolean, false},
		                 {"updated_at", varchar, false}});
		std::cout << "package compiler fixture tests passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
