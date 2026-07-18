#pragma once

#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb_api/connector.hpp"
#include "support/query_runtime_scenarios.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace duckdb {
class Connection;
class DuckDB;
} // namespace duckdb

namespace duckdb_api_test {

std::string RuntimeAdapterTokenCanary(char marker);
std::string QueryError(duckdb::Connection &connection, const std::string &sql);
std::shared_ptr<QueryLifecycleProbe> RegisterNativeAdapter(duckdb::DuckDB &database, QueryRuntimeScenario scenario);
std::shared_ptr<QueryLifecycleProbe> RegisterFixtureAdapter(duckdb::DuckDB &database, QueryRuntimeScenario scenario);
void CreateTemporarySecret(duckdb::Connection &connection, const std::string &name, const std::string &token);
void RegisterStoredSecret(duckdb::Connection &connection, duckdb::unique_ptr<const duckdb::BaseSecret> secret,
                          duckdb::SecretPersistType persist_type = duckdb::SecretPersistType::TEMPORARY,
                          const std::string &storage = duckdb::SecretManager::TEMPORARY_STORAGE_NAME);
duckdb::unique_ptr<duckdb::KeyValueSecret> StoredSecret(const std::string &type, const std::string &provider,
                                                        const std::string &name, const duckdb::Value *token);
void LoadPersistentTestSecretStorage(duckdb::DuckDB &database, const std::string &name);

void RunDuckdbAdapterAuthBindTests();
void RunDuckdbAdapterAuthLifecycleTests();

} // namespace duckdb_api_test
