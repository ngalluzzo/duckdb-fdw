#include "duckdb_api_extension.hpp"

#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/main/stream_query_result.hpp"
#include "duckdb_api/embedded_example.hpp"

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

const char *const ACCEPTED_SQL =
    "SELECT id, name, active FROM duckdb_api_scan(connector := 'example', relation := 'items') ORDER BY id";

const char *const SUCCESS_RESPONSE = "{\"items\":[{\"id\":1,\"name\":\"alpha\",\"active\":true},"
                                     "{\"id\":2,\"name\":\"beta\",\"active\":false},"
                                     "{\"id\":3,\"name\":\"gamma\",\"active\":true}]}\n";

enum class FixtureScenario : uint8_t { SUCCESS, MALFORMED, TYPE_MISMATCH, BLOCKING, UNKNOWN_FAILURE };

struct LifecycleProbe {
	std::atomic<uint64_t> sources_opened {0};
	std::atomic<uint64_t> sources_read {0};
	std::atomic<uint64_t> streams_opened {0};
	std::atomic<uint64_t> streams_closed {0};
	std::atomic<uint64_t> batches {0};
	std::atomic<uint64_t> rows {0};
	std::atomic<uint64_t> interruptions {0};
	std::atomic<uint64_t> active_waiters {0};
	std::atomic<uint64_t> factory_digest_reads {0};
	std::mutex mutex;
	std::condition_variable condition;
};

class ScenarioSource : public duckdb_api::FixtureSource {
public:
	ScenarioSource(FixtureScenario scenario_p, std::string digest_p, std::shared_ptr<LifecycleProbe> probe_p,
	               std::string custom_body_p)
	    : scenario(scenario_p), digest(std::move(digest_p)), probe(std::move(probe_p)),
	      custom_body(std::move(custom_body_p)) {
		probe->sources_opened.fetch_add(1, std::memory_order_relaxed);
	}

	const std::string &ContentDigest() const override {
		return digest;
	}

	void Read(duckdb_api::FixtureReadBuffer &buffer) override {
		probe->sources_read.fetch_add(1, std::memory_order_relaxed);
		if (scenario == FixtureScenario::UNKNOWN_FAILURE) {
			throw std::runtime_error("top-secret-unknown-fixture-payload");
		}
		if (scenario == FixtureScenario::BLOCKING) {
			probe->active_waiters.fetch_add(1, std::memory_order_relaxed);
			probe->condition.notify_all();
			try {
				while (true) {
					buffer.Checkpoint();
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			} catch (...) {
				probe->active_waiters.fetch_sub(1, std::memory_order_relaxed);
				throw;
			}
		}
		if (!custom_body.empty()) {
			buffer.Append(custom_body);
			return;
		}
		if (scenario == FixtureScenario::MALFORMED) {
			buffer.Append("{\"items\":[{\"id\":1,\"name\":\"top-secret-malformed\"");
			return;
		}
		if (scenario == FixtureScenario::TYPE_MISMATCH) {
			buffer.Append("{\"items\":[{\"id\":\"top-secret-type-value\",\"name\":\"alpha\",\"active\":true}]}");
			return;
		}
		buffer.Append(SUCCESS_RESPONSE);
	}

	void OnStreamOpen() override {
		probe->streams_opened.fetch_add(1, std::memory_order_relaxed);
	}

	void OnBatch(duckdb::idx_t row_count) override {
		probe->batches.fetch_add(1, std::memory_order_relaxed);
		probe->rows.fetch_add(row_count, std::memory_order_relaxed);
	}

	void OnInterruption() override {
		probe->interruptions.fetch_add(1, std::memory_order_relaxed);
	}

	void OnStreamClose() override {
		probe->streams_closed.fetch_add(1, std::memory_order_relaxed);
	}

private:
	FixtureScenario scenario;
	std::string digest;
	std::shared_ptr<LifecycleProbe> probe;
	std::string custom_body;
};

class ScenarioFactory : public duckdb_api::FixtureFactory {
public:
	ScenarioFactory(FixtureScenario scenario_p, std::string custom_body_p = "")
	    : scenario(scenario_p), digest("test-only-fixture-digest"), probe(std::make_shared<LifecycleProbe>()),
	      custom_body(std::move(custom_body_p)) {
	}

