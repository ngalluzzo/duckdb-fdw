#pragma once

#include "duckdb_api/local_package_compiler.hpp"
#include "duckdb_api/internal/connector/package/package_source.hpp"

#include <memory>

namespace duckdb_api {
namespace internal {

// Connector-private construction and custody access. Keeping these operations
// outside the public value prevents another team from forging a generation /
// canonical-root pair or inspecting package source.
class CompiledLocalPackageAccess {
public:
	static CompiledLocalPackage Create(std::shared_ptr<const CompiledPackageGeneration> generation,
	                                   connector::PackageSourceSnapshot source);
	static const connector::PackageSourceSnapshot &Source(const CompiledLocalPackage &package);
};

} // namespace internal
} // namespace duckdb_api
