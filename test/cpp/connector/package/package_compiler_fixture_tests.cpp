#include "connector/support/package_compiler_test_fixtures.hpp"

#include "support/require.hpp"

#include <cstddef>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using duckdb_api::CompiledRegistrationAuthentication;
using duckdb_api::CompiledRegistrationRelation;
using duckdb_api::CompiledScalarType;
using duckdb_api_test::Require;

struct ExpectedColumn {
	const char *name;
	CompiledScalarType type;
	bool nullable;
};

using PackageFiles = std::map<std::string, std::string>;

const std::vector<std::string> EXPECTED_GITHUB_PACKAGE_FILES = {"connector.yaml",
                                                                "fixtures/github_authenticated_user.json",
                                                                "fixtures/github_repositories_page_1.json",
                                                                "fixtures/github_repositories_page_2.json",
                                                                "fixtures/github_search_page.json",
                                                                "fixtures/index.yaml",
                                                                "fixtures/private_visibility_duplicates.json",
                                                                "fixtures/private_visibility_duplicates_base.json",
                                                                "fixtures/private_visibility_false_or_null.json",
                                                                "fixtures/private_visibility_false_or_null_base.json",
                                                                "fixtures/private_visibility_matching.json",
                                                                "fixtures/private_visibility_matching_base.json",
                                                                "fixtures/viewer_repository_metrics_page_1.json",
                                                                "fixtures/viewer_repository_metrics_page_2.json",
                                                                "relations/authenticated_repositories.yaml",
                                                                "relations/authenticated_user.yaml",
                                                                "relations/duckdb_login_search_page.yaml",
                                                                "relations/viewer_repository_metrics.yaml"};

bool IsExcludedHumanAsset(const std::string &relative) {
	// The two root READMEs intentionally address different audiences. Every
	// other regular file is package or fixture custody regardless of extension.
	return relative == "README.md";
}

std::string ReadBytes(const std::string &path) {
	std::ifstream input(path.c_str(), std::ios::binary);
	if (!input) {
		throw std::runtime_error("could not read GitHub package drift input: " + path);
	}
	std::ostringstream output;
	output << input.rdbuf();
	return output.str();
}

class TemporaryInventoryTree {
public:
	TemporaryInventoryTree() {
		char pattern[] = "/private/tmp/duckdb-api-package-inventory-XXXXXX";
		const auto *created = ::mkdtemp(pattern);
		if (created == nullptr) {
			throw std::runtime_error("could not create package inventory test root");
		}
		root = created;
		if (::mkdir((root + "/relations").c_str(), 0700) != 0) {
			throw std::runtime_error("could not create package inventory test directory");
		}
	}

	~TemporaryInventoryTree() noexcept {
		for (auto iterator = files.rbegin(); iterator != files.rend(); ++iterator) {
			::unlink((root + "/" + *iterator).c_str());
		}
		::rmdir((root + "/relations").c_str());
		::rmdir(root.c_str());
	}

	TemporaryInventoryTree(const TemporaryInventoryTree &) = delete;
	TemporaryInventoryTree &operator=(const TemporaryInventoryTree &) = delete;

