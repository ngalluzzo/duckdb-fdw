#include "duckdb_api_extension.hpp"

#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/stream_query_result.hpp"
#include "duckdb_api/internal/fixture_runtime.hpp"
#include "support/fixture_scenarios.hpp"
#include "support/require.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

using duckdb_api_test::ACCEPTED_SQL;
using duckdb_api_test::FixtureScenario;
using duckdb_api_test::Require;
using duckdb_api_test::ScenarioFactory;

std::unique_ptr<ScenarioFactory> NewScenario(FixtureScenario scenario, const std::string &body = "") {
	return std::unique_ptr<ScenarioFactory>(new ScenarioFactory(scenario, body));
}

class NullStreamExecutor : public duckdb_api::ScanExecutor {
public:
	std::unique_ptr<duckdb_api::BatchStream> Open(const duckdb_api::ScanPlan &,
	                                              duckdb_api::ExecutionControl &) const override {
		return std::unique_ptr<duckdb_api::BatchStream>();
	}
};

std::shared_ptr<duckdb_api_test::LifecycleProbe> Register(duckdb::DuckDB &database,
                                                          std::unique_ptr<ScenarioFactory> factory) {
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_test");
	auto connector = duckdb_api::BuildCompiledConnector(factory->ContentDigest());
	auto probe = factory->probe;
	duckdb::RegisterDuckdbApi(loader, std::move(connector), duckdb_api::BuildFixtureScanExecutor(std::move(factory)));
	return probe;
}

std::string QueryError(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result->HasError(), "query unexpectedly succeeded: " + sql);
	return result->GetError();
}

