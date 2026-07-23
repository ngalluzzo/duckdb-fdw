#include "connector/support/package_compiler_test_fixtures.hpp"

#include "connector/support/catalog_test_access.hpp"

#include "duckdb_api/content_digest.hpp"
#include "duckdb_api/local_package_compiler.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
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

duckdb_api::CompiledQueryRegistrationView
CompileRepositoryRickAndMortyRegistrationFixture(const std::string &absolute_repository_root) {
	return CompileRepositoryRickAndMortyLocalPackageFixture(absolute_repository_root).Generation().QueryRegistration();
}

duckdb_api::CompiledLocalPackage
CompileRepositoryRickAndMortyLocalPackageFixture(const std::string &absolute_repository_root) {
	NeverCancel cancellation;
	const auto result = duckdb_api::connector::CompileLocalPackageRoot(
	    absolute_repository_root + "/connectors/rickandmorty", cancellation);
	if (!result.Succeeded() || result.Package() == nullptr) {
		throw std::runtime_error("repository Rick and Morty connector package fixture did not compile");
	}
	return duckdb_api::CompiledLocalPackage(*result.Package());
}

duckdb_api::CompiledPackageGeneration CompileRetryV2GenerationFixture(const std::string &absolute_repository_root) {
	NeverCancel cancellation;
	const auto result = duckdb_api::connector::CompileLocalPackageRoot(
	    absolute_repository_root + "/test/fixtures/package_retry_v2", cancellation);
	if (!result.Succeeded() || result.Package() == nullptr) {
		throw std::runtime_error("repository retry v2 connector package fixture did not compile");
	}
	return result.Package()->Generation();
}

duckdb_api::CompiledPackageGeneration CompileRateLimitV3GenerationFixture(const std::string &absolute_repository_root) {
	NeverCancel cancellation;
	const auto result = duckdb_api::connector::CompileLocalPackageRoot(
	    absolute_repository_root + "/test/fixtures/package_rate_limit_v3", cancellation);
	if (!result.Succeeded() || result.Package() == nullptr) {
		throw std::runtime_error("repository rate-limit v3 connector package fixture did not compile");
	}
	return result.Package()->Generation();
}

// The canonical package-independence relation. Every field is identical across
// both migration envelopes except the operation origin host, which must track
// each envelope's network policy. It deliberately exercises the v1 mechanisms
// both real packages share (static schema, typed columns with JSONPath
// extractors, a nullable relation input bound into a REST query field with
// omission semantics, anonymous auth, terminal-collection response, disabled
// pagination, full resource ceilings) so equivalence is proven across the
// contract surface, not one accidental field.
const char *const kMigrationProbeRelationTemplate = "api_version: duckdb_api/v1\n"
                                                    "kind: relation\n"
                                                    "id: migration_probe\n"
                                                    "schema: static\n"
                                                    "\n"
                                                    "columns:\n"
                                                    "  - id: id\n"
                                                    "    type: BIGINT\n"
                                                    "    nullable: false\n"
                                                    "    extract: $.id\n"
                                                    "  - id: name\n"
                                                    "    type: VARCHAR\n"
                                                    "    nullable: false\n"
                                                    "    extract: $.name\n"
                                                    "\n"
                                                    "inputs:\n"
                                                    "  - id: status\n"
                                                    "    type: VARCHAR\n"
                                                    "    nullable: true\n"
                                                    "\n"
                                                    "auth:\n"
                                                    "  mode: anonymous\n"
                                                    "\n"
                                                    "resources:\n"
                                                    "  max_response_bytes_per_page: 65536\n"
                                                    "  max_response_bytes_per_scan: 65536\n"
                                                    "  max_records_per_page: 20\n"
                                                    "  max_records_per_scan: 20\n"
                                                    "  max_extracted_string_bytes: 256\n"
                                                    "\n"
                                                    "operations:\n"
                                                    "  - id: fetch_probe\n"
                                                    "    fallback: true\n"
                                                    "    cardinality: many\n"
                                                    "    replay_safety: safe\n"
                                                    "    request:\n"
                                                    "      protocol: rest\n"
                                                    "      method: GET\n"
                                                    "      origin:\n"
                                                    "        scheme: https\n"
                                                    "        host: %s\n"
                                                    "        port: 443\n"
                                                    "      path: /api/probe\n"
                                                    "      query:\n"
                                                    "        - name: status\n"
                                                    "          input: status\n"
                                                    "          encoding: form_urlencoded\n"
                                                    "          omit_when_unbound: true\n"
                                                    "          omit_when_null: true\n"
                                                    "      headers: []\n"
                                                    "    response:\n"
                                                    "      source: terminal_collection\n"
                                                    "      records: $.results[*]\n"
                                                    "    pagination:\n"
                                                    "      strategy: disabled\n";

