#include "package_fixture_candidate_internal.hpp"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace duckdb_api {
namespace connector {
namespace internal {
namespace {

void CheckCancellation(PackageCancellation &cancellation) {
	if (cancellation.IsCancellationRequested()) {
		throw PackageCompilationCancelled();
	}
}

bool IsIdentifier(const std::string &value) {
	if (value.empty() || value.size() > 63 || value.front() < 'a' || value.front() > 'z') {
		return false;
	}
	for (const auto character : value) {
		if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_')) {
			return false;
		}
	}
	return true;
}

bool IsSemanticPath(const std::string &path) {
	if (path == "connector.yaml") {
		return true;
	}
	const std::string prefix = "relations/";
	const std::string suffix = ".yaml";
	return path.size() > prefix.size() + suffix.size() && path.compare(0, prefix.size(), prefix) == 0 &&
	       path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0 &&
	       IsIdentifier(path.substr(prefix.size(), path.size() - prefix.size() - suffix.size()));
}

void RemoveTree(const std::string &root, const std::vector<std::string> &files) noexcept {
	for (auto iterator = files.rbegin(); iterator != files.rend(); ++iterator) {
		(void)::unlink((root + "/" + *iterator).c_str());
	}
	(void)::rmdir((root + "/relations").c_str());
	(void)::rmdir(root.c_str());
}

void WriteFile(const std::string &path, const std::string &bytes) {
	const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) {
		throw std::runtime_error("private fixture candidate leaf could not be created");
	}
	std::size_t offset = 0;
	while (offset < bytes.size()) {
		const auto written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
		if (written < 0) {
			if (errno == EINTR) {
				continue;
			}
			const int saved = errno;
			(void)::close(fd);
			errno = saved;
			throw std::runtime_error("private fixture candidate leaf could not be written");
		}
		offset += static_cast<std::size_t>(written);
	}
	if (::close(fd) != 0) {
		throw std::runtime_error("private fixture candidate leaf could not be closed");
	}
}

std::string CreateRoot() {
#if defined(__APPLE__)
	char pattern[] = "/private/tmp/duckdb-api-fixture-candidate-XXXXXX";
#else
	char pattern[] = "/tmp/duckdb-api-fixture-candidate-XXXXXX";
#endif
	const auto *created = ::mkdtemp(pattern);
	if (created == nullptr) {
		throw std::runtime_error("private fixture candidate root could not be created");
	}
	return created;
}

} // namespace

PrivatePackageSourceCopy::PrivatePackageSourceCopy(std::string root_p, std::vector<std::string> files_p)
    : root(std::move(root_p)), files(std::move(files_p)) {
}

std::unique_ptr<PrivatePackageSourceCopy>
PrivatePackageSourceCopy::Create(const std::vector<SemanticSourceFile> &source_files,
                                 PackageCancellation &cancellation) {
	CheckCancellation(cancellation);
	const auto root = CreateRoot();
	std::vector<std::string> written;
	try {
		if (::mkdir((root + "/relations").c_str(), 0700) != 0) {
			throw std::runtime_error("private fixture candidate relation directory could not be created");
		}
		for (const auto &file : source_files) {
			CheckCancellation(cancellation);
			if (!IsSemanticPath(file.path)) {
				throw std::logic_error("compiled package retained a source outside the v1 semantic grammar");
			}
			written.push_back(file.path);
			WriteFile(root + "/" + file.path, file.bytes);
		}
		CheckCancellation(cancellation);
		return std::unique_ptr<PrivatePackageSourceCopy>(new PrivatePackageSourceCopy(root, std::move(written)));
	} catch (...) {
		RemoveTree(root, written);
		throw;
	}
}

PrivatePackageSourceCopy::~PrivatePackageSourceCopy() noexcept {
	RemoveTree(root, files);
}

const std::string &PrivatePackageSourceCopy::Root() const noexcept {
	return root;
}

} // namespace internal
} // namespace connector
} // namespace duckdb_api