void TestSuccessAndOfflineBind() {
	duckdb::DuckDB database(nullptr);
	auto probe = Register(database, NewScenario(FixtureScenario::SUCCESS));
	duckdb::Connection connection(database);
	Require(probe->factory_digest_reads.load(std::memory_order_relaxed) == 1,
	        "registration did not capture fixture identity exactly once");

	auto describe =
	    connection.Query("DESCRIBE SELECT * FROM duckdb_api_scan(connector := 'example', relation := 'items')");
	if (describe->HasError()) {
		throw std::runtime_error("bind-only describe failed: " + describe->GetError());
	}
	Require(probe->sources_opened.load(std::memory_order_relaxed) == 0, "bind opened a fixture source");
	Require(probe->sources_read.load(std::memory_order_relaxed) == 0, "bind read fixture bytes");
	Require(probe->factory_digest_reads.load(std::memory_order_relaxed) == 1,
	        "bind consulted mutable fixture-factory identity");

	auto result = connection.Query(ACCEPTED_SQL);
	if (result->HasError()) {
		throw std::runtime_error("accepted SQL failed: " + result->GetError());
	}
	auto chunk = result->Fetch();
	Require(chunk && chunk->size() == 3, "accepted SQL did not return exactly three rows");
	Require(chunk->GetValue(0, 0).GetValue<int64_t>() == 1, "row 1 id mismatch");
	Require(chunk->GetValue(1, 0).ToString() == "alpha", "row 1 name mismatch");
	Require(chunk->GetValue(2, 0).GetValue<bool>(), "row 1 active mismatch");
	Require(chunk->GetValue(0, 1).GetValue<int64_t>() == 2, "row 2 id mismatch");
	Require(chunk->GetValue(1, 1).ToString() == "beta", "row 2 name mismatch");
	Require(!chunk->GetValue(2, 1).GetValue<bool>(), "row 2 active mismatch");
	Require(chunk->GetValue(0, 2).GetValue<int64_t>() == 3, "row 3 id mismatch");
	Require(chunk->GetValue(1, 2).ToString() == "gamma", "row 3 name mismatch");
	Require(chunk->GetValue(2, 2).GetValue<bool>(), "row 3 active mismatch");
	Require(probe->batches.load(std::memory_order_relaxed) == 2, "success did not use two bounded batches");
	Require(probe->rows.load(std::memory_order_relaxed) == 3, "success batch rows mismatch");
	result.reset();
	Require(probe->streams_opened.load(std::memory_order_relaxed) == 1, "success stream open mismatch");
	Require(probe->streams_closed.load(std::memory_order_relaxed) == 1, "success stream did not close");

	auto filtered = connection.Query(
	    "SELECT id FROM duckdb_api_scan(connector := 'example', relation := 'items') WHERE NOT active");
	Require(!filtered->HasError(), "DuckDB-local filter query failed");
	auto filtered_chunk = filtered->Fetch();
	Require(filtered_chunk && filtered_chunk->size() == 1 && filtered_chunk->GetValue(0, 0).GetValue<int64_t>() == 2,
	        "DuckDB did not retain filter ownership");
	filtered.reset();

	auto ordered = connection.Query(
	    "SELECT id FROM duckdb_api_scan(connector := 'example', relation := 'items') ORDER BY id DESC");
	Require(!ordered->HasError(), "DuckDB-local order query failed");
	auto ordered_chunk = ordered->Fetch();
	Require(ordered_chunk && ordered_chunk->size() == 3 && ordered_chunk->GetValue(0, 0).GetValue<int64_t>() == 3 &&
	            ordered_chunk->GetValue(0, 1).GetValue<int64_t>() == 2 &&
	            ordered_chunk->GetValue(0, 2).GetValue<int64_t>() == 1,
	        "DuckDB did not retain ordering ownership");
	ordered.reset();

	auto offset = connection.Query(
	    "SELECT id FROM duckdb_api_scan(connector := 'example', relation := 'items') ORDER BY id LIMIT 1 OFFSET 1");
	Require(!offset->HasError(), "DuckDB-local offset query failed");
	auto offset_chunk = offset->Fetch();
	Require(offset_chunk && offset_chunk->size() == 1 && offset_chunk->GetValue(0, 0).GetValue<int64_t>() == 2,
	        "DuckDB did not retain limit and offset ownership");
	offset.reset();

	auto dependent = connection.Query("SELECT id FROM duckdb_api_scan(connector := 'example', relation := 'items') "
	                                  "WHERE active ORDER BY id LIMIT 1 OFFSET 1");
	Require(!dependent->HasError(), "DuckDB-local filter-before-limit query failed");
	auto dependent_chunk = dependent->Fetch();
	Require(dependent_chunk && dependent_chunk->size() == 1 && dependent_chunk->GetValue(0, 0).GetValue<int64_t>() == 3,
	        "DuckDB did not preserve filter-before-limit ownership");
	dependent.reset();
	Require(probe->streams_opened.load(std::memory_order_relaxed) == 5, "repeated scan did not open independently");
	Require(probe->streams_closed.load(std::memory_order_relaxed) == 5, "repeated scan did not close independently");
	Require(probe->factory_digest_reads.load(std::memory_order_relaxed) == 6,
	        "execution did not verify fixture identity exactly once per scan");

	const auto unknown_connector =
	    QueryError(connection, "SELECT * FROM duckdb_api_scan(connector := 'other', relation := 'items')");
	Require(unknown_connector.find("unknown connector identifier") != std::string::npos,
	        "unknown connector did not fail during bind");
	const auto unknown_relation =
	    QueryError(connection, "SELECT * FROM duckdb_api_scan(connector := 'example', relation := 'other')");
	Require(unknown_relation.find("unknown relation identifier") != std::string::npos,
	        "unknown relation did not fail during bind");
	Require(probe->streams_opened.load(std::memory_order_relaxed) == 5, "bind failure opened execution state");
}

