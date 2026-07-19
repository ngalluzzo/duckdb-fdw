#include "support/duckdb_secret_test_support.hpp"

#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb_api/connector.hpp"
#include "support/require.hpp"

#include <memory>
#include <utility>

namespace duckdb_api_test {
namespace duckdb_secret {
namespace {

duckdb::unique_ptr<duckdb::BaseSecret> ExistingConfigProvider(duckdb::ClientContext &,
                                                              duckdb::CreateSecretInput &input) {
	return duckdb::make_uniq<duckdb::KeyValueSecret>(input.scope, input.type, input.provider, input.name);
}

} // namespace

// These cases own the author-visible registration and CREATE SECRET policy.
// Resolution internals belong to duckdb_secret_resolution_tests.cpp.
void TestRegisteredSurfaceIsTemporaryConfigAndRedacted() {
	duckdb::DuckDB database(nullptr);
	RegisterProduct(database);
	duckdb::Connection connection(database);
	const auto token_a = TokenCanary('A');
	RequireCanaryAbsentFromInventory(connection, token_a);

	auto created = connection.Query("CREATE TEMPORARY SECRET GitHub_Default (TYPE duckdb_api, PROVIDER config, "
	                                "TOKEN '" +
	                                token_a + "')");
	Require(!created->HasError(), "explicit temporary config secret was rejected");
	auto inventory = connection.Query("SELECT type, provider, storage, secret_string FROM duckdb_secrets() "
	                                  "WHERE lower(name) = 'github_default'");
	Require(!inventory->HasError() && inventory->RowCount() == 1, "registered secret was absent from DuckDB inventory");
	Require(inventory->GetValue(0, 0).ToString() == "duckdb_api" && inventory->GetValue(1, 0).ToString() == "config" &&
	            inventory->GetValue(2, 0).ToString() == "memory",
	        "registered secret escaped its type, provider, or storage boundary");
	const auto redacted = inventory->GetValue(3, 0).ToString();
	Require(redacted.find("token=redacted") != std::string::npos && redacted.find(token_a) == std::string::npos,
	        "DuckDB secret inventory did not redact TOKEN");

	connection.context->RunFunctionInTransaction([&]() {
		auto transaction = duckdb::CatalogTransaction::GetSystemCatalogTransaction(*connection.context);
		auto direct = duckdb::SecretManager::Get(*connection.context).GetSecretByName(transaction, "gItHuB_dEfAuLt");
		Require(direct && direct->secret, "pinned DuckDB exact-name lookup did not find the case-insensitive entry");
	});
	auto authorization = Resolve(connection, "gItHuB_dEfAuLt");
	Require(authorization != nullptr, "case-insensitive lookup did not return an authorization capability");
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "github_other"); }, token_a);
}

void TestCreationRejectsImplicitPersistenceAndMalformedOptions() {
	duckdb::DuckDB database(nullptr);
	RegisterSecrets(database);
	duckdb::Connection connection(database);
	const auto token_a = TokenCanary('A');
	RequireCanaryAbsentFromInventory(connection, token_a);

	RequireQueryFailure(connection,
	                    "CREATE SECRET implicit_default (TYPE duckdb_api, PROVIDER config, TOKEN '" + token_a + "')",
	                    "require explicit CREATE TEMPORARY SECRET", token_a);
	RequireQueryFailure(connection,
	                    "CREATE PERSISTENT SECRET persistent_value (TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                        token_a + "')",
	                    "require explicit CREATE TEMPORARY SECRET", token_a);
	RequireQueryFailure(
	    connection,
	    "CREATE TEMPORARY SECRET wrong_storage IN local_file (TYPE duckdb_api, PROVIDER config, TOKEN '" + token_a +
	        "')",
	    "only temporary memory storage", token_a);
	RequireQueryFailure(connection, "CREATE TEMPORARY SECRET missing_token (TYPE duckdb_api, PROVIDER config)",
	                    "TOKEN must be a non-empty VARCHAR");
	RequireQueryFailure(connection, "CREATE TEMPORARY SECRET null_token (TYPE duckdb_api, PROVIDER config, TOKEN NULL)",
	                    "TOKEN must be a non-empty VARCHAR");
	RequireQueryFailure(connection, "CREATE TEMPORARY SECRET empty_token (TYPE duckdb_api, PROVIDER config, TOKEN '')",
	                    "TOKEN must be a non-empty VARCHAR");
	RequireQueryFailure(connection,
	                    "CREATE TEMPORARY SECRET unknown_option (TYPE duckdb_api, PROVIDER config, OTHER 'x')",
	                    "Unknown parameter 'other'");
	const auto token_limit = duckdb_api::ScanAuthorization::GithubUserBearerTokenByteLimit();
	const auto exact_token = std::string(static_cast<std::size_t>(token_limit), 'e');
	const auto exact = connection.Query("CREATE TEMPORARY SECRET exact_limit "
	                                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                                    exact_token + "')");
	Require(!exact->HasError(), "exact-limit temporary bearer secret was rejected");
	const auto oversized_token = std::string(static_cast<std::size_t>(token_limit + 1), 'o');
	RequireQueryFailure(connection,
	                    "CREATE TEMPORARY SECRET over_limit (TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                        oversized_token + "')",
	                    "[duckdb_api][resource] field=header_bytes", oversized_token);

	auto explicit_memory = connection.Query("CREATE TEMPORARY SECRET explicit_memory IN memory "
	                                        "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                                        token_a + "')");
	Require(!explicit_memory->HasError(), "explicit temporary memory storage was rejected");
	Require(Resolve(connection, "explicit_memory") != nullptr,
	        "explicit memory secret did not resolve to a capability");
}

void TestFailedProviderRegistrationNeverPublishesScan() {
	duckdb::DuckDB database(nullptr);
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_secret_test");
	duckdb::CreateSecretFunction existing;
	existing.secret_type = "duckdb_api";
	existing.provider = "config";
	existing.function = ExistingConfigProvider;
	loader.RegisterFunction(std::move(existing));

	bool failed = false;
	try {
		RegisterProduct(loader);
	} catch (const duckdb::Exception &) {
		failed = true;
	}
	Require(failed, "pre-existing provider did not fail product registration");
	Require(!loader.TryGetTableFunction("duckdb_api_scan"),
	        "failed secret-provider registration exposed duckdb_api_scan");

	// DuckDB has no unregister/transaction across these registries. The type
	// registered before the provider failure is intentionally observable as an
	// orphan, documenting why a failed load must stop instead of retrying.
	const auto orphan = duckdb::SecretManager::Get(*database.instance).LookupType("duckdb_api");
	Require(orphan.name == "duckdb_api", "pinned DuckDB no longer exhibits the documented orphan-type limitation");
}

} // namespace duckdb_secret
} // namespace duckdb_api_test
