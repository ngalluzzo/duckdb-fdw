#include "query/support/duckdb_secret_test_support.hpp"

#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb_api/authorization.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

namespace duckdb_api_test {
namespace duckdb_secret {
namespace {

duckdb::unique_ptr<duckdb::BaseSecret> ExistingConfigProvider(duckdb::ClientContext &,
                                                              duckdb::CreateSecretInput &input) {
	return duckdb::make_uniq<duckdb::KeyValueSecret>(input.scope, input.type, input.provider, input.name);
}

void RequireInventory(duckdb::Connection &connection, const std::string &name, const std::string &provider,
                      const std::string &storage, const std::string &redacted_key, const std::string &forbidden) {
	auto inventory = connection.Query("SELECT type, provider, storage, secret_string FROM duckdb_secrets() "
	                                  "WHERE lower(name) = lower('" +
	                                  name + "')");
	Require(!inventory->HasError() && inventory->RowCount() == 1,
	        "registered credential was absent from DuckDB inventory");
	Require(inventory->GetValue(0, 0).ToString() == "duckdb_api" && inventory->GetValue(1, 0).ToString() == provider &&
	            inventory->GetValue(2, 0).ToString() == storage,
	        "registered credential escaped its type, provider, or storage boundary");
	const auto rendered = inventory->GetValue(3, 0).ToString();
	Require(rendered.find(redacted_key + "=redacted") != std::string::npos &&
	            rendered.find(forbidden) == std::string::npos,
	        "DuckDB credential inventory exposed provider payload");
}

} // namespace

void TestRegisteredSurfaceCoversAllProvidersAndStorageModes() {
	ScopedCredentialRoot root;
	duckdb::DuckDB database(nullptr);
	RegisterProduct(database);
	duckdb::Connection connection(database);
	root.Configure(connection);
	const auto token_a = TokenCanary('A');
	const auto token_b = TokenCanary('B');
	const std::string variable = "DUCKDB_API_SECRET_TEST_REGISTERED";
	Require(::setenv(variable.c_str(), token_b.c_str(), 1) == 0, "could not configure credential environment fixture");

	Require(!connection
	             .Query("CREATE TEMPORARY SECRET temp_config "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                    token_a + "')")
	             ->HasError(),
	        "temporary config credential was rejected");
	Require(!connection
	             .Query("CREATE TEMPORARY SECRET temp_environment "
	                    "(TYPE duckdb_api, PROVIDER environment, VARIABLE '" +
	                    variable + "')")
	             ->HasError(),
	        "temporary environment credential was rejected");
	Require(!connection
	             .Query("CREATE PERSISTENT SECRET persistent_config IN duckdb_api "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                    token_b + "')")
	             ->HasError(),
	        "persistent config credential was rejected");
	Require(!connection
	             .Query("CREATE PERSISTENT SECRET persistent_environment IN duckdb_api "
	                    "(TYPE duckdb_api, PROVIDER environment, VARIABLE '" +
	                    variable + "')")
	             ->HasError(),
	        "persistent environment credential was rejected");

	RequireInventory(connection, "temp_config", "config", "memory", "token", token_a);
	RequireInventory(connection, "temp_environment", "environment", "memory", "variable", variable);
	RequireInventory(connection, "persistent_config", "config", "duckdb_api", "token", token_b);
	RequireInventory(connection, "persistent_environment", "environment", "duckdb_api", "variable", variable);
	Require(Resolve(connection, "TeMp_CoNfIg") != nullptr, "temporary config credential did not resolve");
	Require(Resolve(connection, "temp_environment") != nullptr, "temporary environment credential did not resolve");
	Require(Resolve(connection, "persistent_config") != nullptr, "persistent config credential did not resolve");
	Require(Resolve(connection, "persistent_environment") != nullptr,
	        "persistent environment credential did not resolve");
	Require(::unsetenv(variable.c_str()) == 0, "could not clear credential environment fixture");
}

void TestCreationRejectsImplicitPersistenceAndMalformedOptions() {
	ScopedCredentialRoot root;
	duckdb::DuckDB database(nullptr);
	RegisterSecrets(database);
	duckdb::Connection connection(database);
	root.Configure(connection);
	const auto token_a = TokenCanary('A');

	RequireQueryFailure(connection,
	                    "CREATE SECRET implicit_default (TYPE duckdb_api, PROVIDER config, TOKEN '" + token_a + "')",
	                    "explicit TEMPORARY memory storage or PERSISTENT IN duckdb_api", token_a);
	RequireQueryFailure(connection,
	                    "CREATE PERSISTENT SECRET persistent_default "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                        token_a + "')",
	                    "explicit TEMPORARY memory storage or PERSISTENT IN duckdb_api", token_a);
	RequireQueryFailure(connection,
	                    "CREATE TEMPORARY SECRET wrong_storage IN local_file "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                        token_a + "')",
	                    "temporary duckdb_api credentials require memory storage", token_a);
	RequireQueryFailure(connection,
	                    "CREATE PERSISTENT SECRET wrong_persistent IN local_file "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                        token_a + "')",
	                    "explicit TEMPORARY memory storage or PERSISTENT IN duckdb_api", token_a);
	RequireQueryFailure(connection, "CREATE TEMPORARY SECRET missing_token (TYPE duckdb_api, PROVIDER config)",
	                    "TOKEN must be a non-empty visible-ASCII VARCHAR");
	RequireQueryFailure(connection, "CREATE TEMPORARY SECRET null_token (TYPE duckdb_api, PROVIDER config, TOKEN NULL)",
	                    "TOKEN must be a non-empty visible-ASCII VARCHAR");
	RequireQueryFailure(connection,
	                    "CREATE TEMPORARY SECRET numeric_token (TYPE duckdb_api, PROVIDER config, TOKEN 123)",
	                    "TOKEN must be a non-empty visible-ASCII VARCHAR");
	RequireQueryFailure(connection, "CREATE TEMPORARY SECRET empty_token (TYPE duckdb_api, PROVIDER config, TOKEN '')",
	                    "TOKEN must be a non-empty visible-ASCII VARCHAR");
	RequireQueryFailure(connection,
	                    "CREATE TEMPORARY SECRET spaced_token "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN 'not safe')",
	                    "TOKEN must be a non-empty visible-ASCII VARCHAR", "not safe");
	RequireQueryFailure(connection,
	                    "CREATE TEMPORARY SECRET invalid_variable "
	                    "(TYPE duckdb_api, PROVIDER environment, VARIABLE '1-NOT-PORTABLE')",
	                    "VARIABLE must be a portable environment identifier", "1-NOT-PORTABLE");
	RequireQueryFailure(connection,
	                    "CREATE TEMPORARY SECRET boolean_variable "
	                    "(TYPE duckdb_api, PROVIDER environment, VARIABLE true)",
	                    "VARIABLE must be a portable environment identifier");
	RequireQueryFailure(connection,
	                    "CREATE TEMPORARY SECRET unknown_option (TYPE duckdb_api, PROVIDER config, OTHER 'x')",
	                    "Unknown parameter 'other'");

