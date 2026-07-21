#include "connector/support/package_compiler_test_fixtures.hpp"

#include "duckdb_api/local_package_compiler.hpp"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <ftw.h>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace duckdb_api_test {

namespace {

class NeverCancel final : public duckdb_api::connector::PackageCancellation {
public:
	bool IsCancellationRequested() const noexcept override {
		return false;
	}
};

int RemoveEntry(const char *path, const struct stat *, int, struct FTW *) {
	return ::remove(path);
}

class FixtureRootCustody {
public:
	~FixtureRootCustody() noexcept {
		for (const auto &root : roots) {
			(void)::nftw(root.c_str(), RemoveEntry, 32, FTW_DEPTH | FTW_PHYS);
		}
	}

	void Retain(std::string root) {
		std::lock_guard<std::mutex> guard(lock);
		roots.push_back(std::move(root));
	}

private:
	std::mutex lock;
	std::vector<std::string> roots;
};

FixtureRootCustody &RetainedFixtureRoots() {
	static FixtureRootCustody custody;
	return custody;
}

std::string ReadFile(const std::string &path) {
	std::ifstream input(path.c_str(), std::ios::binary);
	if (!input) {
		throw std::runtime_error("could not read repository local-package fixture source");
	}
	std::ostringstream result;
	result << input.rdbuf();
	return result.str();
}

void WriteFile(const std::string &path, const std::string &bytes) {
	const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0) {
		throw std::runtime_error("could not open repository local-package fixture output");
	}
	std::size_t offset = 0;
	while (offset < bytes.size()) {
		const auto written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
		if (written < 0) {
			const int saved = errno;
			::close(fd);
			errno = saved;
			throw std::runtime_error("could not write repository local-package fixture output");
		}
		offset += static_cast<std::size_t>(written);
	}
	if (::close(fd) != 0) {
		throw std::runtime_error("could not close repository local-package fixture output");
	}
}

std::string WithDistinctConnectorId(std::string manifest) {
	const std::string anchor = "\nid: github\n";
	const auto offset = manifest.find(anchor);
	if (offset == std::string::npos) {
		throw std::runtime_error("repository package connector-id anchor is missing");
	}
	manifest.replace(offset, anchor.size(), "\nid: github_distinct\n");
	return manifest;
}

} // namespace

duckdb_api::CompiledQueryRegistrationView
CompileRepositoryGithubRegistrationFixture(const std::string &absolute_repository_root) {
	return CompileRepositoryGithubLocalPackageFixture(absolute_repository_root).Generation().QueryRegistration();
}

duckdb_api::CompiledLocalPackage
CompileRepositoryGithubLocalPackageFixture(const std::string &absolute_repository_root) {
	NeverCancel cancellation;
	const auto result = duckdb_api::connector::CompileLocalPackageRoot(
	    absolute_repository_root + "/docs/rfcs/evidence/0013/github", cancellation);
	if (!result.Succeeded() || result.Package() == nullptr) {
		throw std::runtime_error("repository GitHub connector package fixture did not compile");
	}
	return duckdb_api::CompiledLocalPackage(*result.Package());
}

duckdb_api::CompiledLocalPackage
CompileRepositoryDistinctLocalPackageFixture(const std::string &absolute_repository_root) {
	char pattern[] = "/private/tmp/duckdb-api-distinct-fixture-XXXXXX";
	const auto *created = ::mkdtemp(pattern);
	if (!created) {
		throw std::runtime_error("could not create distinct local-package fixture root");
	}
	const std::string root = created;
	try {
		if (::mkdir((root + "/relations").c_str(), 0700) != 0) {
			throw std::runtime_error("could not create distinct local-package fixture relations");
		}
		const std::string evidence = absolute_repository_root + "/docs/rfcs/evidence/0013/github/";
		WriteFile(root + "/connector.yaml", WithDistinctConnectorId(ReadFile(evidence + "connector.yaml")));
		for (const auto &relation : {"authenticated_repositories", "authenticated_user", "duckdb_login_search_page",
		                             "viewer_repository_metrics"}) {
			WriteFile(root + "/relations/" + std::string(relation) + ".yaml",
			          ReadFile(evidence + "relations/" + std::string(relation) + ".yaml"));
		}
		NeverCancel cancellation;
		const auto result = duckdb_api::connector::CompileLocalPackageRoot(root, cancellation);
		if (!result.Succeeded() || result.Package() == nullptr) {
			throw std::runtime_error("distinct local-package fixture did not compile");
		}
		duckdb_api::CompiledLocalPackage package(*result.Package());
		RetainedFixtureRoots().Retain(root);
		return package;
	} catch (...) {
		(void)::nftw(root.c_str(), RemoveEntry, 32, FTW_DEPTH | FTW_PHYS);
		throw;
	}
}

} // namespace duckdb_api_test
