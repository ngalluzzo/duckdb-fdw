#include "support/duckdb_secret_test_support.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_set.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/main/secret/secret_storage.hpp"
#include "duckdb_api/duckdb_secret.hpp"
#include "support/require.hpp"

#include <string>
#include <utility>
#include <vector>

namespace duckdb_api_test {
namespace duckdb_secret {
namespace {

// These no-filesystem storages isolate persistence, storage, ambiguity, and
// host-failure behavior without writing credential artifacts.
class PersistentMemoryTestStorage final : public duckdb::CatalogSetSecretStorage {
public:
	PersistentMemoryTestStorage(duckdb::DatabaseInstance &database, const std::string &name)
	    : CatalogSetSecretStorage(database, name, LOCAL_FILE_STORAGE_OFFSET + 10) {
		secrets = duckdb::make_uniq<duckdb::CatalogSet>(duckdb::Catalog::GetSystemCatalog(database));
		persistent = true;
	}
};

class TemporaryAlternateTestStorage final : public duckdb::CatalogSetSecretStorage {
public:
	TemporaryAlternateTestStorage(duckdb::DatabaseInstance &database, const std::string &name)
	    : CatalogSetSecretStorage(database, name, LOCAL_FILE_STORAGE_OFFSET + 20) {
		secrets = duckdb::make_uniq<duckdb::CatalogSet>(duckdb::Catalog::GetSystemCatalog(database));
		persistent = false;
	}
};

enum class StorageFailure { INTERRUPT, HOST, INVALID_CONFIGURATION };

class FaultingTestStorage final : public duckdb::CatalogSetSecretStorage {
public:
	FaultingTestStorage(duckdb::DatabaseInstance &database, const std::string &name, StorageFailure failure_p,
	                    std::string diagnostic_p = "synthetic secret storage failure")
	    : CatalogSetSecretStorage(database, name, LOCAL_FILE_STORAGE_OFFSET + 30), failure(failure_p),
	      diagnostic(std::move(diagnostic_p)) {
		persistent = false;
	}

	duckdb::unique_ptr<duckdb::SecretEntry> GetSecretByName(const std::string &,
	                                                        duckdb::optional_ptr<duckdb::CatalogTransaction>) override {
		if (failure == StorageFailure::INTERRUPT) {
			throw duckdb::InterruptException();
		}
		if (failure == StorageFailure::INVALID_CONFIGURATION) {
			throw duckdb::InvalidConfigurationException(diagnostic);
		}
		throw duckdb::IOException(diagnostic);
	}

private:
	const StorageFailure failure;
	const std::string diagnostic;
};

duckdb::unique_ptr<duckdb::KeyValueSecret> StoredSecret(const std::string &type, const std::string &provider,
                                                        const std::string &name, const duckdb::Value *token) {
	auto secret = duckdb::make_uniq<duckdb::KeyValueSecret>(std::vector<std::string>(), type, provider, name);
	if (token) {
		secret->secret_map["token"] = *token;
		secret->redact_keys.insert("token");
	}
	return secret;
}

void RegisterStoredSecret(duckdb::Connection &connection, duckdb::unique_ptr<const duckdb::BaseSecret> secret,
                          duckdb::SecretPersistType persist_type = duckdb::SecretPersistType::TEMPORARY,
                          const std::string &storage = duckdb::SecretManager::TEMPORARY_STORAGE_NAME) {
	connection.context->RunFunctionInTransaction([&]() {
		auto transaction = duckdb::CatalogTransaction::GetSystemCatalogTransaction(*connection.context);
		duckdb::SecretManager::Get(*connection.context)
		    .RegisterSecret(transaction, std::move(secret), duckdb::OnCreateConflict::ERROR_ON_CONFLICT, persist_type,
		                    storage);
	});
}

} // namespace

// These cases own exact-name resolution, current-transaction visibility,
// lifecycle semantics, storage validation, and host-failure containment.
void TestResolutionValidatesTypeProviderShapeAndToken() {
	duckdb::DuckDB database(nullptr);
	RegisterSecrets(database);
	duckdb::Connection connection(database);
	const auto token_a = TokenCanary('A');
	RequireCanaryAbsentFromInventory(connection, token_a);

	RequireAuthenticationFailure([&]() { (void)Resolve(connection, ""); });
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "missing_secret"); });

	duckdb::Value token(token_a);
	RegisterStoredSecret(connection, StoredSecret("http", "config", "wrong_type", &token));
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "wrong_type"); }, token_a);
	RegisterStoredSecret(connection, StoredSecret("duckdb_api", "rogue", "wrong_provider", &token));
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "wrong_provider"); }, token_a);
	RegisterStoredSecret(connection, duckdb::make_uniq<duckdb::BaseSecret>(std::vector<std::string>(), "duckdb_api",
	                                                                       "config", "wrong_shape"));
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "wrong_shape"); });
	RegisterStoredSecret(connection, StoredSecret("duckdb_api", "config", "missing_stored_token", nullptr));
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "missing_stored_token"); });

	duckdb::Value null_token(duckdb::LogicalType::VARCHAR);
	RegisterStoredSecret(connection, StoredSecret("duckdb_api", "config", "null_stored_token", &null_token));
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "null_stored_token"); });
	duckdb::Value empty_token("");
	RegisterStoredSecret(connection, StoredSecret("duckdb_api", "config", "empty_stored_token", &empty_token));
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "empty_stored_token"); });
	duckdb::Value integer_token = duckdb::Value::BIGINT(42);
	RegisterStoredSecret(connection, StoredSecret("duckdb_api", "config", "integer_stored_token", &integer_token));
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "integer_stored_token"); });
}

