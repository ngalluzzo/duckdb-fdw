#include "package_model_compiler_internal.hpp"

#include <set>

namespace duckdb_api {
namespace connector {
namespace internal {

namespace {

std::string OriginIdentity(const OriginDeclaration &origin) {
	return origin.scheme.value + "\n" + origin.host.value + "\n" + origin.port.value;
}

void ValidateManifestPolicy(const PackageDeclaration &package, PackageDiagnosticSink &diagnostics) {
	std::set<std::string> allowed;
	for (const auto &origin : package.manifest.network_policy.origins) {
		allowed.insert(OriginIdentity(origin));
	}
	for (const auto &credential : package.manifest.credentials) {
		for (const auto &destination : credential.destinations) {
			if (allowed.count(OriginIdentity(destination)) == 0) {
				diagnostics.Add(PackageDiagnosticCode::POLICY_WIDENING, PackageDiagnosticPhase::COMPILE,
				                destination.mark, package.manifest.id.value);
			}
		}
	}
	const auto manifest_response_bytes = ParseUnsigned(package.manifest.network_policy.max_response_bytes);
	for (const auto &relation : package.relations) {
		if (ParseUnsigned(relation.resources.max_response_bytes_per_page) > manifest_response_bytes) {
			diagnostics.Add(PackageDiagnosticCode::POLICY_WIDENING, PackageDiagnosticPhase::COMPILE,
			                relation.resources.max_response_bytes_per_page.mark, package.manifest.id.value,
			                relation.id.value);
		}
		for (const auto &operation : relation.operations) {
			const auto &origin = operation.graphql ? operation.graphql_request.origin : operation.rest.origin;
			if (allowed.count(OriginIdentity(origin)) == 0) {
				diagnostics.Add(PackageDiagnosticCode::POLICY_WIDENING, PackageDiagnosticPhase::COMPILE, origin.mark,
				                package.manifest.id.value, relation.id.value, operation.id.value);
			}
		}
	}
}

CompiledNetworkPolicy CompileNetworkPolicy(const NetworkPolicyDeclaration &source) {
	std::vector<CompiledHttpOrigin> origins;
	origins.reserve(source.origins.size());
	for (const auto &origin : source.origins) {
		origins.push_back(CompileOrigin(origin));
	}
	return CompiledNetworkPolicy(std::move(origins), ParseUnsigned(source.max_response_bytes));
}

} // namespace

std::shared_ptr<const CompiledPackageGeneration> CompilePackageDeclaration(const PackageDeclaration &package,
                                                                           const PackageSourceSnapshot &snapshot,
                                                                           PackageDiagnosticSink &diagnostics,
                                                                           PackageCancellation &cancellation) {
	ValidateManifestPolicy(package, diagnostics);
	std::vector<CompiledRelation> relations;
	relations.reserve(package.relations.size());
	for (const auto &source : package.relations) {
		auto relation = CompileRelation(package.manifest, source, snapshot.Digest(), diagnostics, cancellation);
		if (relation) {
			relations.push_back(std::move(*relation));
		}
	}
	if (!diagnostics.Empty()) {
		return nullptr;
	}
	if (cancellation.IsCancellationRequested()) {
		throw FailsafeYamlError(FailsafeYamlErrorCode::CANCELLED, "connector.yaml", package.manifest.mark.span,
		                        "package compilation was cancelled");
	}
	auto connector = duckdb_api::internal::CompiledModelBuilder::Connector(
	    CompiledConnectorOrigin::PACKAGE_COMPILED_METADATA, package.manifest.id.value, package.manifest.version.value,
	    std::move(relations), CompileNetworkPolicy(package.manifest.network_policy));
	auto identity = duckdb_api::internal::CompiledModelBuilder::PackageIdentity(
	    package.manifest.api_version.value, package.manifest.id.value, package.manifest.version.value,
	    snapshot.Digest());
	return std::make_shared<const CompiledPackageGeneration>(
	    duckdb_api::internal::CompiledModelBuilder::PackageGeneration(std::move(identity), std::move(connector)));
}

} // namespace internal
} // namespace connector
} // namespace duckdb_api
