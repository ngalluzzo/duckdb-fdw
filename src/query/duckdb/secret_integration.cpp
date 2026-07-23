#include "duckdb_api/duckdb_secret.hpp"

#include "credential_provider_adapter_internal.hpp"
#include "credential_secret_internal.hpp"
#include "credential_storage_internal.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/secret/secret_manager.hpp"

#include <utility>

namespace duckdb {
namespace {

unique_ptr<BaseSecret> RejectGenericDeserialization(Deserializer &, BaseSecret) {
	throw InvalidInputException(
	    "[duckdb_api][credential_provider] generic credential deserialization is not supported");
}

} // namespace

void RegisterDuckdbApiSecrets(ExtensionLoader &loader) {
	SecretType type;
	type.name = duckdb_api_query_internal::DuckdbApiSecretType();
	type.deserializer = RejectGenericDeserialization;
	type.default_provider = duckdb_api_query_internal::DuckdbApiConfigProvider();
	loader.RegisterSecretType(std::move(type));

	auto &database = loader.GetDatabaseInstance();
	SecretManager::Get(database).LoadSecretStorage(duckdb_api_query_internal::CreateDuckdbApiSecretStorage(database));
	duckdb_api_query_internal::RegisterDuckdbApiCredentialProviders(loader);
}

} // namespace duckdb