std::string RenderMigrationProbeRelation(const std::string &host) {
	char buffer[4096];
	const int written = ::snprintf(buffer, sizeof(buffer), kMigrationProbeRelationTemplate, host.c_str());
	if (written < 0 || static_cast<std::size_t>(written) >= sizeof(buffer)) {
		throw std::runtime_error("migration probe relation template did not render");
	}
	return std::string(buffer, static_cast<std::size_t>(written));
}

// Derives a migration envelope from one real package's connector.yaml by
// re-identifying the connector and replacing its relation list with the single
// canonical probe. Everything else (network policy, credentials, response
// ceiling) is the real package's, so the oracle proves equivalence across the
// actual package profiles rather than a hand-authored approximation.
std::string DeriveMigrationManifest(const std::string &real_manifest, const std::string &real_id,
                                    const std::string &migration_id) {
	const std::string id_anchor = "\nid: " + real_id + "\n";
	const auto id_offset = real_manifest.find(id_anchor);
	if (id_offset == std::string::npos) {
		throw std::runtime_error("migration envelope source connector id anchor is missing");
	}
	const std::string relations_anchor = "\nrelations:\n";
	const auto relations_offset = real_manifest.find(relations_anchor);
	if (relations_offset == std::string::npos) {
		throw std::runtime_error("migration envelope source relations block is missing");
	}
	std::string result = real_manifest;
	result.replace(id_offset, id_anchor.size(), "\nid: " + migration_id + "\n");
	// Re-locate the relations block after the id replacement shifted the text.
	const auto new_relations_offset = result.find(relations_anchor);
	result.replace(new_relations_offset, result.size() - new_relations_offset, "\nrelations:\n  - migration_probe\n");
	return result;
}

std::string ApplyReplacements(std::string text, const std::vector<MigrationReplacement> &replacements) {
	for (const auto &replacement : replacements) {
		const auto offset = text.find(replacement.from);
		if (offset == std::string::npos) {
			throw std::runtime_error("migration mutation anchor is absent: " + replacement.from);
		}
		text.replace(offset, replacement.from.size(), replacement.to);
	}
	return text;
}

struct MigrationProfileSource {
	const char *package;
	const char *real_id;
	const char *migration_id;
	const char *host;
};

MigrationProfileSource ResolveMigrationProfile(MigrationProfile profile) {
	switch (profile) {
	case MigrationProfile::GITHUB:
		return {"github", "github", "github_migration", "api.github.com"};
	case MigrationProfile::RICK_AND_MORTY:
		return {"rickandmorty", "rickandmorty", "rickandmorty_migration", "rickandmortyapi.com"};
	}
	throw std::invalid_argument("unknown migration profile");
}

duckdb_api::connector::PackageCompileResult
CompileMigrationEnvelopeWithMutation(const std::string &absolute_repository_root, MigrationProfile profile,
                                     const std::vector<MigrationReplacement> &manifest_replacements,
                                     const std::vector<MigrationReplacement> &relation_replacements) {
	const auto source = ResolveMigrationProfile(profile);
	char pattern[] = "/private/tmp/duckdb-api-migration-fixture-XXXXXX";
	const auto *created = ::mkdtemp(pattern);
	if (!created) {
		throw std::runtime_error("could not create migration local-package fixture root");
	}
	const std::string root = created;
	try {
		if (::mkdir((root + "/relations").c_str(), 0700) != 0) {
			throw std::runtime_error("could not create migration local-package fixture relations");
		}
		const std::string evidence = absolute_repository_root + "/connectors/" + source.package + "/";
		WriteFile(root + "/connector.yaml",
		          ApplyReplacements(DeriveMigrationManifest(ReadFile(evidence + "connector.yaml"), source.real_id,
		                                                    source.migration_id),
		                            manifest_replacements));
		WriteFile(root + "/relations/migration_probe.yaml",
		          ApplyReplacements(RenderMigrationProbeRelation(source.host), relation_replacements));
		NeverCancel cancellation;
		auto result = duckdb_api::connector::CompileLocalPackageRoot(root, cancellation);
		// A successful compilation retains the root through the returned
		// custody; a failed compilation has no custody, so the provider owns
		// the private root for process lifetime in both cases.
		RetainedFixtureRoots().Retain(root);
		return result;
	} catch (...) {
		(void)::nftw(root.c_str(), RemoveEntry, 32, FTW_DEPTH | FTW_PHYS);
		throw;
	}
}