	const auto token_limit = duckdb_api::ScanAuthorization::CredentialByteLimit();
	const auto exact_token = std::string(static_cast<std::size_t>(token_limit), 'e');
	Require(!connection
	             .Query("CREATE TEMPORARY SECRET exact_limit "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                    exact_token + "')")
	             ->HasError(),
	        "exact-limit temporary config credential was rejected");
	const auto oversized_token = std::string(static_cast<std::size_t>(token_limit + 1), 'o');
	RequireQueryFailure(connection,
	                    "CREATE TEMPORARY SECRET over_limit (TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                        oversized_token + "')",
	                    "[duckdb_api][resource] field=header_bytes", oversized_token);

	Require(!connection
	             .Query("CREATE TEMPORARY SECRET explicit_memory IN memory "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                    token_a + "')")
	             ->HasError(),
	        "explicit temporary memory credential was rejected");
	Require(!connection
	             .Query("CREATE PERSISTENT SECRET explicit_persistent IN duckdb_api "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                    token_a + "')")
	             ->HasError(),
	        "explicit persistent credential was rejected");

	Require(!connection.Query("BEGIN")->HasError(), "persistent transaction fixture did not begin");
	RequireQueryFailure(connection,
	                    "CREATE PERSISTENT SECRET transaction_forbidden IN duckdb_api "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                        token_a + "')",
	                    "persistent credential mutation requires autocommit", token_a);
	Require(!connection.Query("ROLLBACK")->HasError(), "persistent transaction fixture did not roll back");
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
	        "failed credential-provider registration exposed duckdb_api_scan");

	const auto orphan = duckdb::SecretManager::Get(*database.instance).LookupType("duckdb_api");
	Require(orphan.name == "duckdb_api", "pinned DuckDB no longer exhibits the documented orphan-type limitation");
}

} // namespace duckdb_secret
} // namespace duckdb_api_test
