#include "package_fixture_candidate_internal.hpp"
#include "compiled_local_package_internal.hpp"

#include "duckdb_api/internal/connector/package/failsafe_yaml.hpp"

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

const FailsafeYamlNode &Required(const FailsafeYamlNode &node, const std::string &field) {
	if (node.Type() != FailsafeYamlNode::Kind::MAPPING) {
		throw std::logic_error("compiled package source shape no longer matches its generation");
	}
	const auto *value = node.Find(field);
	if (value == nullptr) {
		throw std::logic_error("compiled package source field no longer matches its generation");
	}
	return *value;
}

const FailsafeYamlNode &FindEntry(const FailsafeYamlNode &root, const std::string &sequence_field,
                                  const std::string &id) {
	const auto &sequence = Required(root, sequence_field);
	if (sequence.Type() != FailsafeYamlNode::Kind::SEQUENCE) {
		throw std::logic_error("compiled package source collection is no longer a sequence");
	}
	for (std::size_t index = 0; index < sequence.Size(); index++) {
		const auto &candidate = sequence.SequenceValue(index);
		const auto &candidate_id = Required(candidate, "id");
		if (candidate_id.Type() == FailsafeYamlNode::Kind::SCALAR && candidate_id.Scalar() == id) {
			return candidate;
		}
	}
	throw std::logic_error("compiled package source entry is absent from retained source");
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

void RemoveMappingEntry(SemanticSourceFile &source, const FailsafeYamlNode &mapping, const std::string &field) {
	if (mapping.Type() != FailsafeYamlNode::Kind::MAPPING) {
		throw std::logic_error("compiled package source root is no longer a mapping");
	}
	for (std::size_t index = 0; index < mapping.Size(); index++) {
		if (mapping.MappingKey(index) != field) {
			continue;
		}
		const auto key_offset = static_cast<std::size_t>(mapping.MappingKeySpan(index).begin.byte_offset);
		const auto value_end = static_cast<std::size_t>(mapping.MappingValue(index).Span().end.byte_offset);
		const auto prior_newline = key_offset == 0 ? std::string::npos : source.bytes.rfind('\n', key_offset - 1);
		const auto begin = prior_newline == std::string::npos ? 0 : prior_newline + 1;
		const auto next_newline = source.bytes.find('\n', value_end);
		const auto end = next_newline == std::string::npos ? source.bytes.size() : next_newline + 1;
		if (begin > end || end > source.bytes.size()) {
			throw std::logic_error("compiled package mapping entry has an invalid retained span");
		}
		source.bytes.erase(begin, end - begin);
		return;
	}
	throw std::logic_error("compiled package source mapping field is absent");
}

void InsertBeforeMappingEntry(SemanticSourceFile &source, const FailsafeYamlNode &mapping, const std::string &field,
                              const std::string &bytes) {
	if (mapping.Type() != FailsafeYamlNode::Kind::MAPPING) {
		throw std::logic_error("compiled package source root is no longer a mapping");
	}
	for (std::size_t index = 0; index < mapping.Size(); index++) {
		if (mapping.MappingKey(index) == field) {
			const auto key_offset = static_cast<std::size_t>(mapping.MappingKeySpan(index).begin.byte_offset);
			const auto prior_newline = key_offset == 0 ? std::string::npos : source.bytes.rfind('\n', key_offset - 1);
			const auto begin = prior_newline == std::string::npos ? 0 : prior_newline + 1;
			source.bytes.insert(begin, bytes);
			return;
		}
	}
	throw std::logic_error("compiled package source insertion field is absent");
}

void InsertAfterScalarLine(SemanticSourceFile &source, const FailsafeYamlNode &scalar, const std::string &bytes) {
	if (scalar.Type() != FailsafeYamlNode::Kind::SCALAR || scalar.Span().end.byte_offset > source.bytes.size()) {
		throw std::logic_error("compiled package source scalar has an invalid insertion span");
	}
	const auto end = static_cast<std::size_t>(scalar.Span().end.byte_offset);
	const auto newline = source.bytes.find('\n', end);
	const auto insertion = newline == std::string::npos ? source.bytes.size() : newline + 1;
	source.bytes.insert(insertion, bytes);
}

const CompiledRelation &FirstRelation(const CompiledPackageGeneration &generation) {
	const auto &relations = generation.Connector().Relations();
	if (relations.empty()) {
		throw std::logic_error("compiled package has no relation");
	}
	return relations.front();
}

const CompiledRelation &RelationWithColumns(const CompiledPackageGeneration &generation, std::size_t minimum) {
	for (const auto &relation : generation.Connector().Relations()) {
		if (relation.Columns().size() >= minimum) {
			return relation;
		}
	}
	throw std::invalid_argument("diagnostic source variant requires more compiled columns");
}

const CompiledRelation &RelationWithGraphql(const CompiledPackageGeneration &generation) {
	for (const auto &relation : generation.Connector().Relations()) {
		for (const auto &operation : relation.Operations()) {
			if (operation.Protocol() == CompiledProtocol::GRAPHQL) {
				return relation;
			}
		}
	}
	throw std::invalid_argument("diagnostic source variant requires a GraphQL operation");
}

const CompiledRelation &RelationWithFallback(const CompiledPackageGeneration &generation) {
	for (const auto &relation : generation.Connector().Relations()) {
		for (const auto &operation : relation.Operations()) {
			if (operation.fallback) {
				return relation;
			}
		}
	}
	throw std::invalid_argument("diagnostic source variant requires a fallback operation");
}

const CompiledRelation &RelationWithPredicate(const CompiledPackageGeneration &generation) {
	for (const auto &relation : generation.Connector().Relations()) {
		if (!relation.PredicateMappings().empty()) {
			return relation;
		}
	}
	throw std::invalid_argument("diagnostic source variant requires a predicate mapping");
}

SemanticSourceFile &RelationSource(std::vector<SemanticSourceFile> &files, const CompiledRelation &relation) {
	return FindSource(files, "relations/" + relation.Name() + ".yaml");
}

const CompiledOperation &FallbackOperation(const CompiledRelation &relation) {
	for (const auto &operation : relation.Operations()) {
		if (operation.fallback) {
			return operation;
		}
	}
	throw std::logic_error("compiled relation lost its fallback operation");
}

const CompiledOperation &GraphqlOperation(const CompiledRelation &relation) {
	for (const auto &operation : relation.Operations()) {
		if (operation.Protocol() == CompiledProtocol::GRAPHQL) {
			return operation;
		}
	}
	throw std::logic_error("compiled relation lost its GraphQL operation");
}

std::string UppercaseFirst(std::string value) {
	for (auto &character : value) {
		if (character >= 'a' && character <= 'z') {
			character = static_cast<char>(character - 'a' + 'A');
			return value;
		}
	}
	throw std::invalid_argument("diagnostic invalid-identifier variant requires a lowercase identifier");
}

void MutateCompilerDiagnostic(std::vector<SemanticSourceFile> &files, const CompiledPackageGeneration &generation,
                              const std::string &diagnostic, PackageCancellation &cancellation) {
	auto &manifest_source = FindSource(files, "connector.yaml");
	const auto manifest = ParseSource(manifest_source, cancellation);
	if (diagnostic == "DUCKDB_API_UNSUPPORTED_SPEC") {
		ReplaceScalar(manifest_source, Required(manifest, "api_version"), "duckdb_api/v2");
	} else if (diagnostic == "DUCKDB_API_UNSUPPORTED_DIALECT") {
		ReplaceScalar(manifest_source, Required(manifest, "extractor_dialect"), "duckdb_api/unsupported");
	} else if (diagnostic == "DUCKDB_API_MALFORMED_YAML") {
		ReplaceScalar(manifest_source, Required(manifest, "api_version"), "[");
	} else if (diagnostic == "DUCKDB_API_UNKNOWN_FIELD") {
		manifest_source.bytes += "\nfixture_unknown_field: true\n";
	} else if (diagnostic == "DUCKDB_API_MISSING_FIELD") {
		const auto &relation = FirstRelation(generation);
		auto &source = RelationSource(files, relation);
		const auto root = ParseSource(source, cancellation);
		RemoveMappingEntry(source, root, "columns");
	} else if (diagnostic == "DUCKDB_API_DUPLICATE_ID") {
		const auto &relation = RelationWithColumns(generation, 2);
		auto &source = RelationSource(files, relation);
		const auto root = ParseSource(source, cancellation);
		const auto &columns = Required(root, "columns");
		ReplaceScalar(source, Required(columns.SequenceValue(1), "id"), relation.Columns()[0].name);
	} else if (diagnostic == "DUCKDB_API_INVALID_REFERENCE") {
		const auto &relation = FirstRelation(generation);
		auto &source = RelationSource(files, relation);
		const auto root = ParseSource(source, cancellation);
		const auto &auth = Required(root, "auth");
		const auto &mode = Required(auth, "mode");
		if (mode.Scalar() == "anonymous") {
			ReplaceScalar(source, mode, "credential");
			InsertAfterScalarLine(source, mode, "  credential: fixture_missing_credential\n");
		} else {
			ReplaceScalar(source, Required(auth, "credential"), "fixture_missing_credential");
		}
	} else if (diagnostic == "DUCKDB_API_INVALID_IDENTIFIER") {
		ReplaceScalar(manifest_source, Required(manifest, "id"), UppercaseFirst(generation.Identity().ConnectorId()));
	} else if (diagnostic == "DUCKDB_API_INVALID_TYPE") {
		const auto &relation = RelationWithColumns(generation, 1);
		auto &source = RelationSource(files, relation);
		const auto root = ParseSource(source, cancellation);
		ReplaceScalar(source, Required(Required(root, "columns").SequenceValue(0), "type"), "INTEGER");
	} else if (diagnostic == "DUCKDB_API_INVALID_EXTRACTOR") {
		const auto &relation = RelationWithColumns(generation, 1);
		auto &source = RelationSource(files, relation);
		const auto root = ParseSource(source, cancellation);
		ReplaceScalar(source, Required(Required(root, "columns").SequenceValue(0), "extract"),
		              "fixture_invalid_extractor");
	} else if (diagnostic == "DUCKDB_API_RESERVED_INPUT") {
		const CompiledRelation *relation = nullptr;
		for (const auto &candidate : generation.Connector().Relations()) {
			if (!candidate.Inputs().empty()) {
				relation = &candidate;
				break;
			}
		}
		if (relation == nullptr) {
			relation = &FirstRelation(generation);
		}
		auto &source = RelationSource(files, *relation);
		const auto root = ParseSource(source, cancellation);
		if (!relation->Inputs().empty()) {
			ReplaceScalar(source, Required(FindEntry(root, "inputs", relation->Inputs()[0].Name()), "id"), "secret");
		} else {
			InsertBeforeMappingEntry(source, root, "auth",
			                         "inputs:\n  - id: secret\n    type: VARCHAR\n    nullable: false\n\n");
		}
	} else if (diagnostic == "DUCKDB_API_UNSUPPORTED_DECLARATION") {
		ReplaceScalar(manifest_source, Required(manifest, "kind"), "unsupported");
	} else if (diagnostic == "DUCKDB_API_INVALID_SELECTOR") {
		const auto &relation = RelationWithFallback(generation);
		auto &source = RelationSource(files, relation);
		const auto root = ParseSource(source, cancellation);
		ReplaceScalar(source, Required(FindEntry(root, "operations", FallbackOperation(relation).name), "fallback"),
		              "false");
	} else if (diagnostic == "DUCKDB_API_INVALID_PREDICATE") {
		const auto &relation = RelationWithPredicate(generation);
		auto &source = RelationSource(files, relation);
		const auto root = ParseSource(source, cancellation);
		ReplaceScalar(source,
		              Required(FindEntry(root, "predicates", relation.PredicateMappings()[0].Name()), "accuracy"),
		              "approximate");
	} else if (diagnostic == "DUCKDB_API_INVALID_GRAPHQL_PROFILE") {
		const auto &relation = RelationWithGraphql(generation);
		const auto &operation = GraphqlOperation(relation);
		auto &source = RelationSource(files, relation);
		const auto root = ParseSource(source, cancellation);
		const auto &operation_node = FindEntry(root, "operations", operation.name);
		ReplaceScalar(source, Required(Required(Required(operation_node, "request"), "query"), "operation_name"),
		              "1Invalid");
	} else if (diagnostic == "DUCKDB_API_POLICY_WIDENING") {
		ReplaceScalar(manifest_source, Required(Required(manifest, "network_policy"), "private_addresses"), "allow");
	} else if (diagnostic == "DUCKDB_API_INCOMPATIBLE_RELOAD") {
		ReplaceScalar(manifest_source, Required(manifest, "id"),
		              generation.Identity().ConnectorId() == "fixture_incompatible" ? "fixture_incompatible_alt"
		                                                                            : "fixture_incompatible");
	} else if (diagnostic != "DUCKDB_API_RESOURCE_EXHAUSTED" && diagnostic != "DUCKDB_API_PACKAGE_IDENTITY" &&
	           diagnostic != "DUCKDB_API_FIXTURE_MISMATCH") {
		throw std::invalid_argument("coverage entry is not a Connector-owned diagnostic variant");
	}
}

} // namespace

std::vector<SemanticSourceFile> BuildFixtureDiagnosticSources(const CompiledLocalPackage &active,
                                                              const PackageFixtureCoverageEntry &coverage_entry,
                                                              PackageCancellation &cancellation) {
	CheckCancellation(cancellation);
	if (!active.IsValid() || coverage_entry.scope != PackageFixtureCoverageScope::DIAGNOSTIC) {
		throw std::invalid_argument("fixture diagnostic candidate requires a valid package and diagnostic entry");
	}
	auto files = duckdb_api::internal::CompiledLocalPackageAccess::Source(active).Files();
	MutateCompilerDiagnostic(files, active.Generation(), coverage_entry.diagnostic, cancellation);
	CheckCancellation(cancellation);
	return files;
}

} // namespace internal
} // namespace connector
} // namespace duckdb_api
