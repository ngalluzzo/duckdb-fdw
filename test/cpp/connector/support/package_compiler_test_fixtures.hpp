#pragma once

#include "duckdb_api/compiled_package_generation.hpp"
#include "duckdb_api/local_package_compiler.hpp"
#include "duckdb_api/package_compatibility.hpp"

#include <memory>
#include <string>

namespace duckdb_api_test {

// Compiles the repository's permanent `connectors/github` package through
// Connector's real local-root production entry point. Query registration
// consumers receive the bounded structural view and its opaque owner;
// Semantics integration tests may receive the complete immutable generation.
// Neither can construct descriptors or import compiler implementation details.
// The caller supplies its explicit absolute repository root so the fixture
// remains valid in archived cold-build projections as well as developer
// worktrees.
duckdb_api::CompiledQueryRegistrationView
CompileRepositoryGithubRegistrationFixture(const std::string &absolute_repository_root);

// Compiles the permanent repository GitHub package through Connector's public
// local-root service. Consumers receive the generation/custody pair and import
// no package source, YAML, schema, or compiler-private construction API.
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

// Compiles and returns the complete immutable generation for cross-team
// Semantics tests that must prove exact generation-handle ownership. Consumers
// receive no compiler internals and cannot construct or replace its catalog.
duckdb_api::CompiledPackageGeneration
CompileRepositoryGithubGenerationFixture(const std::string &absolute_repository_root);

// Compiles a deliberately non-GitHub v1 package with an explicit 8443 origin,
// equally ranked GraphQL and REST candidates, an unconditional REST fallback,
// typed defaults, exact and superset predicate mappings, and a separate
// unpaginated root-array relation. Resource declarations are deliberately
// wider than the current host cell.
duckdb_api::CompiledPackageGeneration
CompileNonGithubGraphqlGenerationFixture(const std::string &absolute_repository_root);

enum class RepositoryGithubGraphqlCounterexample {
	DOCUMENT_MISMATCH,
	DIGEST_MISMATCH,
	VARIABLE_MISMATCH,
	RESPONSE_PATH_MISMATCH,
	COLUMN_MISMATCH,
	CURSOR_MISMATCH,
	UNKNOWN_RECIPE_IDENTITY,
	MIXED_CASE_AUTHORIZATION_HEADER,
	MIXED_CASE_HOST_HEADER,
	MIXED_CASE_CONTENT_LENGTH_HEADER,
	CASE_INSENSITIVE_DUPLICATE_HEADER,
	MIXED_CASE_CONTENT_TYPE_MISMATCH,
	INVALID_HEADER_NAME,
	INVALID_HEADER_VALUE,
	INVALID_ENDPOINT_PATH_GRAMMAR,
	TRAILING_ENDPOINT_PATH_SEPARATOR,
	ENDPOINT_PATH_TOO_LONG,
	ENDPOINT_PORT_OUTSIDE_POLICY,
	TOO_MANY_HEADERS,
	HEADER_BYTES_EXCEEDED,
	RESPONSE_SCAN_SCOPE_EXCEEDED,
	RECORD_SCAN_SCOPE_EXCEEDED,
	RESOURCE_PRODUCT_OVERFLOW,
	COUNT
};

enum class RepositoryGithubGraphqlBoundary {
	ENDPOINT_PATH_BYTES,
	FIXED_HEADER_COUNT,
	FIXED_HEADER_BYTES,
	RESPONSE_SCAN_PRODUCT,
	COUNT
};

// Closed Connector-owned counterexamples for Semantics' independent recipe
// copier. The provider performs all friend-only construction; consumers see
// only immutable compiled values and cannot mutate Connector internals.
enum class RepositoryGithubGraphqlRecipeFixture {
	EXACT_LITERAL_DEPTH,
	EXCESSIVE_LITERAL_DEPTH,
	EXACT_LIST_ITEMS,
	EXCESSIVE_LIST_ITEMS,
	MINIMUM_SIGNED_INTEGER,
	MAXIMUM_SIGNED_INTEGER,
	BELOW_MINIMUM_SIGNED_INTEGER,
	ABOVE_MAXIMUM_SIGNED_INTEGER,
	COUNT
};

enum class GraphqlLiteralNodeBudgetFixture { EXACT, EXCESSIVE, COUNT };

duckdb_api::CompiledGraphqlQueryRecipe
CompileRepositoryGithubGraphqlRecipeFixture(const std::string &absolute_repository_root,
                                            RepositoryGithubGraphqlRecipeFixture fixture);

duckdb_api::CompiledGraphqlLiteral BuildGraphqlLiteralNodeBudgetFixture(GraphqlLiteralNodeBudgetFixture fixture);

// Starts from a real compiler-produced repository generation, then confines
// one named post-validation mutation to Connector's non-installable fixture
// boundary. Semantics receives only the public immutable catalog facade and
// must fail before producing a partial ScanPlan.
duckdb_api::CompiledConnector
CompileRepositoryGithubGraphqlCounterexample(const std::string &absolute_repository_root,
                                             RepositoryGithubGraphqlCounterexample counterexample);

// Starts from the same compiler-produced operation and moves one fact to the
// exact accepted RFC 0013 boundary. These values complement the one-over
// counterexamples so Semantics does not accidentally narrow package syntax.
duckdb_api::CompiledConnector CompileRepositoryGithubGraphqlBoundary(const std::string &absolute_repository_root,
                                                                     RepositoryGithubGraphqlBoundary boundary);

} // namespace duckdb_api_test