void TestResolutionObservesReplacementDropStorageAndAmbiguity() {
	duckdb::DuckDB database(nullptr);
	RegisterSecrets(database);
	auto &manager = duckdb::SecretManager::Get(*database.instance);
	manager.LoadSecretStorage(duckdb::make_uniq<PersistentMemoryTestStorage>(*database.instance, "query_persistent"));
	duckdb::Connection connection(database);
	const auto token_a = TokenCanary('A');
	const auto token_b = TokenCanary('B');
	auto invalid_token = TokenCanary('I');
	invalid_token.insert(invalid_token.size() / 2, 1, ' ');
	RequireCanaryAbsentFromInventory(connection, token_a);
	RequireCanaryAbsentFromInventory(connection, token_b);
	RequireCanaryAbsentFromInventory(connection, invalid_token);

	Require(!connection
	             .Query("CREATE TEMPORARY SECRET rotating (TYPE duckdb_api, PROVIDER config, TOKEN '" + token_a + "')")
	             ->HasError(),
	        "initial rotating secret was rejected");
	Require(Resolve(connection, "rotating") != nullptr, "initial secret did not resolve to a capability");
	Require(!connection
	             .Query("CREATE OR REPLACE TEMPORARY SECRET rotating "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                    invalid_token + "')")
	             ->HasError(),
	        "replacement secret was rejected before resolution");
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "rotating"); }, invalid_token);
	Require(!connection
	             .Query("CREATE OR REPLACE TEMPORARY SECRET rotating "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                    token_b + "')")
	             ->HasError(),
	        "second replacement secret was rejected");
	Require(Resolve(connection, "rotating") != nullptr, "safe replacement did not resolve to a capability");
	Require(!connection.Query("DROP TEMPORARY SECRET rotating")->HasError(), "temporary secret drop failed");
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "rotating"); });

	duckdb::Value token(token_a);
	RegisterStoredSecret(connection, StoredSecret("duckdb_api", "config", "persistent_only", &token),
	                     duckdb::SecretPersistType::PERSISTENT, "query_persistent");
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "persistent_only"); }, token_a);

	const std::string escaped_ambiguous = "ambiguous\"\\name";
	RegisterStoredSecret(connection, StoredSecret("duckdb_api", "config", escaped_ambiguous, &token));
	RegisterStoredSecret(connection, StoredSecret("duckdb_api", "config", escaped_ambiguous, &token),
	                     duckdb::SecretPersistType::PERSISTENT, "query_persistent");
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, escaped_ambiguous); }, token_a);
}

