#include "compiler_test_support.hpp"

#include "duckdb_api/internal/connector/graphql_operation_declaration.hpp"

#include <iostream>

namespace {

using duckdb_api_test::NeverCancel;
using duckdb_api_test::Require;
using duckdb_api_test::TemporaryPackage;

void TestGithubPackageCompilesAsOneGeneration() {
	TemporaryPackage package;
	duckdb_api_test::WriteGithubPackage(package);
	NeverCancel cancellation;
	const auto result = duckdb_api_test::CompileRoot(package.Root(), cancellation);
	Require(result.Succeeded() && result.Diagnostics().empty(), "accepted GitHub package did not compile");
	const auto *generation = result.Generation();
	Require(generation != nullptr && generation->Identity().SpecIdentifier() == "duckdb_api/v1" &&
	            generation->Identity().ConnectorId() == "github" &&
	            generation->Identity().PackageVersion() == "1.0.0" &&
	            generation->Identity().PackageDigest() ==
	                "sha256.b286e6f7481b437b243dfe2ce017a59d601d909272b9d2b35788fb78753ff23b",
	        "compiled generation lost stable package identity");
	const auto &connector = generation->Connector();
	Require(connector.Origin() == duckdb_api::CompiledConnectorOrigin::PACKAGE_COMPILED_METADATA &&
	            connector.Relations().size() == 4 && connector.Relations()[0].Name() == "duckdb_login_search_page" &&
	            connector.Relations()[1].Name() == "authenticated_user" &&
	            connector.Relations()[2].Name() == "authenticated_repositories" &&
	            connector.Relations()[3].Name() == "viewer_repository_metrics",
	        "compiled generation lost manifest relation order or package provenance");
	const auto *predicate = connector.FindRelation("authenticated_repositories");
	Require(predicate != nullptr && predicate->PredicateMappings().size() == 1 &&
	            predicate->PredicateMappings()[0].ProofIdentity() ==
	                duckdb_api::CompiledPredicateProofIdentity::PACKAGE_DECLARED_V1 &&
	            predicate->PredicateMappings()[0].EncodedRemoteValue() == "private",
	        "compiled GitHub package lost its declared predicate binding");
	const auto *graphql = connector.FindRelation("viewer_repository_metrics");
	Require(graphql != nullptr && graphql->Operations().size() == 1 &&
	            graphql->Operation().Graphql().document ==
	                duckdb_api::internal::CanonicalGithubViewerRepositoryMetricsDocument() &&
	            graphql->Operation().Graphql().document_digest ==
	                duckdb_api::internal::CanonicalGithubViewerRepositoryMetricsDigest(),
	        "structured GitHub package did not reproduce the exact GraphQL golden");
	const auto query = generation->QueryRegistration();
	Require(query.Relations().size() == 4 && query.GenerationHandle().IsValid() &&
	            query.Identity().PackageDigest() == generation->Identity().PackageDigest(),
	        "compiler did not provide the bounded Query registration projection");
}

void TestLocalRootSourceFailuresStayDiagnosticOnly() {
	TemporaryPackage package;
	NeverCancel cancellation;
	const auto result = duckdb_api::connector::CompileLocalPackageRoot(package.Root() + "/missing", cancellation);
	Require(!result.Succeeded() && result.Generation() == nullptr && result.Diagnostics().size() == 1 &&
	            result.Diagnostics()[0].Code() == duckdb_api::connector::PackageDiagnosticCode::PACKAGE_IDENTITY &&
	            result.Diagnostics()[0].Phase() == duckdb_api::connector::PackageDiagnosticPhase::SOURCE &&
	            result.Diagnostics()[0].Coordinate().file.empty(),
	        "production local-root compiler leaked or threw a source-custody failure");
}

void TestSchemaAssetIdentity() {
	Require(std::string(duckdb_api::connector::ConnectorPackageV1SchemaDigest()) ==
	                "sha256.14d126bd0c540ba8c298480f71d8ba5c433943d4019b19c5e99ba29f0c8a4fdb" &&
	            duckdb_api::connector::VerifyConnectorPackageV1SchemaAsset(),
	        "permanent connector schema asset drifted from RFC 0013");
}

} // namespace

int main() {
	try {
		TestSchemaAssetIdentity();
		TestGithubPackageCompilesAsOneGeneration();
		TestLocalRootSourceFailuresStayDiagnosticOnly();
		std::cout << "package compiler contract tests passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