void TestFrozenConnectorMetadata() {
	duckdb::DuckDB database(nullptr);
	auto factory = NewScenario(FixtureScenario::SUCCESS);
	factory->digest = "runtime-provider-digest";
	factory->source_digest = factory->digest;
	auto probe = factory->probe;
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_test");
	duckdb::RegisterDuckdbApi(loader, duckdb_api::BuildCompiledConnector("frozen-registration-digest"),
	                          duckdb_api::BuildFixtureScanExecutor(std::move(factory)));
	duckdb::Connection connection(database);
	auto describe =
	    connection.Query("DESCRIBE SELECT * FROM duckdb_api_scan(connector := 'example', relation := 'items')");
	Require(!describe->HasError(), "bind did not use frozen connector metadata");
	Require(probe->factory_digest_reads.load(std::memory_order_relaxed) == 0,
	        "bind rebuilt connector metadata from the runtime provider");
	const auto error = QueryError(connection, ACCEPTED_SQL);
	Require(error.find("[duckdb_api][policy] connector=example relation=items: fixture identity does not match the "
	                   "immutable scan plan") != std::string::npos,
	        "execution did not reject a fixture identity that drifted after registration");
	Require(probe->sources_opened.load(std::memory_order_relaxed) == 0,
	        "identity drift opened a fixture source before rejection");
}

void TestMissingRuntimeStreamIsRedacted() {
	duckdb::DuckDB database(nullptr);
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_test");
	auto executor = std::shared_ptr<const duckdb_api::ScanExecutor>(new NullStreamExecutor());
	duckdb::RegisterDuckdbApi(loader, duckdb_api::BuildCompiledConnector("test-only-fixture-digest"), executor);
	duckdb::Connection connection(database);
	const auto error = QueryError(connection, ACCEPTED_SQL);
	Require(error.find("[duckdb_api][internal] connector=example relation=items: unexpected execution failure") !=
	                std::string::npos &&
	            error.find("scan executor returned no stream") == std::string::npos,
	        "adapter did not contain and redact a missing runtime stream");
}

void TestFailure(FixtureScenario scenario, const std::string &expected, const std::string &forbidden,
                 const std::string &custom_body = "") {
	duckdb::DuckDB database(nullptr);
	auto probe = Register(database, NewScenario(scenario, custom_body));
	duckdb::Connection connection(database);
	const auto error = QueryError(connection, ACCEPTED_SQL);
	Require(error.find(expected) != std::string::npos, "failure category or safe context mismatch: " + error);
	Require(error.find(forbidden) == std::string::npos, "failure leaked rejected input: " + error);
	Require(probe->streams_opened.load(std::memory_order_relaxed) == 1, "failure stream open mismatch");
	Require(probe->streams_closed.load(std::memory_order_relaxed) == 1, "failure stream did not close once");
}

void TestFailuresAndRedaction() {
	TestFailure(FixtureScenario::MALFORMED,
	            "[duckdb_api][decode] connector=example relation=items: response is not valid JSON",
	            "top-secret-malformed");
	TestFailure(FixtureScenario::TYPE_MISMATCH,
	            "[duckdb_api][schema] connector=example relation=items: field id cannot be converted to BIGINT",
	            "top-secret-type-value");
	TestFailure(FixtureScenario::UNKNOWN_FAILURE,
	            "[duckdb_api][internal] connector=example relation=items: unexpected execution failure",
	            "top-secret-unknown-fixture-payload");
}