void TestResolutionUsesCurrentTransaction() {
	duckdb::DuckDB database(nullptr);
	RegisterSecrets(database);
	duckdb::Connection connection(database);
	const auto token_a = TokenCanary('A');
	auto invalid_token = TokenCanary('I');
	invalid_token.insert(invalid_token.size() / 2, 1, ' ');
	RequireCanaryAbsentFromInventory(connection, token_a);
	RequireCanaryAbsentFromInventory(connection, invalid_token);

	Require(!connection.Query("BEGIN")->HasError(), "transaction begin failed");
	Require(!connection
	             .Query("CREATE TEMPORARY SECRET transactional_create "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                    token_a + "')")
	             ->HasError(),
	        "same-transaction create failed");
	Require(Resolve(connection, "transactional_create") != nullptr, "same-transaction create was not visible");
	Require(!connection.Query("ROLLBACK")->HasError(), "create transaction rollback failed");
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "transactional_create"); });

	Require(!connection
	             .Query("CREATE TEMPORARY SECRET transactional_replace "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                    token_a + "')")
	             ->HasError(),
	        "replacement baseline create failed");
	Require(!connection.Query("BEGIN")->HasError(), "replacement transaction begin failed");
	Require(!connection
	             .Query("CREATE OR REPLACE TEMPORARY SECRET transactional_replace "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                    invalid_token + "')")
	             ->HasError(),
	        "same-transaction replacement failed");
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "transactional_replace"); }, invalid_token);
	Require(!connection.Query("ROLLBACK")->HasError(), "replacement transaction rollback failed");
	Require(Resolve(connection, "transactional_replace") != nullptr, "replacement rollback was not visible");

	Require(!connection
	             .Query("CREATE TEMPORARY SECRET transactional_drop "
	                    "(TYPE duckdb_api, PROVIDER config, TOKEN '" +
	                    token_a + "')")
	             ->HasError(),
	        "drop baseline create failed");
	Require(!connection.Query("BEGIN")->HasError(), "drop transaction begin failed");
	Require(!connection.Query("DROP TEMPORARY SECRET transactional_drop")->HasError(), "same-transaction drop failed");
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "transactional_drop"); });
	Require(!connection.Query("ROLLBACK")->HasError(), "drop transaction rollback failed");
	Require(Resolve(connection, "transactional_drop") != nullptr, "drop rollback was not visible");
}

void TestResolutionRequiresTemporaryMemoryIndependently() {
	duckdb::DuckDB database(nullptr);
	RegisterSecrets(database);
	auto &manager = duckdb::SecretManager::Get(*database.instance);
	manager.LoadSecretStorage(
	    duckdb::make_uniq<TemporaryAlternateTestStorage>(*database.instance, "query_temporary_alternate"));
	duckdb::Connection connection(database);
	const auto token_a = TokenCanary('A');
	RequireCanaryAbsentFromInventory(connection, token_a);
	duckdb::Value token(token_a);
	RegisterStoredSecret(connection, StoredSecret("duckdb_api", "config", "temporary_not_memory", &token),
	                     duckdb::SecretPersistType::TEMPORARY, "query_temporary_alternate");
	RequireAuthenticationFailure([&]() { (void)Resolve(connection, "temporary_not_memory"); }, token_a);
}

void TestResolutionRequiresAnActiveTransaction() {
	duckdb::DuckDB database(nullptr);
	RegisterSecrets(database);
	duckdb::Connection connection(database);
	RequireInternalFailure([&]() { (void)duckdb::ResolveDuckdbApiSecret(*connection.context, "outside_transaction"); });
}

void TestResolutionPreservesHostInterruption() {
	duckdb::DuckDB database(nullptr);
	RegisterSecrets(database);
	auto &manager = duckdb::SecretManager::Get(*database.instance);
	manager.LoadSecretStorage(
	    duckdb::make_uniq<FaultingTestStorage>(*database.instance, "query_interrupting", StorageFailure::INTERRUPT));
	duckdb::Connection connection(database);
	RequireCancellation([&]() { (void)Resolve(connection, "interrupt_me"); });
}

void TestResolutionClassifiesOtherHostFailuresAsInternal() {
	for (const auto failure : {StorageFailure::HOST, StorageFailure::INVALID_CONFIGURATION}) {
		duckdb::DuckDB database(nullptr);
		RegisterSecrets(database);
		auto &manager = duckdb::SecretManager::Get(*database.instance);
		const auto token_canary = TokenCanary(failure == StorageFailure::HOST ? 'H' : 'C');
		duckdb::Connection connection(database);
		RequireCanaryAbsentFromInventory(connection, token_canary);
		std::string diagnostic = token_canary;
		if (failure == StorageFailure::INVALID_CONFIGURATION) {
			diagnostic +=
			    " prefix Ambiguity detected for secret name 'host_failure', secret occurs in multiple storage "
			    "backends. suffix";
		}
		manager.LoadSecretStorage(duckdb::make_uniq<FaultingTestStorage>(*database.instance, "query_faulting", failure,
		                                                                 std::move(diagnostic)));
		RequireInternalFailure([&]() { (void)Resolve(connection, "host_failure"); }, token_canary);
	}
}

} // namespace duckdb_secret
} // namespace duckdb_api_test
