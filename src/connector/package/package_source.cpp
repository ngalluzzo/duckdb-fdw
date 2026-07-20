#include "duckdb_api/internal/connector/package/package_source.hpp"

#include "duckdb_api/internal/connector/package/failsafe_yaml.hpp"
#include "package_source_internal.hpp"

#include <set>
#include <stdexcept>
#include <unistd.h>
#include <utility>

namespace duckdb_api {
namespace connector {

PackageSourceError::PackageSourceError(PackageSourceErrorCode code_p, std::string path_p, std::string safe_message_p)
    : code(code_p), path(std::move(path_p)), safe_message(std::move(safe_message_p)) {
}

const char *PackageSourceError::what() const noexcept {
	return safe_message.c_str();
}

PackageSourceErrorCode PackageSourceError::Code() const noexcept {
	return code;
}

const std::string &PackageSourceError::Path() const noexcept {
	return path;
}

PackageSourceLimits PackageSourceLimits::V1() {
	return {65, 4096, 1024, 5120, 1024 * 1024, 16 * 1024 * 1024, 255};
}

class PackageSourceSnapshot::State {
public:
	State(int root_fd_p, std::vector<SemanticSourceFile> files_p, std::vector<std::string> relation_ids_p,
	      std::string digest_p)
	    : root_fd(root_fd_p), files(std::move(files_p)), relation_ids(std::move(relation_ids_p)),
	      digest(std::move(digest_p)) {
	}

	~State() noexcept {
		if (root_fd >= 0) {
			::close(root_fd);
		}
	}

	State(const State &) = delete;
	State &operator=(const State &) = delete;

	int root_fd;
	std::vector<SemanticSourceFile> files;
	std::vector<std::string> relation_ids;
	std::string digest;
};

PackageSourceSnapshot::PackageSourceSnapshot() {
}

PackageSourceSnapshot::PackageSourceSnapshot(std::shared_ptr<const State> state_p) : state(std::move(state_p)) {
}

const std::vector<SemanticSourceFile> &PackageSourceSnapshot::Files() const {
	if (!state) {
		throw std::logic_error("package source snapshot is empty");
	}
	return state->files;
}

const std::vector<std::string> &PackageSourceSnapshot::RelationIds() const {
	if (!state) {
		throw std::logic_error("package source snapshot is empty");
	}
	return state->relation_ids;
}

const std::string &PackageSourceSnapshot::Digest() const {
	if (!state) {
		throw std::logic_error("package source snapshot is empty");
	}
	return state->digest;
}

bool PackageSourceSnapshot::IsValid() const noexcept {
	return static_cast<bool>(state);
}

namespace {

bool IsIdentifier(const std::string &value) {
	if (value.empty() || value.size() > 63 || value[0] < 'a' || value[0] > 'z') {
		return false;
	}
	for (std::size_t index = 1; index < value.size(); index++) {
		const char character = value[index];
		if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_')) {
			return false;
		}
	}
	return true;
}

std::vector<std::string> ManifestRelationIds(const std::string &manifest, std::uint64_t max_relations,
                                             PackageCancellation &cancellation) {
	FailsafeYamlBudget budget(FailsafeYamlLimits::V1());
	const auto root = ParseFailsafeYaml("connector.yaml", manifest, budget, cancellation);
	if (root.Type() != FailsafeYamlNode::Kind::MAPPING) {
		throw PackageSourceError(PackageSourceErrorCode::MANIFEST_RELATIONS, "connector.yaml",
		                         "connector manifest must be a mapping");
	}
	const auto *relations = root.Find("relations");
	if (!relations || relations->Type() != FailsafeYamlNode::Kind::SEQUENCE || relations->Size() == 0) {
		throw PackageSourceError(PackageSourceErrorCode::MANIFEST_RELATIONS, "connector.yaml",
		                         "connector manifest requires a non-empty relation list");
	}
	std::vector<std::string> result;
	std::set<std::string> ids;
	result.reserve(relations->Size());
	for (std::size_t index = 0; index < relations->Size(); index++) {
		if (cancellation.IsCancellationRequested()) {
			throw PackageSourceError(PackageSourceErrorCode::CANCELLED, "connector.yaml",
			                         "package source acquisition was cancelled");
		}
		const auto &node = relations->SequenceValue(index);
		if (node.Type() != FailsafeYamlNode::Kind::SCALAR || !IsIdentifier(node.Scalar()) ||
		    !ids.insert(node.Scalar()).second) {
			throw PackageSourceError(PackageSourceErrorCode::MANIFEST_RELATIONS, "connector.yaml",
			                         "connector manifest relation IDs are invalid or duplicate");
		}
		if (result.size() >= max_relations) {
			throw PackageSourceError(PackageSourceErrorCode::RESOURCE_EXHAUSTED, "connector.yaml",
			                         "semantic source-file budget is exhausted");
		}
		result.push_back(node.Scalar());
	}
	return result;
}

struct AcquiredPackageSource {
	AcquiredPackageSource(int root_fd_p, std::vector<SemanticSourceFile> files_p,
	                      std::vector<std::string> relation_ids_p, std::string digest_p)
	    : root_fd(root_fd_p), files(std::move(files_p)), relation_ids(std::move(relation_ids_p)),
	      digest(std::move(digest_p)) {
	}
	AcquiredPackageSource(AcquiredPackageSource &&other) noexcept
	    : root_fd(other.ReleaseRoot()), files(std::move(other.files)), relation_ids(std::move(other.relation_ids)),
	      digest(std::move(other.digest)) {
	}
	~AcquiredPackageSource() noexcept {
		if (root_fd >= 0) {
			::close(root_fd);
		}
	}
	int ReleaseRoot() noexcept {
		const int result = root_fd;
		root_fd = -1;
		return result;
	}