	const std::string &ContentDigest() const override {
		probe->factory_digest_reads.fetch_add(1, std::memory_order_relaxed);
		return digest;
	}

	std::unique_ptr<duckdb_api::FixtureSource> Open() const override {
		return std::unique_ptr<duckdb_api::FixtureSource>(new ScenarioSource(scenario, digest, probe, custom_body));
	}

	FixtureScenario scenario;
	std::string digest;
	std::shared_ptr<LifecycleProbe> probe;
	std::string custom_body;
};

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void Register(duckdb::DuckDB &database, const duckdb::shared_ptr<ScenarioFactory> &factory) {
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_test");
	duckdb::RegisterDuckdbApi(loader, factory);
}

std::string QueryError(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result->HasError(), "query unexpectedly succeeded: " + sql);
	return result->GetError();
}

void RequirePlanRejected(duckdb_api::ScanPlan plan, const std::string &field) {
	auto factory = duckdb::make_shared_ptr<ScenarioFactory>(FixtureScenario::SUCCESS);
	bool rejected = false;
	try {
		auto stream = duckdb_api::OpenBatchStream(plan, *factory);
		stream->Close();
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = error.Stage() == duckdb_api::ErrorStage::POLICY &&
		           error.SafeMessage() == "scan plan is not authorized for fixture execution";
	}
	Require(rejected, "fixture executor accepted a mutated " + field);
	Require(factory->probe->sources_opened.load(std::memory_order_relaxed) == 0,
	        "fixture executor opened a source before rejecting " + field);
}

void TestFixturePlanValidation() {
	const auto connector = duckdb_api::BuildCompiledConnector("fixture-digest");
	const auto request = duckdb_api::BuildConservativeScanRequest();
	auto plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.method = "POST";
	RequirePlanRejected(plan, "method");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.path = "/other";
	RequirePlanRejected(plan, "path");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.extractor = "$.other[*]";
	RequirePlanRejected(plan, "extractor");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.output_columns.pop_back();
	RequirePlanRejected(plan, "projection");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.remote_predicate = "id > 1";
	RequirePlanRejected(plan, "remote predicate");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.runtime_residual_predicate = "id > 1";
	RequirePlanRejected(plan, "runtime residual");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.remote_ordering.push_back("id");
	RequirePlanRejected(plan, "remote ordering");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.has_remote_limit = true;
	RequirePlanRejected(plan, "remote limit");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.has_remote_offset = true;
	RequirePlanRejected(plan, "remote offset");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.has_runtime_limit = true;
	RequirePlanRejected(plan, "runtime limit");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.has_runtime_offset = true;
	RequirePlanRejected(plan, "runtime offset");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.duckdb_owned_operations.pop_back();
	RequirePlanRejected(plan, "DuckDB ownership");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.pagination_enabled = true;
	RequirePlanRejected(plan, "pagination capability");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.providers_enabled = true;
	RequirePlanRejected(plan, "provider capability");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.retry_enabled = true;
	RequirePlanRejected(plan, "retry capability");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.cache_enabled = true;
	RequirePlanRejected(plan, "cache capability");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.network_enabled = true;
	RequirePlanRejected(plan, "network capability");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.budgets.fixture_bytes++;
	RequirePlanRejected(plan, "fixture-byte budget");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.budgets.decoded_records++;
	RequirePlanRejected(plan, "record budget");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.budgets.name_bytes++;
	RequirePlanRejected(plan, "name budget");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.budgets.json_nesting++;
	RequirePlanRejected(plan, "nesting budget");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.budgets.batch_rows++;
	RequirePlanRejected(plan, "batch budget");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.budgets.wall_milliseconds++;
	RequirePlanRejected(plan, "wall-time budget");
	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	plan.budgets.concurrency++;
	RequirePlanRejected(plan, "concurrency budget");

	plan = duckdb_api::BuildConservativeScanPlan(connector, request);
	auto factory = duckdb::make_shared_ptr<ScenarioFactory>(FixtureScenario::SUCCESS);
	bool identity_rejected = false;
	try {
		auto stream = duckdb_api::OpenBatchStream(plan, *factory);
		stream->Close();
	} catch (const duckdb_api::ExecutionError &error) {
		identity_rejected = error.Stage() == duckdb_api::ErrorStage::POLICY;
	}
	Require(identity_rejected, "fixture executor accepted a mismatched factory identity");
	Require(factory->probe->sources_opened.load(std::memory_order_relaxed) == 0,
	        "fixture executor opened a source before rejecting its factory identity");
}

