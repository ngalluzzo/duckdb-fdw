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

std::string Digest(const std::vector<SemanticSourceFile> &files) {
	duckdb_api_test::NeverCancel cancellation;
	std::string digest;
	Require(ComputePackageDigest(files, cancellation, digest), "uncancelled digest did not complete");
	return digest;
}

class CountdownCancellation final : public duckdb_api::connector::PackageCancellation {
public:
	explicit CountdownCancellation(std::uint64_t cancel_at_p) : cancel_at(cancel_at_p), calls(0) {
	}

	bool IsCancellationRequested() const noexcept override {
		calls++;
		return calls == cancel_at;
	}

	std::uint64_t cancel_at;
	mutable std::uint64_t calls;
};

void TestFramingVectors() {
	Require(Digest({}) == "sha256.e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
	        "empty framing vector drifted");
	Require(Digest({{"a", "x"}}) == "sha256.e8197eec1ea18962a4f72c2f992a3a779b244cf58feae22a64cba25cbea5ae5b",
	        "single-file framing vector drifted");
	Require(Digest({{"b", "c"}, {"a", ""}}) ==
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
	Require(Digest(files) == expected, "accepted GitHub package digest drifted");
	std::reverse(files.begin(), files.end());
	Require(Digest(files) == expected, "package digest depends on caller file order");
	files[0].bytes.push_back('\n');
	Require(Digest(files) != expected, "raw semantic byte change did not change package digest");
}

void TestInvalidAndCollidingPaths() {
	const std::vector<std::string> invalid = {"/a", "a\\b", "a//b", "a/./b", "a/../b", "a/"};
	for (const auto &path : invalid) {
		try {
			(void)Digest({{path, "x"}});
			throw std::runtime_error("invalid semantic path was accepted");
		} catch (const std::invalid_argument &) {
		}
	}
	try {
		(void)Digest({{"relations/A.yaml", "x"}, {"relations/B.yaml", "z"}, {"relations/a.yaml", "y"}});
		throw std::runtime_error("case-colliding semantic paths were accepted");
	} catch (const std::invalid_argument &) {
	}
}

void TestEveryDigestUnitCanCancelWithoutPublishingOutput() {
	const std::vector<SemanticSourceFile> files = {{"a", "x"}};
	// One file has before/after checks for validation, size accounting, and
	// framing, followed by before/after checks for the bounded SHA-256 unit.
	for (std::uint64_t cancel_at = 1; cancel_at <= 8; cancel_at++) {
		CountdownCancellation cancellation(cancel_at);
		std::string output = "unchanged";
		Require(!ComputePackageDigest(files, cancellation, output) && output == "unchanged" &&
		            cancellation.calls == cancel_at,
		        "digest cancellation published output or skipped a bounded checkpoint");
	}
	CountdownCancellation not_cancelled(9);
	std::string output;
	Require(ComputePackageDigest(files, not_cancelled, output) && not_cancelled.calls == 8 && !output.empty(),
	        "digest did not complete after every bounded checkpoint remained active");
}

} // namespace

int main() {
	try {
		TestFramingVectors();
		TestAcceptedGithubDigest();
		TestInvalidAndCollidingPaths();
		TestEveryDigestUnitCanCancelWithoutPublishingOutput();
		std::cout << "package digest tests passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "package digest tests failed: " << error.what() << std::endl;
		return 1;
	}
}
