#include "connector/support/package_compiler_test_fixtures.hpp"

#include "connector/support/catalog_test_access.hpp"

#include "duckdb_api/content_digest.hpp"
#include "duckdb_api/local_package_compiler.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <ftw.h>
#include <mutex>
#include <sstream>
#include <limits>
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

void FillHeadersToCombinedBytes(std::vector<duckdb_api::CompiledHttpHeader> &headers, std::size_t target) {
	std::size_t bytes = 0;
	for (const auto &header : headers) {
		bytes += header.name.size() + header.value.size();
	}
	for (std::size_t index = 0; bytes < target; index++) {
		const auto name = "X-Budget-" + std::to_string(index);
		const auto remaining = target - bytes;
		if (headers.size() == 32 || remaining < name.size()) {
			throw std::runtime_error("header-byte fixture cannot reach its exact target");
		}
		const auto value_size = std::min<std::size_t>(1024, remaining - name.size());
		headers.push_back({name, std::string(value_size, 'a')});
		bytes += name.size() + value_size;
	}
}

} // namespace

duckdb_api::CompiledQueryRegistrationView
CompileRepositoryGithubRegistrationFixture(const std::string &absolute_repository_root) {
	return CompileRepositoryGithubLocalPackageFixture(absolute_repository_root).Generation().QueryRegistration();
}