	int root_fd;
	std::vector<SemanticSourceFile> files;
	std::vector<std::string> relation_ids;
	std::string digest;
};

AcquiredPackageSource AcquireFromCustody(internal::PackageDirectoryCustody custody, PackageCancellation &cancellation) {
	std::vector<SemanticSourceFile> files;
	files.push_back({"connector.yaml", custody.ReadManifest(cancellation)});
	auto relation_ids = ManifestRelationIds(files.front().bytes, custody.Limits().max_semantic_files - 1, cancellation);
	custody.ValidateListedRelations(relation_ids);
	files.reserve(relation_ids.size() + 1);
	for (const auto &id : relation_ids) {
		files.push_back({"relations/" + id + ".yaml", custody.ReadRelation(id, cancellation)});
	}
	custody.VerifyUnchanged(cancellation);
	auto digest = ComputePackageDigest(files);
	AcquiredPackageSource acquired(-1, std::move(files), std::move(relation_ids), std::move(digest));
	acquired.root_fd = custody.ReleaseRoot();
	return acquired;
}

} // namespace

PackageSourceSnapshot AcquirePackageSource(const std::string &absolute_root, const PackageSourceLimits &host_limits,
                                           PackageCancellation &cancellation) {
	auto acquired = AcquireFromCustody(
	    internal::PackageDirectoryCustody::OpenAbsolute(absolute_root, host_limits, cancellation), cancellation);
	const int root_fd = acquired.ReleaseRoot();
	try {
		auto state = std::make_shared<PackageSourceSnapshot::State>(
		    root_fd, std::move(acquired.files), std::move(acquired.relation_ids), std::move(acquired.digest));
		return PackageSourceSnapshot(std::move(state));
	} catch (...) {
		::close(root_fd);
		throw;
	}
}

PackageSourceSnapshot ReacquirePackageSource(const PackageSourceSnapshot &prior, const PackageSourceLimits &host_limits,
                                             PackageCancellation &cancellation) {
	if (!prior.state) {
		throw PackageSourceError(PackageSourceErrorCode::INVALID_ROOT, "", "prior package source snapshot is empty");
	}
	auto acquired = AcquireFromCustody(
	    internal::PackageDirectoryCustody::OpenRetained(prior.state->root_fd, host_limits, cancellation), cancellation);
	const int root_fd = acquired.ReleaseRoot();
	try {
		auto state = std::make_shared<PackageSourceSnapshot::State>(
		    root_fd, std::move(acquired.files), std::move(acquired.relation_ids), std::move(acquired.digest));
		return PackageSourceSnapshot(std::move(state));
	} catch (...) {
		::close(root_fd);
		throw;
	}
}

} // namespace connector
} // namespace duckdb_api
