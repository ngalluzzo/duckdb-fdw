#include "package_fixture_candidate_internal.hpp"

#include "compiled_local_package_internal.hpp"
#include "duckdb_api/internal/connector/package/failsafe_yaml.hpp"
#include "duckdb_api/package_semver.hpp"

#include <limits>
#include <stdexcept>

namespace duckdb_api {
namespace connector {
namespace internal {
namespace {

void CheckCancellation(PackageCancellation &cancellation) {
	if (cancellation.IsCancellationRequested()) {
		throw PackageCompilationCancelled();
	}
}

SemanticSourceFile &FindSource(std::vector<SemanticSourceFile> &files, const std::string &path) {
	for (auto &file : files) {
		if (file.path == path) {
			return file;
		}
	}
	throw std::logic_error("compiled package source snapshot is incomplete");
}

FailsafeYamlNode ParseSource(const SemanticSourceFile &source, PackageCancellation &cancellation) {
	FailsafeYamlBudget budget(FailsafeYamlLimits::V1());
	try {
		return ParseFailsafeYaml(source.path, source.bytes, budget, cancellation);
	} catch (const FailsafeYamlError &error) {
		if (error.Code() == FailsafeYamlErrorCode::CANCELLED) {
			throw PackageCompilationCancelled();
		}
		throw std::logic_error("compiled package retained invalid semantic YAML");
	}
}

const FailsafeYamlNode &Required(const FailsafeYamlNode &node, const char *field) {
	if (node.Type() != FailsafeYamlNode::Kind::MAPPING) {
		throw std::logic_error("compiled package source shape no longer matches its generation");
	}
	const auto *value = node.Find(field);
	if (value == nullptr) {
		throw std::logic_error("compiled package source field no longer matches its generation");
	}
	return *value;
}

void ReplaceScalar(SemanticSourceFile &source, const FailsafeYamlNode &node, const std::string &replacement) {
	if (node.Type() != FailsafeYamlNode::Kind::SCALAR || node.Span().begin.byte_offset > node.Span().end.byte_offset ||
	    node.Span().end.byte_offset > source.bytes.size()) {
		throw std::logic_error("compiled package source scalar has an invalid retained span");
	}
	source.bytes.replace(static_cast<std::size_t>(node.Span().begin.byte_offset),
	                     static_cast<std::size_t>(node.Span().end.byte_offset - node.Span().begin.byte_offset),
	                     replacement);
}

std::string IncrementedPatch(const PackageSemVer &version) {
	if (version.Patch() == std::numeric_limits<std::uint32_t>::max()) {
		throw std::invalid_argument("fixture reload patch variant is unavailable at the SemVer boundary");
	}
	return std::to_string(version.Major()) + "." + std::to_string(version.Minor()) + "." +
	       std::to_string(version.Patch() + 1);
}

std::string IncrementedMinor(const PackageSemVer &version) {
	if (version.Minor() == std::numeric_limits<std::uint32_t>::max()) {
		throw std::invalid_argument("fixture reload minor variant is unavailable at the SemVer boundary");
	}
	return std::to_string(version.Major()) + "." + std::to_string(version.Minor() + 1) + ".0";
}

std::string Downgraded(const PackageSemVer &version) {
	if (version.Patch() > 0) {
		return std::to_string(version.Major()) + "." + std::to_string(version.Minor()) + "." +
		       std::to_string(version.Patch() - 1);
	}
	if (version.Minor() > 0) {
		return std::to_string(version.Major()) + "." + std::to_string(version.Minor() - 1) + ".0";
	}
	if (version.Major() > 0) {
		return std::to_string(version.Major() - 1) + ".0.0";
	}
	throw std::invalid_argument("fixture reload downgrade variant is unavailable at version 0.0.0");
}

void MutateReload(std::vector<SemanticSourceFile> &files, const CompiledPackageGeneration &generation,
                  const std::string &variant, PackageCancellation &cancellation) {
	auto &manifest = FindSource(files, "connector.yaml");
	if (variant == "exact_no_op") {
		return;
	}
	if (variant == "version_reuse_rejected") {
		manifest.bytes += "\n# duckdb_api closed fixture identity variant\n";
		return;
	}
	const auto root = ParseSource(manifest, cancellation);
	if (variant == "incompatible_rejected") {
		const auto &id = Required(root, "id");
		const auto replacement =
		    id.Scalar() == "fixture_incompatible" ? "fixture_incompatible_alt" : "fixture_incompatible";
		ReplaceScalar(manifest, id, replacement);
		return;
	}
	const auto &version_node = Required(root, "version");
	const auto version = PackageSemVer::Parse(generation.Identity().PackageVersion());
	std::string replacement;
	if (variant == "compatible_patch") {
		replacement = IncrementedPatch(version);
	} else if (variant == "compatible_minor") {
		replacement = IncrementedMinor(version);
	} else if (variant == "downgrade_rejected") {
		replacement = Downgraded(version);
	} else {
		throw std::invalid_argument("coverage entry is not a closed reload source variant");
	}
	ReplaceScalar(manifest, version_node, replacement);
}

const CompiledOperation &FindGraphqlOperation(const CompiledPackageGeneration &generation,
                                              const PackageFixtureCoverageEntry &entry) {
	const auto *relation = generation.Connector().FindRelation(entry.relation);
	if (relation == nullptr) {
		throw std::invalid_argument("GraphQL fixture candidate relation is absent from the generation");
	}
	for (const auto &operation : relation->Operations()) {
		if (operation.name == entry.operation && operation.Protocol() == CompiledProtocol::GRAPHQL) {
			return operation;
		}
	}
	throw std::invalid_argument("GraphQL fixture candidate operation is absent from the generation");
}

const FailsafeYamlNode &FindOperationNode(const FailsafeYamlNode &root, const std::string &operation) {
	const auto &operations = Required(root, "operations");
	if (operations.Type() != FailsafeYamlNode::Kind::SEQUENCE) {
		throw std::logic_error("compiled relation operation source is no longer a sequence");
	}
	for (std::size_t index = 0; index < operations.Size(); index++) {
		const auto &candidate = operations.SequenceValue(index);
		const auto &id = Required(candidate, "id");
		if (id.Type() == FailsafeYamlNode::Kind::SCALAR && id.Scalar() == operation) {
			return candidate;
		}
	}
	throw std::logic_error("compiled GraphQL operation is absent from retained source");
}

void MutateGraphqlDocumentLimit(std::vector<SemanticSourceFile> &files, const CompiledPackageGeneration &generation,
                                const PackageFixtureCoverageEntry &entry, PackageCancellation &cancellation) {
	if (entry.resource != "max_document_bytes" ||
	    (entry.variant != "boundary" && entry.variant != "one_over_rejected")) {
		throw std::invalid_argument("coverage entry is not a closed GraphQL document-limit variant");
	}
	const auto &operation = FindGraphqlOperation(generation, entry);
	const auto document_bytes = operation.Graphql().document.size();
	if (document_bytes == 0 || (entry.variant == "one_over_rejected" && document_bytes == 1)) {
		throw std::logic_error("compiled GraphQL document cannot exercise the requested limit variant");
	}
	auto &relation_source = FindSource(files, "relations/" + entry.relation + ".yaml");
	const auto root = ParseSource(relation_source, cancellation);
	const auto &operation_node = FindOperationNode(root, entry.operation);
	const auto &request = Required(operation_node, "request");
	const auto &limit = Required(request, "max_document_bytes");
	const auto value = entry.variant == "boundary" ? document_bytes : document_bytes - 1;
	ReplaceScalar(relation_source, limit, std::to_string(value));
}

} // namespace

std::vector<SemanticSourceFile> BuildFixtureCandidateSources(const CompiledLocalPackage &active,
                                                             const PackageFixtureCoverageEntry &coverage_entry,
                                                             PackageCancellation &cancellation) {
	CheckCancellation(cancellation);
	if (!active.IsValid()) {
		throw std::invalid_argument("fixture source candidate requires a valid active package");
	}
	auto files = duckdb_api::internal::CompiledLocalPackageAccess::Source(active).Files();
	if (coverage_entry.scope == PackageFixtureCoverageScope::RELOAD) {
		MutateReload(files, active.Generation(), coverage_entry.variant, cancellation);
	} else if (coverage_entry.scope == PackageFixtureCoverageScope::GRAPHQL_RESOURCE) {
		MutateGraphqlDocumentLimit(files, active.Generation(), coverage_entry, cancellation);
	} else {
		throw std::invalid_argument("coverage entry has no Connector-owned source candidate");
	}
	CheckCancellation(cancellation);
	return files;
}

} // namespace internal
} // namespace connector
} // namespace duckdb_api
