#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "query/support/duckdb_adapter_auth_test_support.hpp"
#include "support/require.hpp"

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using duckdb_api_test::CreateTemporarySecret;
using duckdb_api_test::QueryRuntimeScenario;
using duckdb_api_test::RegisterNativeAdapter;
using duckdb_api_test::Require;
using duckdb_api_test::RuntimeAdapterTokenCanary;

const char SLOW_SQL[] = "SELECT id FROM duckdb_api_scan(connector := 'github', "
                        "relation := 'duckdb_login_search_page')";
const char HEALTHY_SQL[] = "SELECT id, login, site_admin FROM duckdb_api_scan(connector := 'github', "
                           "relation := 'authenticated_user', secret := 'rfc0026_healthy')";

void RequireSuccess(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	if (result->HasError()) {
		throw std::runtime_error("query failed: " + sql + ": " + result->GetError());
	}
}

void TestSlowCallbacksDoNotConsumeDuckdbSharedWorkers() {
	duckdb::DuckDB database(nullptr);
	auto probe = RegisterNativeAdapter(database, QueryRuntimeScenario::BLOCKING_ANONYMOUS_ONLY);
	duckdb::Connection setup(database);
	RequireSuccess(setup, "SET threads=1");
	CreateTemporarySecret(setup, "rfc0026_healthy", RuntimeAdapterTokenCanary('H'));

	for (const auto &prefix : {"PREPARE rfc0026_bound AS ", "DESCRIBE ", "EXPLAIN "}) {
		RequireSuccess(setup, std::string(prefix) + HEALTHY_SQL);
	}
	Require(probe->authorization_open_calls.load(std::memory_order_relaxed) == 0,
	        "offline preparation entered Runtime admission or execution");

	constexpr std::size_t slow_count = 16;
	std::vector<std::unique_ptr<duckdb::Connection>> slow_connections;
	std::vector<std::thread> slow_workers;
	std::vector<std::string> slow_errors(slow_count);
	for (std::size_t index = 0; index < slow_count; index++) {
		slow_connections.emplace_back(new duckdb::Connection(database));
		auto *connection = slow_connections.back().get();
		slow_workers.emplace_back([connection, &slow_errors, index]() {
			auto result = connection->Query(SLOW_SQL);
			slow_errors[index] = result->HasError() ? result->GetError() : "slow scan unexpectedly completed";
		});
	}

	{
		std::unique_lock<std::mutex> guard(probe->mutex);
		const auto ready = probe->condition.wait_for(guard, std::chrono::seconds(5), [&]() {
			return probe->active_waiters.load(std::memory_order_relaxed) == slow_count;
		});
		if (!ready) {
			for (auto &connection : slow_connections) {
				connection->Interrupt();
			}
			for (auto &worker : slow_workers) {
				worker.join();
			}
			throw std::runtime_error("slow scans did not all enter the real adapter callback");
		}
	}

	duckdb::Connection healthy(database);
	auto healthy_result = std::async(std::launch::async, [&]() { return healthy.Query(HEALTHY_SQL); });
	const auto healthy_ready = healthy_result.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
	if (!healthy_ready) {
		healthy.Interrupt();
	}
	for (auto &connection : slow_connections) {
		connection->Interrupt();
	}
	for (auto &worker : slow_workers) {
		worker.join();
	}
	auto result = healthy_result.get();

	Require(healthy_ready, "sixteen slow callbacks exhausted DuckDB's one configured shared execution thread");
	if (result->HasError()) {
		throw std::runtime_error("unrelated healthy scan failed: " + result->GetError());
	}
	const auto healthy_shape = result->RowCount() == 1 && result->GetValue(0, 0).GetValue<int64_t>() == 42;
	const auto healthy_values =
	    result->GetValue(1, 0).ToString() == "authenticated" && result->GetValue(2, 0).GetValue<bool>();
	Require(healthy_shape && healthy_values, "unrelated healthy scan returned the wrong row");
	for (const auto &error : slow_errors) {
		Require(error.find("Interrupt") != std::string::npos || error.find("interrupt") != std::string::npos,
		        "slow scan did not settle as cancellation: " + error);
	}
	Require(probe->active_waiters.load(std::memory_order_relaxed) == 0 &&
	            probe->cancellations.load(std::memory_order_relaxed) == slow_count &&
	            probe->streams_closed.load(std::memory_order_relaxed) == slow_count + 1,
	        "trial did not release every slow and healthy stream exactly once");
}

} // namespace

int main() {
	try {
		TestSlowCallbacksDoNotConsumeDuckdbSharedWorkers();
		std::cout << "RFC 0026 worker-isolation trial passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
