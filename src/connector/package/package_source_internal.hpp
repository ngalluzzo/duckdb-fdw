#pragma once

#include "duckdb_api/internal/connector/package/package_source.hpp"

#include <memory>
#include <string>
#include <vector>

namespace duckdb_api {
namespace connector {
namespace internal {

// Private POSIX custody boundary. It owns the opened root and relations
// descriptors, pre-read directory/identity captures, byte accounting, and
// post-read comparison. Semantic orchestration can request only the manifest
// and explicitly listed relation leaves; it cannot enumerate or open fixtures.
class PackageDirectoryCustody {
public:
	static PackageDirectoryCustody OpenAbsolute(const std::string &absolute_root,
	                                            const PackageSourceLimits &host_limits,
	                                            PackageCancellation &cancellation);
	static PackageDirectoryCustody OpenRetained(int retained_root_fd, const PackageSourceLimits &host_limits,
	                                            PackageCancellation &cancellation);

	PackageDirectoryCustody(PackageDirectoryCustody &&other) noexcept;
	PackageDirectoryCustody &operator=(PackageDirectoryCustody &&other) noexcept;
	~PackageDirectoryCustody() noexcept;

	PackageDirectoryCustody(const PackageDirectoryCustody &) = delete;
	PackageDirectoryCustody &operator=(const PackageDirectoryCustody &) = delete;

	const PackageSourceLimits &Limits() const noexcept;
	std::string ReadManifest(PackageCancellation &cancellation);
	void ValidateListedRelations(const std::vector<std::string> &relation_ids) const;
	std::string ReadRelation(const std::string &relation_id, PackageCancellation &cancellation);
	void VerifyUnchanged(PackageCancellation &cancellation);
	int ReleaseRoot() noexcept;

private:
	class Impl;
	explicit PackageDirectoryCustody(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl;
};

} // namespace internal
} // namespace connector
} // namespace duckdb_api
