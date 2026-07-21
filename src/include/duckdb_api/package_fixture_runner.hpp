#pragma once

#include "duckdb_api/compiled_package_generation.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace duckdb_api {
namespace connector {

namespace internal {
class PackageFixtureCoverageBuilder;
}

// Immutable result of the project-owned fixture-coverage mapping. Required
// keys preserve the accepted mapping order; OrderedDigest() hashes each key
// followed by LF. The value is derived solely from compiled semantic facts
// before fixture source is opened, so author evidence cannot reduce its scope.
class PackageFixtureCoverage {
public:
	PackageFixtureCoverage(const PackageFixtureCoverage &) = default;
	PackageFixtureCoverage(PackageFixtureCoverage &&) = default;
	PackageFixtureCoverage &operator=(const PackageFixtureCoverage &) = delete;
	PackageFixtureCoverage &operator=(PackageFixtureCoverage &&) = delete;

	const std::vector<std::string> &RequiredKeys() const noexcept;
	const std::string &OrderedDigest() const noexcept;

private:
	friend PackageFixtureCoverage DerivePackageFixtureCoverage(const CompiledPackageGeneration &);
	friend class internal::PackageFixtureCoverageBuilder;
	PackageFixtureCoverage(std::vector<std::string> required_keys, std::string ordered_digest);

	std::vector<std::string> required_keys;
	std::string ordered_digest;
};

// Applies exact duckdb_api/fixture_coverage_v1 scope and rule ordering to one
// immutable package generation. This function performs no source, fixture,
// network, credential, planning, execution, or publication work. Unknown IR,
// duplicate keys, or a key outside the 255-byte grammar fails closed.
PackageFixtureCoverage DerivePackageFixtureCoverage(const CompiledPackageGeneration &generation);

} // namespace connector
} // namespace duckdb_api
