#include "package_compiler_internal.hpp"

#include <map>
#include <stdexcept>
#include <utility>

namespace duckdb_api {
namespace connector {

namespace {

internal::SourceMark RootMark() {
	return {"", {{0, 0, 0}, {0, 0, 0}}, ""};
}

internal::SourceMark SyntaxMark(const FailsafeYamlError &error) {
	return {error.File(), error.Span(), "$"};
}

internal::SourceMark SourceFailureMark(const std::string &file) {
	return file.empty() ? RootMark() : internal::SourceMark {file, {{0, 1, 1}, {0, 1, 1}}, "$"};
}

} // namespace

PackageCompileResult CompilePackage(const PackageSourceSnapshot &snapshot, const PackageCompilerLimits &host_limits,
                                    PackageCancellation &cancellation) {
	internal::PackageDiagnosticSink diagnostics(host_limits.max_diagnostics);
	if (!snapshot.IsValid() || !VerifyConnectorPackageV1SchemaAsset()) {
		diagnostics.Add(PackageDiagnosticCode::PACKAGE_IDENTITY, PackageDiagnosticPhase::SOURCE, RootMark());
		return PackageCompileResult(nullptr, diagnostics.Finish());
	}
	if (cancellation.IsCancellationRequested()) {
		throw FailsafeYamlError(FailsafeYamlErrorCode::CANCELLED, "connector.yaml", {{0, 1, 1}, {0, 1, 1}},
		                        "package compilation was cancelled");
	}

	FailsafeYamlBudget yaml_budget(host_limits.yaml);
	std::vector<std::pair<std::string, FailsafeYamlNode>> documents;
	documents.reserve(snapshot.Files().size());
	for (const auto &source : snapshot.Files()) {
		if (cancellation.IsCancellationRequested()) {
			throw FailsafeYamlError(FailsafeYamlErrorCode::CANCELLED, source.path, {{0, 1, 1}, {0, 1, 1}},
			                        "package compilation was cancelled");
		}
		try {
			documents.push_back(
			    std::make_pair(source.path, ParseFailsafeYaml(source.path, source.bytes, yaml_budget, cancellation)));
		} catch (const FailsafeYamlError &error) {
			if (error.Code() == FailsafeYamlErrorCode::CANCELLED) {
				throw;
			}
			diagnostics.Add(error.Code() == FailsafeYamlErrorCode::RESOURCE_EXHAUSTED
			                    ? PackageDiagnosticCode::RESOURCE_EXHAUSTED
			                    : PackageDiagnosticCode::MALFORMED_YAML,
			                PackageDiagnosticPhase::SYNTAX, SyntaxMark(error));
		}
	}
	if (!diagnostics.Empty()) {
		return PackageCompileResult(nullptr, diagnostics.Finish());
	}

	internal::PackageDeclaration package;
	internal::DecodePackageSchema(documents, snapshot, diagnostics, package);
	if (!diagnostics.Empty()) {
		return PackageCompileResult(nullptr, diagnostics.Finish());
	}

	auto generation = internal::CompilePackageDeclaration(package, snapshot, diagnostics, cancellation);
	if (!diagnostics.Empty() || !generation) {
		return PackageCompileResult(nullptr, diagnostics.Finish());
	}
	return PackageCompileResult(std::move(generation), {});
}

PackageCompileResult CompileLocalPackageRoot(const std::string &absolute_root, const PackageSourceLimits &source_limits,
                                             const PackageCompilerLimits &compiler_limits,
                                             PackageCancellation &cancellation) {
	try {
		return CompilePackage(AcquirePackageSource(absolute_root, source_limits, cancellation), compiler_limits,
		                      cancellation);
	} catch (const PackageSourceError &error) {
		if (error.Code() == PackageSourceErrorCode::CANCELLED) {
			throw;
		}
		internal::PackageDiagnosticSink diagnostics(compiler_limits.max_diagnostics);
		diagnostics.Add(error.Code() == PackageSourceErrorCode::RESOURCE_EXHAUSTED
		                    ? PackageDiagnosticCode::RESOURCE_EXHAUSTED
		                    : PackageDiagnosticCode::PACKAGE_IDENTITY,
		                PackageDiagnosticPhase::SOURCE, SourceFailureMark(error.Path()));
		return PackageCompileResult(nullptr, diagnostics.Finish());
	} catch (const FailsafeYamlError &error) {
		if (error.Code() == FailsafeYamlErrorCode::CANCELLED) {
			throw;
		}
		internal::PackageDiagnosticSink diagnostics(compiler_limits.max_diagnostics);
		diagnostics.Add(error.Code() == FailsafeYamlErrorCode::RESOURCE_EXHAUSTED
		                    ? PackageDiagnosticCode::RESOURCE_EXHAUSTED
		                    : PackageDiagnosticCode::MALFORMED_YAML,
		                PackageDiagnosticPhase::SYNTAX, SyntaxMark(error));
		return PackageCompileResult(nullptr, diagnostics.Finish());
	}
}

PackageCompileResult CompileLocalPackageRoot(const std::string &absolute_root, PackageCancellation &cancellation) {
	return CompileLocalPackageRoot(absolute_root, PackageSourceLimits::V1(), PackageCompilerLimits::V1(), cancellation);
}

namespace internal {

bool DecodePackageSchema(const std::vector<std::pair<std::string, FailsafeYamlNode>> &documents,
                         const PackageSourceSnapshot &snapshot, PackageDiagnosticSink &diagnostics,
                         PackageDeclaration &package) {
	std::map<std::string, const FailsafeYamlNode *> by_file;
	for (const auto &document : documents) {
		by_file[document.first] = &document.second;
	}
	const auto manifest = by_file.find("connector.yaml");
	if (manifest == by_file.end()) {
		diagnostics.Add(PackageDiagnosticCode::PACKAGE_IDENTITY, PackageDiagnosticPhase::SOURCE, RootMark());
		return false;
	}
	DecodeManifestSchema(manifest->first, *manifest->second, diagnostics, package.manifest);

	if (package.manifest.relations.size() != snapshot.RelationIds().size()) {
		diagnostics.Add(PackageDiagnosticCode::PACKAGE_IDENTITY, PackageDiagnosticPhase::SOURCE, package.manifest.mark);
		return false;
	}
	package.relations.reserve(package.manifest.relations.size());
	for (std::size_t index = 0; index < package.manifest.relations.size(); index++) {
		const auto &listed = package.manifest.relations[index];
		if (listed.value != snapshot.RelationIds()[index]) {
			diagnostics.Add(PackageDiagnosticCode::PACKAGE_IDENTITY, PackageDiagnosticPhase::SOURCE, listed.mark,
			                package.manifest.id.value);
			continue;
		}
		const auto file = "relations/" + listed.value + ".yaml";
		const auto found = by_file.find(file);
		if (found == by_file.end()) {
			diagnostics.Add(PackageDiagnosticCode::PACKAGE_IDENTITY, PackageDiagnosticPhase::SOURCE, listed.mark,
			                package.manifest.id.value);
			continue;
		}
		RelationDeclaration relation;
		DecodeRelationSchema(found->first, *found->second, diagnostics, relation);
		if (relation.id.value != listed.value) {
			diagnostics.Add(PackageDiagnosticCode::INVALID_REFERENCE, PackageDiagnosticPhase::REFERENCE,
			                relation.id.mark, package.manifest.id.value, listed.value, "", &listed.mark);
		}
		package.relations.push_back(std::move(relation));
	}
	return diagnostics.Empty();
}

} // namespace internal
} // namespace connector
} // namespace duckdb_api
