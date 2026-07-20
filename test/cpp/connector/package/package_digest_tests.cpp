#include "duckdb_api/internal/connector/package/package_digest.hpp"

#include "connector/package/test_support.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using duckdb_api::connector::ComputePackageDigest;
using duckdb_api::connector::SemanticSourceFile;
using duckdb_api_test::ReadFile;
using duckdb_api_test::Require;

void TestFramingVectors() {
	Require(ComputePackageDigest({}) == "sha256.e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
	        "empty framing vector drifted");
	Require(ComputePackageDigest({{"a", "x"}}) ==
	            "sha256.e8197eec1ea18962a4f72c2f992a3a779b244cf58feae22a64cba25cbea5ae5b",
	        "single-file framing vector drifted");
	Require(ComputePackageDigest({{"b", "c"}, {"a", ""}}) ==
	            "sha256.67828f4f00b886c92b0a49b80f76c7317caa87d8501a0d4cb2a35be3fa209cfd",
	        "multi-file sorted framing vector drifted");
}

void TestAcceptedGithubDigest() {
	std::vector<SemanticSourceFile> files = {
	    {"connector.yaml", ReadFile("docs/rfcs/evidence/0013/github/connector.yaml")},
	    {"relations/authenticated_repositories.yaml",
	     ReadFile("docs/rfcs/evidence/0013/github/relations/authenticated_repositories.yaml")},
	    {"relations/authenticated_user.yaml",
	     ReadFile("docs/rfcs/evidence/0013/github/relations/authenticated_user.yaml")},
	    {"relations/duckdb_login_search_page.yaml",
	     ReadFile("docs/rfcs/evidence/0013/github/relations/duckdb_login_search_page.yaml")},
	    {"relations/viewer_repository_metrics.yaml",
	     ReadFile("docs/rfcs/evidence/0013/github/relations/viewer_repository_metrics.yaml")}};
	const auto expected = "sha256.b286e6f7481b437b243dfe2ce017a59d601d909272b9d2b35788fb78753ff23b";
	Require(ComputePackageDigest(files) == expected, "accepted GitHub package digest drifted");
	std::reverse(files.begin(), files.end());
	Require(ComputePackageDigest(files) == expected, "package digest depends on caller file order");
	files[0].bytes.push_back('\n');
	Require(ComputePackageDigest(files) != expected, "raw semantic byte change did not change package digest");
}

void TestInvalidAndCollidingPaths() {
	const std::vector<std::string> invalid = {"/a", "a\\b", "a//b", "a/./b", "a/../b", "a/"};
	for (const auto &path : invalid) {
		try {
			(void)ComputePackageDigest({{path, "x"}});
			throw std::runtime_error("invalid semantic path was accepted");
		} catch (const std::invalid_argument &) {
		}
	}
	try {
		(void)ComputePackageDigest({{"relations/A.yaml", "x"}, {"relations/B.yaml", "z"}, {"relations/a.yaml", "y"}});
		throw std::runtime_error("case-colliding semantic paths were accepted");
	} catch (const std::invalid_argument &) {
	}
}

} // namespace

int main() {
	try {
		TestFramingVectors();
		TestAcceptedGithubDigest();
		TestInvalidAndCollidingPaths();
		std::cout << "package digest tests passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "package digest tests failed: " << error.what() << std::endl;
		return 1;
	}
}