void TestSuccessAndOfflineBind() {
	duckdb::DuckDB database(nullptr);
	auto factory = duckdb::make_shared_ptr<ScenarioFactory>(FixtureScenario::SUCCESS);
	Register(database, factory);
	duckdb::Connection connection(database);
	Require(factory->probe->factory_digest_reads.load(std::memory_order_relaxed) == 1,
	        "registration did not capture fixture identity exactly once");

	auto describe =
	    connection.Query("DESCRIBE SELECT * FROM duckdb_api_scan(connector := 'example', relation := 'items')");
	if (describe->HasError()) {
		throw std::runtime_error("bind-only describe failed: " + describe->GetError());
	}
	Require(factory->probe->sources_opened.load(std::memory_order_relaxed) == 0, "bind opened a fixture source");
	Require(factory->probe->sources_read.load(std::memory_order_relaxed) == 0, "bind read fixture bytes");
	Require(factory->probe->factory_digest_reads.load(std::memory_order_relaxed) == 1,
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
	Require(factory->probe->batches.load(std::memory_order_relaxed) == 2, "success did not use two bounded batches");
	Require(factory->probe->rows.load(std::memory_order_relaxed) == 3, "success batch rows mismatch");
	result.reset();
	Require(factory->probe->streams_opened.load(std::memory_order_relaxed) == 1, "success stream open mismatch");
	Require(factory->probe->streams_closed.load(std::memory_order_relaxed) == 1, "success stream did not close");

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
	Require(factory->probe->streams_opened.load(std::memory_order_relaxed) == 5,
	        "repeated scan did not open independently");
	Require(factory->probe->streams_closed.load(std::memory_order_relaxed) == 5,
	        "repeated scan did not close independently");
	Require(factory->probe->factory_digest_reads.load(std::memory_order_relaxed) == 6,
	        "execution did not verify fixture identity exactly once per scan");

	const auto unknown_connector =
	    QueryError(connection, "SELECT * FROM duckdb_api_scan(connector := 'other', relation := 'items')");
	Require(unknown_connector.find("unknown connector identifier") != std::string::npos,
	        "unknown connector did not fail during bind");
	const auto unknown_relation =
	    QueryError(connection, "SELECT * FROM duckdb_api_scan(connector := 'example', relation := 'other')");
	Require(unknown_relation.find("unknown relation identifier") != std::string::npos,
	        "unknown relation did not fail during bind");
	Require(factory->probe->streams_opened.load(std::memory_order_relaxed) == 5, "bind failure opened execution state");
}

void TestFrozenConnectorMetadata() {
	duckdb::DuckDB database(nullptr);
	auto factory = duckdb::make_shared_ptr<ScenarioFactory>(FixtureScenario::SUCCESS);
	Register(database, factory);
	Require(factory->probe->factory_digest_reads.load(std::memory_order_relaxed) == 1,
	        "registration did not capture connector metadata");
	factory->digest = "mutated-after-registration";
	duckdb::Connection connection(database);
	auto describe =
	    connection.Query("DESCRIBE SELECT * FROM duckdb_api_scan(connector := 'example', relation := 'items')");
	Require(!describe->HasError(), "frozen connector metadata did not survive fixture-factory mutation");
	Require(factory->probe->factory_digest_reads.load(std::memory_order_relaxed) == 1,
	        "bind rebuilt connector metadata from the mutated factory");
	const auto error = QueryError(connection, ACCEPTED_SQL);
	Require(error.find("[duckdb_api][policy] connector=example relation=items: fixture identity does not match the "
	                   "immutable scan plan") != std::string::npos,
	        "execution did not reject a fixture identity that drifted after registration");
	Require(factory->probe->sources_opened.load(std::memory_order_relaxed) == 0,
	        "identity drift opened a fixture source before rejection");
}

void TestUnicodeAndAdditiveFields() {
	duckdb::DuckDB database(nullptr);
	auto factory = duckdb::make_shared_ptr<ScenarioFactory>(
	    FixtureScenario::SUCCESS,
	    "{\"metadata\":{\"nested\":[1,{\"ignored\":true}]},\"items\":[{\"id\":1,\"name\":\"\\u0061lpha\","
	    "\"active\":true,\"future\":{\"also\":\"ignored\"}},{\"id\":2,\"name\":\"\\uD83D\\uDE00\","
	    "\"active\":false}]}");
	Register(database, factory);
	duckdb::Connection connection(database);
	auto result = connection.Query(ACCEPTED_SQL);
	if (result->HasError()) {
		throw std::runtime_error("valid Unicode or additive fields broke extraction: " + result->GetError());
	}
	auto chunk = result->Fetch();
	Require(chunk && chunk->size() == 2 && chunk->GetValue(1, 0).ToString() == "alpha" &&
	            chunk->GetValue(1, 1).ToString() == "\xf0\x9f\x98\x80",
	        "Unicode escape was not decoded losslessly");
	result.reset();
	Require(factory->probe->streams_opened.load(std::memory_order_relaxed) == 1 &&
	            factory->probe->streams_closed.load(std::memory_order_relaxed) == 1,
	        "compatible extraction did not close exactly once");
}

void TestLosslessBigintConversion() {
	duckdb::DuckDB database(nullptr);
	auto factory = duckdb::make_shared_ptr<ScenarioFactory>(
	    FixtureScenario::SUCCESS,
	    "{\"items\":[{\"id\":1.0,\"name\":\"alpha\",\"active\":true},{\"id\":2e0,\"name\":\"beta\","
	    "\"active\":false},{\"id\":300e-2,\"name\":\"gamma\",\"active\":true}]}");
	Register(database, factory);
	duckdb::Connection connection(database);
	auto result = connection.Query(ACCEPTED_SQL);
	if (result->HasError()) {
		throw std::runtime_error("lossless BIGINT conversion failed: " + result->GetError());
	}
	auto chunk = result->Fetch();
	Require(chunk && chunk->size() == 3 && chunk->GetValue(0, 0).GetValue<int64_t>() == 1 &&
	            chunk->GetValue(0, 1).GetValue<int64_t>() == 2 && chunk->GetValue(0, 2).GetValue<int64_t>() == 3,
	        "exact decimal or exponent syntax did not convert losslessly to BIGINT");
}

void TestFailure(FixtureScenario scenario, const std::string &expected, const std::string &forbidden,
                 const std::string &custom_body = "") {
	duckdb::DuckDB database(nullptr);
	auto factory = duckdb::make_shared_ptr<ScenarioFactory>(scenario, custom_body);
	Register(database, factory);
	duckdb::Connection connection(database);
	const auto error = QueryError(connection, ACCEPTED_SQL);
	Require(error.find(expected) != std::string::npos, "failure category or safe context mismatch: " + error);
	Require(error.find(forbidden) == std::string::npos, "failure leaked rejected input: " + error);
	Require(factory->probe->streams_opened.load(std::memory_order_relaxed) == 1, "failure stream open mismatch");
	Require(factory->probe->streams_closed.load(std::memory_order_relaxed) == 1, "failure stream did not close once");
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
	TestFailure(FixtureScenario::SUCCESS,
	            "[duckdb_api][schema] connector=example relation=items: required field id is missing",
	            "top-secret-missing", "{\"items\":[{\"name\":\"top-secret-missing\",\"active\":true}]}");
	TestFailure(FixtureScenario::SUCCESS,
	            "[duckdb_api][schema] connector=example relation=items: field id cannot be converted to BIGINT",
	            "9223372036854775808", "{\"items\":[{\"id\":9223372036854775808,\"name\":\"alpha\",\"active\":true}]}");
	TestFailure(FixtureScenario::SUCCESS,
	            "[duckdb_api][schema] connector=example relation=items: field id cannot be converted to BIGINT", "1.5",
	            "{\"items\":[{\"id\":1.5,\"name\":\"alpha\",\"active\":true}]}");
	TestFailure(FixtureScenario::SUCCESS,
	            "[duckdb_api][schema] connector=example relation=items: field id cannot be converted to BIGINT",
	            "9223372036854775808.0",
	            "{\"items\":[{\"id\":9223372036854775808.0,\"name\":\"alpha\",\"active\":true}]}");
	TestFailure(FixtureScenario::SUCCESS,
	            "[duckdb_api][schema] connector=example relation=items: field active cannot be converted to BOOLEAN",
	            "top-secret-active", "{\"items\":[{\"id\":1,\"name\":\"alpha\",\"active\":\"top-secret-active\"}]}");
	TestFailure(FixtureScenario::SUCCESS,
	            "[duckdb_api][schema] connector=example relation=items: field id cannot be converted to BIGINT",
	            "top-secret-object",
	            "{\"items\":[{\"id\":{\"secret\":\"top-secret-object\"},\"name\":\"alpha\",\"active\":true}]}");
	TestFailure(FixtureScenario::SUCCESS,
	            "[duckdb_api][schema] connector=example relation=items: response extractor did not match the required "
	            "field",
	            "top-secret-wrapper", "{\"other\":{\"nested\":[\"top-secret-wrapper\"]}}");
	TestFailure(FixtureScenario::SUCCESS,
	            "[duckdb_api][schema] connector=example relation=items: response extractor did not produce an array",
	            "top-secret-items", "{\"items\":{\"secret\":\"top-secret-items\"}}");
	TestFailure(FixtureScenario::SUCCESS,
	            "[duckdb_api][schema] connector=example relation=items: response item does not match the declared row "
	            "shape",
	            "top-secret-scalar", "{\"items\":[null],\"secret\":\"top-secret-scalar\"}");
	TestFailure(FixtureScenario::SUCCESS,
	            "[duckdb_api][decode] connector=example relation=items: response is not valid JSON",
	            "top-secret-trailing", "{\"items\":[{\"id\":1,\"name\":\"top-secret-trailing\",\"active\":true},]}");
	TestFailure(
	    FixtureScenario::SUCCESS, "[duckdb_api][decode] connector=example relation=items: response is not valid JSON",
	    "top-secret-surrogate", "{\"items\":[{\"id\":1,\"name\":\"\\uD800top-secret-surrogate\",\"active\":true}]}");
	std::string invalid_utf8 = "{\"items\":[{\"id\":1,\"name\":\"";
	invalid_utf8.push_back(static_cast<char>(0xc3));
	invalid_utf8 += "(top-secret-invalid-utf8\",\"active\":true}]}";
	TestFailure(FixtureScenario::SUCCESS,
	            "[duckdb_api][decode] connector=example relation=items: response is not valid JSON",
	            "top-secret-invalid-utf8", invalid_utf8);
	TestFailure(FixtureScenario::SUCCESS,
	            "[duckdb_api][policy] connector=example relation=items: field name exceeds the configured resource "
	            "budget",
	            std::string(129, 'x'),
	            "{\"items\":[{\"id\":1,\"name\":\"" + std::string(129, 'x') + "\",\"active\":true}]}");
	TestFailure(FixtureScenario::SUCCESS,
	            "[duckdb_api][policy] connector=example relation=items: response exceeds the fixture-byte budget",
	            "top-secret-oversize", std::string(4097, 'x') + "top-secret-oversize");
}

void TestWallTimeBudget() {
	duckdb::DuckDB database(nullptr);
	auto factory = duckdb::make_shared_ptr<ScenarioFactory>(FixtureScenario::BLOCKING);
	Register(database, factory);
	duckdb::Connection connection(database);
	const auto started = std::chrono::steady_clock::now();
	const auto error = QueryError(connection, ACCEPTED_SQL);
	const auto elapsed =
	    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
	Require(
	    error.find("[duckdb_api][policy] connector=example relation=items: execution exceeds the wall-time budget") !=
	        std::string::npos,
	    "blocking read did not report the wall-time budget: " + error);
	Require(elapsed >= 4500 && elapsed < 8000, "wall-time budget did not terminate blocking fixture promptly");
	Require(factory->probe->active_waiters.load(std::memory_order_relaxed) == 0,
	        "wall-time budget left an active fixture waiter");
	Require(factory->probe->streams_opened.load(std::memory_order_relaxed) == 1 &&
	            factory->probe->streams_closed.load(std::memory_order_relaxed) == 1,
	        "wall-time failure did not close exactly once");
}

void TestEarlyCloseAndConnectionShutdown() {
	duckdb::DuckDB database(nullptr);
	auto factory = duckdb::make_shared_ptr<ScenarioFactory>(FixtureScenario::SUCCESS);
	Register(database, factory);
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
		Require(factory->probe->streams_closed.load(std::memory_order_relaxed) == 1,
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
	Require(factory->probe->streams_opened.load(std::memory_order_relaxed) == 2 &&
	            factory->probe->streams_closed.load(std::memory_order_relaxed) == 2,
	        "connection shutdown did not close its active stream exactly once");
}

void TestConcurrentScansOwnIndependentState() {
	duckdb::DuckDB database(nullptr);
	auto factory = duckdb::make_shared_ptr<ScenarioFactory>(FixtureScenario::SUCCESS);
	Register(database, factory);
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
	Require(factory->probe->streams_opened.load(std::memory_order_relaxed) == 2 &&
	            factory->probe->streams_closed.load(std::memory_order_relaxed) == 2,
	        "concurrent scans shared or leaked stream state");
}

void TestSynchronizedCancellation() {
	duckdb::DuckDB database(nullptr);
	auto factory = duckdb::make_shared_ptr<ScenarioFactory>(FixtureScenario::BLOCKING);
	Register(database, factory);
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
		std::unique_lock<std::mutex> guard(factory->probe->mutex);
		const auto ready = factory->probe->condition.wait_for(guard, std::chrono::seconds(5), [&]() {
			return factory->probe->active_waiters.load(std::memory_order_relaxed) == 1;
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
	Require(factory->probe->active_waiters.load(std::memory_order_relaxed) == 0,
	        "cancellation left an active fixture waiter");
	Require(factory->probe->interruptions.load(std::memory_order_relaxed) >= 1,
	        "fixture execution did not observe cancellation");
	Require(factory->probe->streams_opened.load(std::memory_order_relaxed) == 1, "cancel stream open mismatch");
	Require(factory->probe->streams_closed.load(std::memory_order_relaxed) == 1, "cancel stream did not close once");
}

} // namespace

int main() {
	try {
		TestFixturePlanValidation();
		TestSuccessAndOfflineBind();
		TestFrozenConnectorMetadata();
		TestUnicodeAndAdditiveFields();
		TestLosslessBigintConversion();
		TestFailuresAndRedaction();
		TestWallTimeBudget();
		TestEarlyCloseAndConnectionShutdown();
		TestConcurrentScansOwnIndependentState();
		TestSynchronizedCancellation();
		std::cout << "duckdb_api contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "duckdb_api contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
