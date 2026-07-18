#include "support/duckdb_adapter_auth_test_support.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_set.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/secret/secret_storage.hpp"
#include "duckdb_api_extension.hpp"
#include "support/connector_catalog_test_fixtures.hpp"
#include "support/require.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace duckdb_api_test {
namespace {

class PersistentTestSecretStorage final : public duckdb::CatalogSetSecretStorage {
public:
	PersistentTestSecretStorage(duckdb::DatabaseInstance &database, const std::string &name)
	    : CatalogSetSecretStorage(database, name, LOCAL_FILE_STORAGE_OFFSET + 50) {
		secrets = duckdb::make_uniq<duckdb::CatalogSet>(duckdb::Catalog::GetSystemCatalog(database));
		persistent = true;
	}
};

std::shared_ptr<QueryLifecycleProbe> RegisterAdapter(duckdb::DuckDB &database, duckdb_api::CompiledConnector connector,
                                                     QueryRuntimeScenario scenario) {
	auto probe = std::shared_ptr<QueryLifecycleProbe>(new QueryLifecycleProbe());
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_auth_adapter_test");
	duckdb::RegisterDuckdbApi(loader, std::move(connector), BuildQueryScenarioExecutor(scenario, probe));
	return probe;
}

} // namespace

std::string RuntimeAdapterTokenCanary(char marker) {
	std::string result(11, 'q');
	result.push_back('.');
	result.append(13, marker);
	result.append("_adapter");
	return result;
}

std::string QueryError(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result->HasError(), "query unexpectedly succeeded: " + sql);
	return result->GetError();
}

std::shared_ptr<QueryLifecycleProbe> RegisterNativeAdapter(duckdb::DuckDB &database, QueryRuntimeScenario scenario) {
	return RegisterAdapter(database, duckdb_api::BuildNativeGithubConnector(), scenario);
}

std::shared_ptr<QueryLifecycleProbe> RegisterFixtureAdapter(duckdb::DuckDB &database, QueryRuntimeScenario scenario) {
	return RegisterAdapter(database, BuildDistinctSchemaConnectorCatalogFixture(), scenario);
}

void CreateTemporarySecret(duckdb::Connection &connection, const std::string &name, const std::string &token) {
	auto result = connection.Query("CREATE TEMPORARY SECRET \"" + name +
	                               "\" (TYPE duckdb_api, PROVIDER config, TOKEN '" + token + "')");
	if (result->HasError()) {
		throw std::runtime_error("temporary adapter secret creation failed: " + result->GetError());
	}
}

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
                          duckdb::SecretPersistType persist_type, const std::string &storage) {
	connection.context->RunFunctionInTransaction([&]() {
		auto transaction = duckdb::CatalogTransaction::GetSystemCatalogTransaction(*connection.context);
		duckdb::SecretManager::Get(*connection.context)
		    .RegisterSecret(transaction, std::move(secret), duckdb::OnCreateConflict::ERROR_ON_CONFLICT, persist_type,
		                    storage);
	});
}

void LoadPersistentTestSecretStorage(duckdb::DuckDB &database, const std::string &name) {
	duckdb::SecretManager::Get(*database.instance)
	    .LoadSecretStorage(duckdb::make_uniq<PersistentTestSecretStorage>(*database.instance, name));
}

} // namespace duckdb_api_test
