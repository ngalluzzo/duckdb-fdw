#include "compiler_test_support.hpp"

#include "duckdb_api/content_digest.hpp"
#include "duckdb_api/internal/connector/graphql_operation_declaration.hpp"
#include "duckdb_api/internal/connector/graphql_query_recipe.hpp"

#include <iostream>

namespace {

using duckdb_api_test::NeverCancel;
using duckdb_api_test::Require;
using duckdb_api_test::TemporaryPackage;

std::string GraphqlSource() {
	return duckdb_api_test::ReadFile("docs/rfcs/evidence/0013/github/relations/viewer_repository_metrics.yaml");
}

duckdb_api::CompiledGraphqlOperation CompileGraphql(const std::string &source, TemporaryPackage &package) {
	duckdb_api_test::WriteGithubPackage(package, source);
	NeverCancel cancellation;
	const auto result = duckdb_api_test::CompileRoot(package.Root(), cancellation);
	if (!result.Succeeded()) {
		throw std::runtime_error("structured GraphQL test package did not compile");
	}
	const auto *relation = result.Generation()->Connector().FindRelation("viewer_repository_metrics");
	if (relation == nullptr) {
		throw std::runtime_error("structured GraphQL test relation disappeared");
	}
	return relation->Operation().Graphql();
}

void TestExactGithubGolden() {
	TemporaryPackage package;
	const auto &operation = CompileGraphql(GraphqlSource(), package);
	const auto &recipe = operation.QueryRecipe();
	Require(operation.document.size() == 581 &&
	            operation.document == duckdb_api::internal::CanonicalGithubViewerRepositoryMetricsDocument() &&
	            operation.document_digest == duckdb_api::internal::CanonicalGithubViewerRepositoryMetricsDigest() &&
	            recipe.Identity() == duckdb_api::CompiledGraphqlDocumentIdentity::PACKAGE_QUERY_GENERATOR_V1 &&
	            recipe.OperationName() == "DuckdbApiViewerRepositoryMetrics" &&
	            recipe.RootPath() == std::vector<std::string>({"viewer", "repositories"}) &&
	            recipe.Variables().size() == 2 && recipe.Variables()[0].Name() == "pageSize" &&
	            recipe.Variables()[0].Role() == duckdb_api::CompiledGraphqlRecipeVariableRole::PAGE_SIZE &&
	            recipe.Variables()[1].Name() == "cursor" &&
	            recipe.Variables()[1].Role() == duckdb_api::CompiledGraphqlRecipeVariableRole::CURSOR &&
	            recipe.FixedArguments().size() == 3 &&
	            recipe.FixedArguments()[0].Value().Kind() == duckdb_api::CompiledGraphqlLiteralKind::LIST &&
	            recipe.FixedArguments()[2].Value().Kind() == duckdb_api::CompiledGraphqlLiteralKind::OBJECT &&
	            recipe.Selections().size() == 8 &&
	            duckdb_api::internal::RenderCompiledGraphqlQueryRecipe(recipe) == operation.document &&
	            !duckdb_api::IsCanonicalGraphqlDocumentProfile(operation.document_identity, operation.document,
	                                                           operation.digest_algorithm, operation.document_digest),
	        "renderer did not reproduce the exact 581-byte GitHub query golden");
}

void TestIndependentStructuredQuery() {
	auto source =
	    duckdb_api_test::ReplaceOnce(GraphqlSource(), "DuckdbApiViewerRepositoryMetrics", "IndependentAccountProjects");
	source =
	    duckdb_api_test::ReplaceOnce(std::move(source), "root: [viewer, repositories]", "root: [account, projects]");
	source = duckdb_api_test::ReplaceOnce(std::move(source), "path: /graphql", "path: /alternate/graphql");
	TemporaryPackage package;
	const auto &operation = CompileGraphql(source, package);
	Require(operation.document != duckdb_api::internal::CanonicalGithubViewerRepositoryMetricsDocument() &&
	            operation.document.find("query IndependentAccountProjects") == 0 &&
	            operation.document.find("  account {\n    projects(") != std::string::npos &&
	            operation.response.nodes.segments ==
	                std::vector<std::string>({"data", "account", "projects", "nodes"}) &&
	            operation.endpoint_path == "/alternate/graphql",
	        "renderer was hard-coded to the repository GitHub query identity");
}

void TestCompleteLiteralGrammarAndStringKeywords() {
	auto source = duckdb_api_test::ReplaceOnce(
	    GraphqlSource(), "                - name: direction\n                  value: {enum: DESC}\n",
	    "                - name: direction\n"
	    "                  value: {enum: DESC}\n"
	    "          - name: literalCoverage\n"
	    "            value:\n"
	    "              object:\n"
	    "                - {name: nullValue, value: {null: true}}\n"
	    "                - {name: booleanValue, value: {boolean: true}}\n"
	    "                - {name: integerValue, value: {integer: -42}}\n"
	    "                - {name: stringValue, value: {string: \"fragment mutation subscription __safe\"}}\n"
	    "                - name: listValue\n"
	    "                  value:\n"
	    "                    list:\n"
	    "                      - {boolean: false}\n"
	    "                      - {enum: ACTIVE}\n");
	TemporaryPackage package;
	const auto &operation = CompileGraphql(source, package);
	Require(operation.document.find(
	            "literalCoverage: {nullValue: null, booleanValue: true, integerValue: -42, stringValue: \"fragment "
	            "mutation subscription __safe\", listValue: [false, ACTIVE]}") != std::string::npos,
	        "renderer rejected or changed a value in the closed fixed-literal grammar");
}

void TestDocumentAndDigestCannotReplaceRecipeMembership() {
	TemporaryPackage package;
	auto operation = CompileGraphql(GraphqlSource(), package);
	operation.document += " ";
	operation.document_digest = duckdb_api::ComputeSha256Hex(operation.document);
	bool rejected = false;
	try {
		duckdb_api::internal::ValidateGraphqlOperationValue(operation);
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	Require(rejected, "changed GraphQL bytes plus their recomputed digest replaced recipe membership");
}

void TestProfileCounterexamples() {
	for (const auto &source :
	     {duckdb_api_test::ReplaceOnce(GraphqlSource(), "field_path: [nameWithOwner]", "field_path: [owner, name]"),
	      duckdb_api_test::ReplaceOnce(GraphqlSource(), "page_size_variable: pageSize", "page_size_variable: cursor"),
	      duckdb_api_test::ReplaceOnce(GraphqlSource(), "max_document_bytes: 4096", "max_document_bytes: 128")}) {
		TemporaryPackage package;
		duckdb_api_test::WriteGithubPackage(package, source);
		NeverCancel cancellation;
		const auto result = duckdb_api_test::CompileRoot(package.Root(), cancellation);
		Require(!result.Succeeded(), "invalid structured GraphQL profile compiled");
		bool found = false;
		for (const auto &diagnostic : result.Diagnostics()) {
			found = found || diagnostic.Code() == duckdb_api::connector::PackageDiagnosticCode::INVALID_GRAPHQL_PROFILE;
		}
		Require(found, "GraphQL profile counterexample used another diagnostic contract");
	}
}

} // namespace

int main() {
	try {
		TestExactGithubGolden();
		TestIndependentStructuredQuery();
		TestCompleteLiteralGrammarAndStringKeywords();
		TestDocumentAndDigestCannotReplaceRecipeMembership();
		TestProfileCounterexamples();
		std::cout << "package GraphQL renderer tests passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