void TestProviderHookFailuresAreContained() {
	duckdb::DuckDB open_database(nullptr);
	auto open_factory = NewScenario(FixtureScenario::SUCCESS);
	open_factory->failures.stream_open = true;
	open_factory->failures.close = true;
	auto open_probe = Register(open_database, std::move(open_factory));
	duckdb::Connection open_connection(open_database);
	auto error = QueryError(open_connection, ACCEPTED_SQL);
	Require(error.find("[duckdb_api][internal] connector=example relation=items: unexpected execution failure") !=
	                std::string::npos &&
	            error.find("top-secret-open-hook-failure") == std::string::npos &&
	            error.find("top-secret-close-hook-failure") == std::string::npos,
	        "adapter leaked or replaced an open-hook failure");
	Require(open_probe->streams_closed.load(std::memory_order_relaxed) == 1,
	        "adapter open-hook failure did not trigger contained cleanup");

	duckdb::DuckDB read_database(nullptr);
	auto read_factory = NewScenario(FixtureScenario::SUCCESS);
	read_factory->failures.read = true;
	read_factory->failures.close = true;
	auto read_probe = Register(read_database, std::move(read_factory));
	duckdb::Connection read_connection(read_database);
	error = QueryError(read_connection, ACCEPTED_SQL);
	Require(error.find("[duckdb_api][internal] connector=example relation=items: unexpected execution failure") !=
	                std::string::npos &&
	            error.find("top-secret-read-hook-failure") == std::string::npos &&
	            error.find("top-secret-close-hook-failure") == std::string::npos,
	        "adapter leaked or replaced a read-hook failure");
	Require(read_probe->streams_closed.load(std::memory_order_relaxed) == 1,
	        "adapter read-hook failure did not close exactly once");
}

void TestEarlyCloseAndConnectionShutdown() {
	duckdb::DuckDB database(nullptr);
	auto factory = NewScenario(FixtureScenario::SUCCESS);
	factory->failures.close = true;
	auto probe = Register(database, std::move(factory));
	{
		duckdb::Connection connection(database);
		auto result = connection.SendQuery(
		    "SELECT id, name, active FROM duckdb_api_scan(connector := 'example', relation := 'items')");
		Require(!result->HasError(), "streaming scan failed before early close");
		auto first = result->Fetch();
		Require(first && first->size() == 2, "streaming scan did not expose its bounded first batch");
		result->Cast<duckdb::StreamQueryResult>().Close();
		result.reset();
		auto barrier = connection.Query("SELECT 42");
		Require(!barrier->HasError(), "connection did not settle after early stream close");
		Require(probe->streams_closed.load(std::memory_order_relaxed) == 1,
		        "early consumer close did not close the stream");
	}
	std::unique_ptr<duckdb::QueryResult> result;
	{
		std::unique_ptr<duckdb::Connection> connection(new duckdb::Connection(database));
		result = connection->SendQuery(
		    "SELECT id, name, active FROM duckdb_api_scan(connector := 'example', relation := 'items')");
		Require(!result->HasError(), "streaming scan failed before connection shutdown");
		auto first = result->Fetch();
		Require(first && first->size() == 2, "connection-shutdown scan did not expose its bounded first batch");
		connection.reset();
	}
	result.reset();
	Require(probe->streams_opened.load(std::memory_order_relaxed) == 2 &&
	            probe->streams_closed.load(std::memory_order_relaxed) == 2,
	        "connection shutdown did not close its active stream exactly once");
}

void TestConcurrentScansOwnIndependentState() {
	duckdb::DuckDB database(nullptr);
	auto probe = Register(database, NewScenario(FixtureScenario::SUCCESS));
	duckdb::Connection first_connection(database);
	duckdb::Connection second_connection(database);
	std::string first_error;
	std::string second_error;
	auto run = [](duckdb::Connection &connection, std::string &error) {
		auto result = connection.Query(ACCEPTED_SQL);
		if (result->HasError()) {
			error = result->GetError();
			return;
		}
		auto chunk = result->Fetch();
		if (!chunk || chunk->size() != 3 || chunk->GetValue(0, 0).GetValue<int64_t>() != 1 ||
		    chunk->GetValue(0, 2).GetValue<int64_t>() != 3) {
			error = "concurrent scan returned the wrong independent row sequence";
		}
	};
	std::thread first([&]() { run(first_connection, first_error); });
	std::thread second([&]() { run(second_connection, second_error); });
	first.join();
	second.join();
	Require(first_error.empty(), "first concurrent scan failed: " + first_error);
	Require(second_error.empty(), "second concurrent scan failed: " + second_error);
	Require(probe->streams_opened.load(std::memory_order_relaxed) == 2 &&
	            probe->streams_closed.load(std::memory_order_relaxed) == 2,
	        "concurrent scans shared or leaked stream state");
}