	void Write(const std::string &relative, const std::string &bytes) {
		std::ofstream output((root + "/" + relative).c_str(), std::ios::binary | std::ios::trunc);
		if (!output) {
			throw std::runtime_error("could not write package inventory test file");
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

void CollectPackageFiles(const std::string &root, const std::string &relative, PackageFiles &files) {
	const auto directory_path = relative.empty() ? root : root + "/" + relative;
	std::unique_ptr<DIR, int (*)(DIR *)> directory(::opendir(directory_path.c_str()), ::closedir);
	if (!directory) {
		throw std::runtime_error("could not inventory GitHub package drift root: " + directory_path);
	}
	while (const auto *entry = ::readdir(directory.get())) {
		const std::string name = entry->d_name;
		if (name == "." || name == "..") {
			continue;
		}
		const auto child_relative = relative.empty() ? name : relative + "/" + name;
		const auto child_path = root + "/" + child_relative;
		struct stat status;
		if (::lstat(child_path.c_str(), &status) != 0) {
			throw std::runtime_error("could not inspect GitHub package drift input: " + child_path);
		}
		if (S_ISLNK(status.st_mode)) {
			throw std::runtime_error("GitHub package drift input contains a symbolic link: " + child_relative);
		}
		if (S_ISDIR(status.st_mode)) {
			CollectPackageFiles(root, child_relative, files);
		} else if (S_ISREG(status.st_mode)) {
			if (!IsExcludedHumanAsset(child_relative)) {
				files.emplace(child_relative, ReadBytes(child_path));
			}
		} else if (!S_ISREG(status.st_mode)) {
			throw std::runtime_error("GitHub package drift input contains a special file: " + child_relative);
		}
	}
}

PackageFiles InventoryPackageFiles(const std::string &root) {
	PackageFiles files;
	CollectPackageFiles(root, "", files);
	return files;
}

std::vector<std::string> PackageFileNames(const PackageFiles &files) {
	std::vector<std::string> names;
	for (const auto &entry : files) {
		names.push_back(entry.first);
	}
	return names;
}

void RequireMatchingPackageBytes(const PackageFiles &product, const PackageFiles &evidence) {
	Require(product == evidence, "permanent GitHub package bytes differ from accepted RFC 0013 evidence");
}

void RequireExactGithubPackageMirror(const PackageFiles &product, const PackageFiles &evidence) {
	Require(PackageFileNames(product) == EXPECTED_GITHUB_PACKAGE_FILES,
	        "permanent GitHub package file inventory drifted");
	Require(PackageFileNames(evidence) == EXPECTED_GITHUB_PACKAGE_FILES,
	        "RFC 0013 GitHub evidence file inventory drifted");
	RequireMatchingPackageBytes(product, evidence);
}

void RequireMirrorFailure(const PackageFiles &product, const PackageFiles &evidence, const std::string &message) {
	try {
		RequireExactGithubPackageMirror(product, evidence);
	} catch (const std::runtime_error &) {
		return;
	}
	throw std::runtime_error(message);
}

void RequireByteMismatchFailure(const PackageFiles &product, const PackageFiles &evidence, const std::string &message) {
	try {
		RequireMatchingPackageBytes(product, evidence);
	} catch (const std::runtime_error &) {
		return;
	}
	throw std::runtime_error(message);
}

void TestPackageInventoryIncludesAllNestedRegularFiles() {
	TemporaryInventoryTree package;
	package.Write("README.md", "root human asset\n");
	package.Write("relations/unexpected.body", "fixture body\n");
	package.Write("relations/README.md", "nested package file\n");
	const auto files = InventoryPackageFiles(package.Root());
	Require(files.size() == 2 && files.at("relations/unexpected.body") == "fixture body\n" &&
	            files.at("relations/README.md") == "nested package file\n",
	        "drift inventory did not include every nested regular file or excluded more than the root README");
}

void TestPermanentGithubPackageTracksAcceptedEvidence(const std::string &repository_root) {
	TestPackageInventoryIncludesAllNestedRegularFiles();
	Require(IsExcludedHumanAsset("README.md") && !IsExcludedHumanAsset("fixtures/README.md") &&
	            !IsExcludedHumanAsset("fixtures/unexpected.body"),
	        "drift inventory excluded a nested or non-README package file");
	const auto product = InventoryPackageFiles(repository_root + "/connectors/github");
	const auto evidence = InventoryPackageFiles(repository_root + "/docs/rfcs/evidence/0013/github");
	RequireExactGithubPackageMirror(product, evidence);

	auto extra_product = product;
	extra_product["fixtures/nested/unexpected.body"] = "product body\n";
	RequireMirrorFailure(extra_product, evidence, "drift gate accepted an extra permanent-package file");
	auto extra_evidence = evidence;
	extra_evidence["fixtures/nested/unexpected.body"] = "evidence body\n";
	RequireMirrorFailure(product, extra_evidence, "drift gate accepted an extra RFC-evidence file");
	auto nested_readme = product;
	nested_readme["fixtures/README.md"] = "nested package file\n";
	RequireMirrorFailure(nested_readme, evidence, "drift gate excluded a nested README from package custody");
	auto body_product = product;
	auto body_evidence = evidence;
	body_product["fixtures/referenced.body"] = "product body\n";
	body_evidence["fixtures/referenced.body"] = "evidence body\n";
	RequireByteMismatchFailure(body_product, body_evidence, "drift gate accepted mismatched non-JSON fixture bytes");
	auto changed_product = product;
	changed_product["connector.yaml"] += "\n";
	RequireMirrorFailure(changed_product, evidence, "drift gate accepted changed semantic package bytes");

	const auto readme = ReadBytes(repository_root + "/connectors/github/README.md");
	Require(readme.find("CALL duckdb_api_load_connector") != std::string::npos &&
	            readme.find("CREATE TEMPORARY SECRET github_default") != std::string::npos &&
	            readme.find("make test") != std::string::npos,
	        "permanent GitHub package README omits load, secret, or validation guidance");
}

class NeverCancel final : public duckdb_api::connector::PackageCancellation {
public:
	bool IsCancellationRequested() const noexcept override {
		return false;
	}
};

void RequireRelation(const CompiledRegistrationRelation &relation, const char *name,
                     CompiledRegistrationAuthentication authentication,
                     const std::vector<ExpectedColumn> &expected_columns) {
	Require(relation.Name() == name, std::string("registration relation order/name drifted at ") + name);
	Require(relation.Inputs().empty(), std::string("registration unexpectedly exposed relation inputs for ") + name);
	Require(relation.Authentication() == authentication,
	        std::string("registration authentication shape drifted for ") + name);
	Require(relation.Columns().size() == expected_columns.size(),
	        std::string("registration column count drifted for ") + name);
	for (std::size_t index = 0; index < expected_columns.size(); index++) {
		const auto &actual = relation.Columns()[index];
		const auto &expected = expected_columns[index];
		Require(actual.Name() == expected.name && actual.Type() == expected.type &&
		            actual.Nullable() == expected.nullable,
		        std::string("registration column shape drifted for ") + name + "." + expected.name);
	}
}

} // namespace

int main(int argc, char **argv) {
	try {
		Require(argc == 2, "usage: package_compiler_fixture_tests ABSOLUTE_REPOSITORY_ROOT");
		TestPermanentGithubPackageTracksAcceptedEvidence(argv[1]);
		const auto registration = duckdb_api_test::CompileRepositoryGithubRegistrationFixture(argv[1]);
		const auto &identity = registration.Identity();
		Require(identity.SpecIdentifier() == "duckdb_api/v1" && identity.ConnectorId() == "github" &&
		            identity.PackageVersion() == "1.0.0" &&
		            identity.PackageDigest() ==
		                "sha256.b286e6f7481b437b243dfe2ce017a59d601d909272b9d2b35788fb78753ff23b" &&
		            registration.GenerationHandle().IsValid(),
		        "Connector compiler fixture did not expose the exact real package identity and generation");
		Require(registration.Relations().size() == 4,
		        "Connector compiler fixture did not expose the real four-relation registration view");
		const auto github = duckdb_api_test::CompileRepositoryGithubLocalPackageFixture(argv[1]);
		const auto distinct = duckdb_api_test::CompileRepositoryDistinctLocalPackageFixture(argv[1]);
		Require(github.IsValid() && distinct.IsValid() &&
		            distinct.Generation().Identity().ConnectorId() == "github_distinct" &&
		            distinct.Generation().Identity().PackageVersion() == "1.0.0" &&
		            distinct.Generation().Identity().PackageDigest() !=
		                github.Generation().Identity().PackageDigest() &&
		            distinct.MatchesGeneration(distinct.Generation().OpaqueHandle()) &&
		            !distinct.MatchesGeneration(github.Generation().OpaqueHandle()),
		        "distinct compiler-produced local-package fixture lost exact identity/custody");
		NeverCancel cancellation;
		const auto distinct_reload =
		    duckdb_api::connector::RecompileLocalPackage(distinct, distinct.Generation().OpaqueHandle(), cancellation);
		Require(distinct_reload.Succeeded() && distinct_reload.Package() != nullptr &&
		            distinct_reload.Package()->MatchesGeneration(distinct_reload.Generation()->OpaqueHandle()) &&
		            distinct_reload.Generation()->Identity().ConnectorId() == "github_distinct",
		        "distinct local-package fixture did not retain its real source custody");

		const auto anonymous = CompiledRegistrationAuthentication::ANONYMOUS;
		const auto required = CompiledRegistrationAuthentication::LOGICAL_SECRET_REQUIRED;
		const auto bigint = CompiledScalarType::BIGINT;
		const auto varchar = CompiledScalarType::VARCHAR;
		const auto boolean = CompiledScalarType::BOOLEAN;
		RequireRelation(registration.Relations()[0], "duckdb_login_search_page", anonymous,
		                {{"id", bigint, false}, {"login", varchar, false}, {"site_admin", boolean, false}});
		RequireRelation(registration.Relations()[1], "authenticated_user", required,
		                {{"id", bigint, false}, {"login", varchar, false}, {"site_admin", boolean, false}});
		RequireRelation(registration.Relations()[2], "authenticated_repositories", required,
		                {{"id", bigint, false},
		                 {"full_name", varchar, false},
		                 {"private", boolean, false},
		                 {"fork", boolean, false},
		                 {"archived", boolean, false},
		                 {"visibility", varchar, false}});
		RequireRelation(registration.Relations()[3], "viewer_repository_metrics", required,
		                {{"id", varchar, false},
		                 {"full_name", varchar, false},
		                 {"owner_login", varchar, false},
		                 {"stars", bigint, false},
		                 {"primary_language", varchar, true},
		                 {"private", boolean, false},
		                 {"archived", boolean, false},
		                 {"updated_at", varchar, false}});
		std::cout << "package compiler fixture tests passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
