#pragma once

#include "duckdb_api/compiled_package_generation.hpp"

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

} // namespace duckdb_api_test