void TestSynchronizedCancellation() {
	duckdb::DuckDB database(nullptr);
	auto factory = NewScenario(FixtureScenario::BLOCKING);
	factory->failures.interruption = true;
	factory->failures.close = true;
	auto probe = Register(database, std::move(factory));
	duckdb::Connection connection(database);
	std::string error;
	std::thread worker([&]() {
		auto result = connection.Query(ACCEPTED_SQL);
		if (!result->HasError()) {
			error = "blocking scan unexpectedly succeeded";
		} else {
			error = result->GetError();
		}
	});

	{
		std::unique_lock<std::mutex> guard(probe->mutex);
		const auto ready = probe->condition.wait_for(guard, std::chrono::seconds(5), [&]() {
			return probe->active_waiters.load(std::memory_order_relaxed) == 1;
		});
		if (!ready) {
			connection.Interrupt();
			worker.join();
			throw std::runtime_error("blocking fixture did not reach its synchronized cancellation point");
		}
	}
	connection.Interrupt();
	worker.join();
	Require(error.find("Interrupt") != std::string::npos || error.find("interrupt") != std::string::npos,
	        "blocking scan did not report interruption: " + error);
	Require(probe->active_waiters.load(std::memory_order_relaxed) == 0, "cancellation left an active fixture waiter");
	Require(probe->interruptions.load(std::memory_order_relaxed) == 1,
	        "fixture execution did not observe cancellation");
	Require(probe->streams_opened.load(std::memory_order_relaxed) == 1, "cancel stream open mismatch");
	Require(probe->streams_closed.load(std::memory_order_relaxed) == 1, "cancel stream did not close once");
}

void TestCancellationAfterRuntimeBatch() {
	duckdb::DuckDB database(nullptr);
	auto factory = NewScenario(FixtureScenario::SUCCESS);
	factory->failures.batch_block = true;
	auto probe = Register(database, std::move(factory));
	duckdb::Connection connection(database);
	std::string error;
	std::thread worker([&]() {
		auto result = connection.Query(ACCEPTED_SQL);
		error = result->HasError() ? result->GetError() : "post-batch cancellation unexpectedly succeeded";
	});
	{
		std::unique_lock<std::mutex> guard(probe->mutex);
		const auto ready = probe->condition.wait_for(guard, std::chrono::seconds(5), [&]() {
			return probe->active_waiters.load(std::memory_order_relaxed) == 1;
		});
		if (!ready) {
			connection.Interrupt();
			probe->release_batches.store(true, std::memory_order_relaxed);
			probe->condition.notify_all();
			guard.unlock();
			worker.join();
			throw std::runtime_error("fixture did not reach its post-batch cancellation point");
		}
	}
	connection.Interrupt();
	probe->release_batches.store(true, std::memory_order_relaxed);
	probe->condition.notify_all();
	worker.join();
	Require(error.find("Interrupt") != std::string::npos || error.find("interrupt") != std::string::npos,
	        "post-batch cancellation did not report DuckDB interruption: " + error);
	Require(probe->interruptions.load(std::memory_order_relaxed) == 1,
	        "adapter-side Cancel did not report provider interruption exactly once");
	Require(probe->streams_closed.load(std::memory_order_relaxed) == 1,
	        "post-batch cancellation did not close exactly once");
}

} // namespace

int main() {
	try {
		TestSuccessAndOfflineBind();
		TestFrozenConnectorMetadata();
		TestMissingRuntimeStreamIsRedacted();
		TestFailuresAndRedaction();
		TestProviderHookFailuresAreContained();
		TestEarlyCloseAndConnectionShutdown();
		TestConcurrentScansOwnIndependentState();
		TestSynchronizedCancellation();
		TestCancellationAfterRuntimeBatch();
		std::cout << "DuckDB adapter tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "DuckDB adapter tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