duckdb_api::CompiledLocalPackage
CompileRepositoryGithubLocalPackageFixture(const std::string &absolute_repository_root) {
	NeverCancel cancellation;
	const auto result =
	    duckdb_api::connector::CompileLocalPackageRoot(absolute_repository_root + "/connectors/github", cancellation);
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
		const std::string evidence = absolute_repository_root + "/connectors/github/";
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

duckdb_api::CompiledPackageGeneration
CompileRepositoryGithubGenerationFixture(const std::string &absolute_repository_root) {
	return CompileRepositoryGithubLocalPackageFixture(absolute_repository_root).Generation();
}

duckdb_api::CompiledPackageGeneration
CompileNonGithubGraphqlGenerationFixture(const std::string &absolute_repository_root) {
	NeverCancel cancellation;
	const auto result = duckdb_api::connector::CompileLocalPackageRoot(
	    absolute_repository_root + "/test/fixtures/package_graphql_non_github", cancellation);
	if (!result.Succeeded() || result.Package() == nullptr) {
		throw std::runtime_error("non-GitHub GraphQL connector package fixture did not compile");
	}
	return result.Package()->Generation();
}

duckdb_api::CompiledConnector
CompileRepositoryGithubGraphqlCounterexample(const std::string &absolute_repository_root,
                                             RepositoryGithubGraphqlCounterexample counterexample) {
	const auto generation = CompileRepositoryGithubGenerationFixture(absolute_repository_root);
	auto connector = generation.Connector();
	const auto *relation = connector.FindRelation("viewer_repository_metrics");
	if (relation == nullptr || relation->Operations().size() != 1) {
		throw std::runtime_error("repository GitHub GraphQL fixture lost its exact relation");
	}
	auto graphql = relation->Operations()[0].Graphql();
	switch (counterexample) {
	case RepositoryGithubGraphqlCounterexample::DOCUMENT_MISMATCH:
		graphql.document += " ";
		graphql.document_digest = duckdb_api::ComputeSha256Hex(graphql.document);
		break;
	case RepositoryGithubGraphqlCounterexample::DIGEST_MISMATCH:
		graphql.document_digest[0] = graphql.document_digest[0] == '0' ? '1' : '0';
		break;
	case RepositoryGithubGraphqlCounterexample::VARIABLE_MISMATCH:
		graphql.variables[0].name = "otherPageSize";
		break;
	case RepositoryGithubGraphqlCounterexample::RESPONSE_PATH_MISMATCH:
		graphql.response.nodes.segments.back() = "edges";
		break;
	case RepositoryGithubGraphqlCounterexample::COLUMN_MISMATCH:
		graphql.result_columns[0].response_path.segments[0] = "nodeId";
		break;
	case RepositoryGithubGraphqlCounterexample::CURSOR_MISMATCH:
		graphql.cursor.cursor_variable = "otherCursor";
		break;
	case RepositoryGithubGraphqlCounterexample::UNKNOWN_RECIPE_IDENTITY:
		graphql = ConnectorCatalogTestAccess::WithUnknownGraphqlRecipeIdentity(std::move(graphql));
		break;
	case RepositoryGithubGraphqlCounterexample::MIXED_CASE_AUTHORIZATION_HEADER:
		graphql.headers.push_back({"AUTHORIZATION", "public-test-value"});
		break;
	case RepositoryGithubGraphqlCounterexample::MIXED_CASE_HOST_HEADER:
		graphql.headers.push_back({"hOsT", "api.github.com"});
		break;
	case RepositoryGithubGraphqlCounterexample::MIXED_CASE_CONTENT_LENGTH_HEADER:
		graphql.headers.push_back({"cOnTeNt-LeNgTh", "1"});
		break;
	case RepositoryGithubGraphqlCounterexample::CASE_INSENSITIVE_DUPLICATE_HEADER:
		graphql.headers.push_back({"aCcEpT", "application/json"});
		break;
	case RepositoryGithubGraphqlCounterexample::MIXED_CASE_CONTENT_TYPE_MISMATCH:
		for (auto &header : graphql.headers) {
			if (header.name == "Content-Type") {
				header.name = "cOnTeNt-TyPe";
				header.value = "text/plain";
			}
		}
		break;
	case RepositoryGithubGraphqlCounterexample::INVALID_HEADER_NAME:
		graphql.headers.push_back({"Bad Header", "public-test-value"});
		break;
	case RepositoryGithubGraphqlCounterexample::INVALID_HEADER_VALUE:
		graphql.headers.push_back({"X-Test-Value", "safe\r\nInjected: value"});
		break;
	case RepositoryGithubGraphqlCounterexample::INVALID_ENDPOINT_PATH_GRAMMAR:
		graphql.endpoint_path = "/graphql?debug=true";
		break;
	case RepositoryGithubGraphqlCounterexample::TRAILING_ENDPOINT_PATH_SEPARATOR:
		graphql.endpoint_path = "/graphql/";
		break;
	case RepositoryGithubGraphqlCounterexample::ENDPOINT_PATH_TOO_LONG:
		graphql.endpoint_path = "/" + std::string(2048, 'a');
		break;
	case RepositoryGithubGraphqlCounterexample::ENDPOINT_PORT_OUTSIDE_POLICY:
		graphql.endpoint_origin.port = 8443;
		break;
	case RepositoryGithubGraphqlCounterexample::TOO_MANY_HEADERS:
		while (graphql.headers.size() <= 32) {
			graphql.headers.push_back({"X-Count-" + std::to_string(graphql.headers.size()), "public-test-value"});
		}
		break;
	case RepositoryGithubGraphqlCounterexample::HEADER_BYTES_EXCEEDED:
		FillHeadersToCombinedBytes(graphql.headers, 16ULL * 1024ULL + 1);
		break;
	case RepositoryGithubGraphqlCounterexample::RESPONSE_SCAN_SCOPE_EXCEEDED:
		return ConnectorCatalogTestAccess::WithInvalidRelationResources(
		    ConnectorCatalogTestAccess::WithInvalidGraphqlOperation(std::move(connector), "viewer_repository_metrics",
		                                                            "github_viewer_repository_metrics",
		                                                            std::move(graphql)),
		    "viewer_repository_metrics", 8ULL * 1024ULL * 1024ULL, 256ULL * 1024ULL * 1024ULL + 1, 100, 3200, 512);
	case RepositoryGithubGraphqlCounterexample::RECORD_SCAN_SCOPE_EXCEEDED:
		return ConnectorCatalogTestAccess::WithInvalidRelationResources(
		    ConnectorCatalogTestAccess::WithInvalidGraphqlOperation(std::move(connector), "viewer_repository_metrics",
		                                                            "github_viewer_repository_metrics",
		                                                            std::move(graphql)),
		    "viewer_repository_metrics", 8ULL * 1024ULL * 1024ULL, 64ULL * 1024ULL * 1024ULL, 100, 3201, 512);
	case RepositoryGithubGraphqlCounterexample::RESOURCE_PRODUCT_OVERFLOW:
		return ConnectorCatalogTestAccess::WithInvalidRelationResources(
		    ConnectorCatalogTestAccess::WithInvalidGraphqlOperation(std::move(connector), "viewer_repository_metrics",
		                                                            "github_viewer_repository_metrics",
		                                                            std::move(graphql)),
		    "viewer_repository_metrics", 8ULL * 1024ULL * 1024ULL, 64ULL * 1024ULL * 1024ULL,
		    std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max(), 512);
	case RepositoryGithubGraphqlCounterexample::COUNT:
		throw std::invalid_argument("unknown repository GitHub GraphQL counterexample");
	}
	return ConnectorCatalogTestAccess::WithInvalidGraphqlOperation(
	    std::move(connector), "viewer_repository_metrics", "github_viewer_repository_metrics", std::move(graphql));
}

duckdb_api::CompiledConnector CompileRepositoryGithubGraphqlBoundary(const std::string &absolute_repository_root,
                                                                     RepositoryGithubGraphqlBoundary boundary) {
	const auto generation = CompileRepositoryGithubGenerationFixture(absolute_repository_root);
	auto connector = generation.Connector();
	const auto *relation = connector.FindRelation("viewer_repository_metrics");
	if (relation == nullptr || relation->Operations().size() != 1) {
		throw std::runtime_error("repository GitHub GraphQL fixture lost its exact relation");
	}
	auto graphql = relation->Operations()[0].Graphql();
	switch (boundary) {
	case RepositoryGithubGraphqlBoundary::ENDPOINT_PATH_BYTES:
		graphql.endpoint_path = "/" + std::string(2047, 'a');
		break;
	case RepositoryGithubGraphqlBoundary::FIXED_HEADER_COUNT:
		while (graphql.headers.size() < 32) {
			graphql.headers.push_back({"X-Count-" + std::to_string(graphql.headers.size()), "v"});
		}
		break;
	case RepositoryGithubGraphqlBoundary::FIXED_HEADER_BYTES: {
		FillHeadersToCombinedBytes(graphql.headers, 16ULL * 1024ULL);
		break;
	}
	case RepositoryGithubGraphqlBoundary::RESPONSE_SCAN_PRODUCT:
		return ConnectorCatalogTestAccess::WithInvalidRelationResources(
		    ConnectorCatalogTestAccess::WithInvalidGraphqlOperation(std::move(connector), "viewer_repository_metrics",
		                                                            "github_viewer_repository_metrics",
		                                                            std::move(graphql)),
		    "viewer_repository_metrics", 8ULL * 1024ULL * 1024ULL, 256ULL * 1024ULL * 1024ULL, 100, 3200, 512);
	case RepositoryGithubGraphqlBoundary::COUNT:
		throw std::invalid_argument("unknown repository GitHub GraphQL boundary");
	}
	return ConnectorCatalogTestAccess::WithInvalidGraphqlOperation(
	    std::move(connector), "viewer_repository_metrics", "github_viewer_repository_metrics", std::move(graphql));
}

} // namespace duckdb_api_test
