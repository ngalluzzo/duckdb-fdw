#pragma once

#include "duckdb_api/compiled_package_generation.hpp"
#include "duckdb_api/local_package_compiler.hpp"
#include "duckdb_api/package_compatibility.hpp"

#include <memory>
#include <string>

namespace duckdb_api_test {

// Compiles the repository's accepted GitHub package through Connector's real
// local-root production entry point. Query tests receive only the bounded
// structural registration view and its opaque generation owner; they cannot
// construct descriptors or import compiler implementation details. The caller
// supplies its explicit absolute repository root so the fixture remains valid
// in archived cold-build projections as well as developer worktrees.
duckdb_api::CompiledQueryRegistrationView
CompileRepositoryGithubRegistrationFixture(const std::string &absolute_repository_root);

// Compiles the repository GitHub package through Connector's public local-root
// service. Consumers receive the generation/custody pair and import no package
// source, YAML, schema, or compiler-private construction API.
duckdb_api::CompiledLocalPackage
CompileRepositoryGithubLocalPackageFixture(const std::string &absolute_repository_root);

// Compiles a deterministic second connector from copied accepted semantic
// files with only the connector id changed. The fixture service retains its
// temporary root for process lifetime so the returned exact custody remains
// eligible for production recompile; consumers receive no YAML or path API.
duckdb_api::CompiledLocalPackage
CompileRepositoryDistinctLocalPackageFixture(const std::string &absolute_repository_root);

enum class LocalPackageReloadFixtureVariant { EXACT_NO_OP, COMPATIBLE_PROVENANCE_PATCH, INCOMPATIBLE_MAJOR };

// Connector-owned real-source fixture for Runtime publication tests. The
// private state owns a temporary package tree plus exact active/candidate
// CompiledLocalPackage values and Connector's bound compatibility decision.
// Keep the fixture alive for any further source reacquisition.
class LocalPackageReloadFixture {
public:
	LocalPackageReloadFixture(const LocalPackageReloadFixture &) = default;
	LocalPackageReloadFixture(LocalPackageReloadFixture &&) = default;
	LocalPackageReloadFixture &operator=(const LocalPackageReloadFixture &) = delete;
	LocalPackageReloadFixture &operator=(LocalPackageReloadFixture &&) = delete;

	const duckdb_api::CompiledLocalPackage &Active() const;
	const duckdb_api::CompiledLocalPackage &Candidate() const;
	const duckdb_api::PackageReloadDecision &Decision() const;

private:
	class State;
	explicit LocalPackageReloadFixture(std::shared_ptr<const State> state);
	std::shared_ptr<const State> state;

	friend LocalPackageReloadFixture BuildRepositoryGithubLocalPackageReloadFixture(const std::string &,
	                                                                                LocalPackageReloadFixtureVariant);
};

LocalPackageReloadFixture BuildRepositoryGithubLocalPackageReloadFixture(const std::string &absolute_repository_root,
                                                                         LocalPackageReloadFixtureVariant variant);

} // namespace duckdb_api_test
