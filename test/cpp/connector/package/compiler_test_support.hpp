#pragma once

#include "duckdb_api/internal/connector/package/package_compiler.hpp"

#include "test_support.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace duckdb_api_test {

class TemporaryPackage {
public:
	TemporaryPackage() {
		char pattern[] = "/private/tmp/duckdb-api-compiler-XXXXXX";
		const auto *created = ::mkdtemp(pattern);
		if (created == nullptr) {
			throw std::runtime_error("could not create compiler test package root");
		}
		root = created;
		if (::mkdir((root + "/relations").c_str(), 0700) != 0) {
			throw std::runtime_error("could not create compiler test relation directory");
		}
	}

	~TemporaryPackage() noexcept {
		for (auto iterator = files.rbegin(); iterator != files.rend(); ++iterator) {
			::unlink((root + "/" + *iterator).c_str());
		}
		::rmdir((root + "/relations").c_str());
		::rmdir(root.c_str());
	}

	TemporaryPackage(const TemporaryPackage &) = delete;
	TemporaryPackage &operator=(const TemporaryPackage &) = delete;

	void Write(const std::string &relative, const std::string &bytes) {
		std::ofstream output((root + "/" + relative).c_str(), std::ios::binary | std::ios::trunc);
		if (!output) {
			throw std::runtime_error("could not write compiler test source");
		}
		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		output.close();
		files.push_back(relative);
	}

	const std::string &Root() const {
		return root;
	}

private:
	std::string root;
	std::vector<std::string> files;
};

inline std::string ReplaceOnce(std::string value, const std::string &before, const std::string &after) {
	const auto offset = value.find(before);
	if (offset == std::string::npos) {
		throw std::runtime_error("compiler test mutation anchor was not found");
	}
	value.replace(offset, before.size(), after);
	return value;
}

inline void WriteGithubPackage(TemporaryPackage &package, const std::string &graphql_relation = std::string()) {
	const std::string github = "docs/rfcs/evidence/0013/github/";
	package.Write("connector.yaml", ReadFile(github + "connector.yaml"));
	for (const auto &relation : {"duckdb_login_search_page", "authenticated_user", "authenticated_repositories"}) {
		package.Write("relations/" + std::string(relation) + ".yaml",
		              ReadFile(github + "relations/" + std::string(relation) + ".yaml"));
	}
	package.Write("relations/viewer_repository_metrics.yaml",
	              graphql_relation.empty() ? ReadFile(github + "relations/viewer_repository_metrics.yaml")
	                                       : graphql_relation);
}

inline duckdb_api::connector::PackageCompileResult CompileRoot(const std::string &root, NeverCancel &cancellation) {
	return duckdb_api::connector::CompileLocalPackageRoot(root, cancellation);
}

} // namespace duckdb_api_test