CrossPackageMigrationFixture BuildRepositoryCrossPackageMigrationFixture(const std::string &absolute_repository_root) {
	const auto github =
	    CompileMigrationEnvelopeWithMutation(absolute_repository_root, MigrationProfile::GITHUB, {}, {});
	if (!github.Succeeded() || github.Package() == nullptr) {
		throw std::runtime_error("github-profile migration envelope did not compile");
	}
	const auto rickandmorty =
	    CompileMigrationEnvelopeWithMutation(absolute_repository_root, MigrationProfile::RICK_AND_MORTY, {}, {});
	if (!rickandmorty.Succeeded() || rickandmorty.Package() == nullptr) {
		throw std::runtime_error("rickandmorty-profile migration envelope did not compile");
	}
	return CrossPackageMigrationFixture {duckdb_api::CompiledLocalPackage(*github.Package()),
	                                     duckdb_api::CompiledLocalPackage(*rickandmorty.Package())};
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

duckdb_api::CompiledGraphqlQueryRecipe
CompileRepositoryGithubGraphqlRecipeFixture(const std::string &absolute_repository_root,
                                            RepositoryGithubGraphqlRecipeFixture fixture) {
	const auto generation = CompileRepositoryGithubGenerationFixture(absolute_repository_root);
	const auto *relation = generation.Connector().FindRelation("viewer_repository_metrics");
	if (relation == nullptr || relation->Operations().size() != 1) {
		throw std::runtime_error("repository GitHub GraphQL recipe fixture lost its exact relation");
	}
	const auto &recipe = relation->Operations()[0].Graphql().QueryRecipe();
	switch (fixture) {
	case RepositoryGithubGraphqlRecipeFixture::EXACT_LITERAL_DEPTH:
		return ConnectorCatalogTestAccess::WithFirstGraphqlFixedArgument(
		    recipe, ConnectorCatalogTestAccess::NestedGraphqlList(31));
	case RepositoryGithubGraphqlRecipeFixture::EXCESSIVE_LITERAL_DEPTH:
		return ConnectorCatalogTestAccess::WithFirstGraphqlFixedArgument(
		    recipe, ConnectorCatalogTestAccess::NestedGraphqlList(32));
	case RepositoryGithubGraphqlRecipeFixture::EXACT_LIST_ITEMS:
		return ConnectorCatalogTestAccess::WithFirstGraphqlFixedArgument(
		    recipe, ConnectorCatalogTestAccess::FlatGraphqlNullList(4096));
	case RepositoryGithubGraphqlRecipeFixture::EXCESSIVE_LIST_ITEMS:
		return ConnectorCatalogTestAccess::WithFirstGraphqlFixedArgument(
		    recipe, ConnectorCatalogTestAccess::FlatGraphqlNullList(4097));
	case RepositoryGithubGraphqlRecipeFixture::MINIMUM_SIGNED_INTEGER:
		return ConnectorCatalogTestAccess::WithFirstGraphqlFixedArgument(
		    recipe, ConnectorCatalogTestAccess::RawGraphqlInteger("-9223372036854775808"));
	case RepositoryGithubGraphqlRecipeFixture::MAXIMUM_SIGNED_INTEGER:
		return ConnectorCatalogTestAccess::WithFirstGraphqlFixedArgument(
		    recipe, ConnectorCatalogTestAccess::RawGraphqlInteger("9223372036854775807"));
	case RepositoryGithubGraphqlRecipeFixture::BELOW_MINIMUM_SIGNED_INTEGER:
		return ConnectorCatalogTestAccess::WithFirstGraphqlFixedArgument(
		    recipe, ConnectorCatalogTestAccess::RawGraphqlInteger("-9223372036854775809"));
	case RepositoryGithubGraphqlRecipeFixture::ABOVE_MAXIMUM_SIGNED_INTEGER:
		return ConnectorCatalogTestAccess::WithFirstGraphqlFixedArgument(
		    recipe, ConnectorCatalogTestAccess::RawGraphqlInteger("9223372036854775808"));
	case RepositoryGithubGraphqlRecipeFixture::COUNT:
		break;
	}
	throw std::invalid_argument("unknown repository GitHub GraphQL recipe fixture");
}

duckdb_api::CompiledGraphqlLiteral BuildGraphqlLiteralNodeBudgetFixture(GraphqlLiteralNodeBudgetFixture fixture) {
	switch (fixture) {
	case GraphqlLiteralNodeBudgetFixture::EXACT:
		return ConnectorCatalogTestAccess::GraphqlLiteralNodeTree(100000);
	case GraphqlLiteralNodeBudgetFixture::EXCESSIVE:
		return ConnectorCatalogTestAccess::GraphqlLiteralNodeTree(100001);
	case GraphqlLiteralNodeBudgetFixture::COUNT:
		break;
	}
	throw std::invalid_argument("unknown GraphQL literal node budget fixture");
}

} // namespace duckdb_api_test
