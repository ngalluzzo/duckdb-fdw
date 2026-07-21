#pragma once

#include "package_declarations.hpp"

#include "duckdb_api/compiled_package_generation.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace duckdb_api {
namespace connector {
namespace internal {

PackageCompileResult PackageSourceFailureResult(const PackageSourceError &error, std::uint64_t maximum_diagnostics);
PackageCompileResult PackageSyntaxFailureResult(const FailsafeYamlError &error, std::uint64_t maximum_diagnostics);

bool DecodePackageSchema(const std::vector<std::pair<std::string, FailsafeYamlNode>> &documents,
                         const PackageSourceSnapshot &snapshot, PackageDiagnosticSink &diagnostics,
                         PackageDeclaration &package, PackageCancellation &cancellation);
bool DecodeManifestSchema(const std::string &file, const FailsafeYamlNode &root, PackageDiagnosticSink &diagnostics,
                          ManifestDeclaration &manifest);
bool DecodeRelationSchema(const std::string &file, const FailsafeYamlNode &root, PackageDiagnosticSink &diagnostics,
                          RelationDeclaration &relation);

std::shared_ptr<const CompiledPackageGeneration> CompilePackageDeclaration(const PackageDeclaration &package,
                                                                           const PackageSourceSnapshot &snapshot,
                                                                           PackageDiagnosticSink &diagnostics,
                                                                           PackageCancellation &cancellation);

struct RenderedGraphqlOperation {
	std::shared_ptr<const CompiledGraphqlQueryRecipe> query_recipe;
	std::string document;
	std::vector<CompiledGraphqlResultColumn> result_columns;
	CompiledGraphqlResponse response;
	CompiledGraphqlCursorPagination cursor;
};

bool RenderGraphqlOperation(const RelationDeclaration &relation, const OperationDeclaration &operation,
                            PackageDiagnosticSink &diagnostics, RenderedGraphqlOperation &rendered);

} // namespace internal
} // namespace connector
} // namespace duckdb_api
