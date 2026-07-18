#include "support/duckdb_secret_test_support.hpp"

#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb_api/connector.hpp"
#include "duckdb_api/duckdb_secret.hpp"
#include "duckdb_api/execution.hpp"
#include "duckdb_api_extension.hpp"
#include "support/require.hpp"

#include <stdexcept>
#include <utility>

namespace duckdb_api_test {
namespace duckdb_secret {
namespace {

class NullExecutor final : public duckdb_api::ScanExecutor {
public:
	std::unique_ptr<duckdb_api::BatchStream> Open(const duckdb_api::ScanPlan &,
	                                              duckdb_api::ExecutionControl &) const override {
		return std::unique_ptr<duckdb_api::BatchStream>();
	}
};

} // namespace

std::string TokenCanary(char marker) {
	std::string result(11, 'q');
	result.push_back('.');
	result.append(13, marker);
	result.append("_phase1");
	return result;
}

void RegisterSecrets(duckdb::DuckDB &database) {
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_secret_test");
	duckdb::RegisterDuckdbApiSecrets(loader);
}

void RegisterProduct(duckdb::ExtensionLoader &loader) {
	duckdb::RegisterDuckdbApi(loader, duckdb_api::BuildNativeGithubConnector(),
	                          std::shared_ptr<const duckdb_api::ScanExecutor>(new NullExecutor()));
}

void RegisterProduct(duckdb::DuckDB &database) {
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_secret_test");
	RegisterProduct(loader);
}

void RequireQueryFailure(duckdb::Connection &connection, const std::string &sql, const std::string &expected,
                         const std::string &forbidden) {
	auto result = connection.Query(sql);
	Require(result->HasError(), "secret statement unexpectedly succeeded");
	const auto error = result->GetError();
	Require(error.find(expected) != std::string::npos, "secret statement used the wrong safe diagnostic");
	Require(forbidden.empty() || error.find(forbidden) == std::string::npos,
	        "secret statement exposed credential bytes");
}

void RequireAuthenticationFailure(const std::function<void()> &action, const std::string &forbidden) {
	bool rejected = false;
	try {
		action();
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = true;
		Require(error.Stage() == duckdb_api::ErrorStage::AUTHENTICATION,
		        "secret resolution used the wrong error stage");
		Require(error.Field() == "secret" || error.Field() == "authorization",
		        "secret resolution used an unstable safe field");
		Require(!error.SafeMessage().empty() && error.SafeMessage().size() <= 128,
		        "secret resolution produced an empty or unbounded diagnostic");
		Require(forbidden.empty() || error.SafeMessage().find(forbidden) == std::string::npos,
		        "secret resolution exposed credential bytes");
	}
	Require(rejected, "secret resolution did not fail closed");
}

void RequireInternalFailure(const std::function<void()> &action, const std::string &forbidden) {
	bool rejected = false;
	try {
		action();
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = true;
		Require(error.Stage() == duckdb_api::ErrorStage::INTERNAL, "host secret failure used the wrong error stage");
		Require(error.Field().empty(), "host secret failure exposed an unstable field");
		Require(error.SafeMessage() == "named secret resolution failed",
		        "host secret failure did not use the fixed safe diagnostic");
		Require(forbidden.empty() || error.SafeMessage().find(forbidden) == std::string::npos,
		        "host secret failure exposed credential bytes");
	}
	Require(rejected, "host secret failure did not fail closed");
}

void RequireCanaryAbsentFromInventory(duckdb::Connection &connection, const std::string &canary) {
	auto inventory = connection.Query("SELECT secret_string FROM duckdb_secrets()");
	Require(!inventory->HasError(), "secret inventory preflight failed");
	for (duckdb::idx_t row = 0; row < inventory->RowCount(); row++) {
		Require(inventory->GetValue(0, row).ToString().find(canary) == std::string::npos,
		        "credential canary existed in a safe surface before injection");
	}
}

std::unique_ptr<duckdb_api::ScanAuthorization> Resolve(duckdb::Connection &connection,
                                                       const std::string &logical_name) {
	std::unique_ptr<duckdb_api::ScanAuthorization> result;
	connection.context->RunFunctionInTransaction([&]() {
		result.reset(
		    new duckdb_api::ScanAuthorization(duckdb::ResolveDuckdbApiSecret(*connection.context, logical_name)));
	});
	return result;
}

void RunTest(const char *name, const std::function<void()> &test) {
	try {
		test();
	} catch (const std::exception &error) {
		throw std::runtime_error(std::string(name) + ": " + error.what());
	}
}

} // namespace duckdb_secret
} // namespace duckdb_api_test
