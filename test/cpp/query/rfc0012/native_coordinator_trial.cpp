#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "query/rfc0012/coordinator_trial_support.hpp"
#include "support/require.hpp"

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using duckdb_api_test::Require;
using duckdb_api_test::rfc0012::CoordinatorTrialControl;
using duckdb_api_test::rfc0012::RegisterNativeCoordinatorTrial;
using duckdb_api_test::rfc0012::RegisterUnrelatedSystemFunction;
using duckdb_api_test::rfc0012::RegisterUnrelatedSystemOverload;

duckdb::unique_ptr<duckdb::MaterializedQueryResult> Query(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	if (result->HasError()) {
		throw std::runtime_error("query failed: " + sql + ": " + result->GetError());
	}
	return result;
}

std::string QueryError(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result->HasError(), "query unexpectedly succeeded: " + sql);
	return result->GetError();
}

void RequireErrorContains(duckdb::Connection &connection, const std::string &sql, const std::string &fragment) {
	const auto error = QueryError(connection, sql);
	Require(error.find(fragment) != std::string::npos, "query error did not contain '" + fragment + "': " + error);
}

void RequireRelation(duckdb::Connection &connection, const std::string &name, std::uint64_t generation,
                     std::int64_t input = 7, const std::string &catalog = std::string()) {
	const auto qualified = catalog.empty() ? name : catalog + "." + name;
	auto result = Query(connection, "SELECT generation, relation, input FROM " + qualified +
	                                    "(input := " + std::to_string(input) + ")");
	Require(result->RowCount() == 1 && result->ColumnCount() == 3, "relation returned the wrong shape: " + qualified);
	Require(result->GetValue(0, 0).GetValue<std::uint64_t>() == generation &&
	            result->GetValue(1, 0).ToString() == name && result->GetValue(2, 0).GetValue<std::int64_t>() == input,
	        "relation mixed generation, identity, or typed input: " + qualified);
}

void RequireInventory(duckdb::Connection &connection, std::uint64_t generation, const std::string &expected_relations) {
	auto result =
	    Query(connection, "SELECT generation, relation FROM rfc0012_generation_inventory() ORDER BY relation");
	std::string actual_relations;
	for (duckdb::idx_t row = 0; row < result->RowCount(); row++) {
		Require(result->GetValue(0, row).GetValue<std::uint64_t>() == generation,
		        "inventory exposed more than one catalog generation");
		if (!actual_relations.empty()) {
			actual_relations += ',';
		}
		actual_relations += result->GetValue(1, row).ToString();
	}
	Require(actual_relations == expected_relations, "inventory did not match one complete catalog generation");
}

void RequireEmptyInventory(duckdb::Connection &connection) {
	auto result = Query(connection, "FROM rfc0012_generation_inventory()");
	Require(result->RowCount() == 0, "empty generation exposed a partially published inventory");
}

void RequirePublication(duckdb::Connection &connection, std::uint64_t generation, bool changed,
                        const std::string &spelling = "CALL") {
	const auto invocation = "rfc0012_publish_generation(generation := " + std::to_string(generation) + ")";
	auto result = Query(connection, spelling == "CALL" ? "CALL " + invocation : "FROM " + invocation);
	Require(result->RowCount() == 1 && result->ColumnCount() == 2 &&
	            result->GetValue(0, 0).GetValue<std::uint64_t>() == generation &&
	            result->GetValue(1, 0).GetValue<bool>() == changed,
	        "publication returned the wrong materialized management result");
}

void RequirePublicationWaiter(const std::shared_ptr<CoordinatorTrialControl> &control) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (control->WaitingPublicationCount() == 0 && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	Require(control->WaitingPublicationCount() > 0, "publication did not enter the cancelable wait queue");
}

void RequireLiveGenerations(const std::shared_ptr<CoordinatorTrialControl> &control, std::uint64_t expected,
                            const std::string &message) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (control->LiveGenerationCount() != expected && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	Require(control->LiveGenerationCount() == expected, message);
}

void TestAtomicFailureAndCallSurface() {
	duckdb::DuckDB database(nullptr);
	auto control = RegisterNativeCoordinatorTrial(database);
	duckdb::Connection connection(database);

	RequirePublication(connection, 1, true);
	RequireRelation(connection, "rfc0012_relation_a", 1);
	RequireInventory(connection, 1, "rfc0012_relation_a,rfc0012_relation_b,rfc0012_relation_old");
	RequirePublication(connection, 1, false, "FROM");

	for (std::int64_t boundary = 1; boundary <= 10; boundary++) {
		RequireErrorContains(
		    connection,
		    "CALL rfc0012_publish_generation(generation := 2, fail_after := " + std::to_string(boundary) + ")",
		    "injected late catalog failure");
		RequireRelation(connection, "rfc0012_relation_a", 1);
		RequireRelation(connection, "rfc0012_relation_old", 1);
		RequireErrorContains(connection, "FROM rfc0012_relation_c()", "does not exist");
		RequireInventory(connection, 1, "rfc0012_relation_a,rfc0012_relation_b,rfc0012_relation_old");
		Require(control->LastCandidateReleased(), "failed publication retained its staged generation");
	}

	RequireErrorContains(connection, "CALL rfc0012_publish_generation(generation := 2, interrupt_after := 3)",
	                     "Interrupted");
	RequireRelation(connection, "rfc0012_relation_a", 1);
	RequireRelation(connection, "rfc0012_relation_old", 1);
	RequireInventory(connection, 1, "rfc0012_relation_a,rfc0012_relation_b,rfc0012_relation_old");
	Require(control->LastCandidateReleased(), "interrupted publication retained its staged generation");
}

void TestInitialFailureIsAtomic() {
	duckdb::DuckDB database(nullptr);
	auto control = RegisterNativeCoordinatorTrial(database);
	duckdb::Connection connection(database);

	for (std::int64_t boundary = 1; boundary <= 7; boundary++) {
		RequireErrorContains(
		    connection,
		    "CALL rfc0012_publish_generation(generation := 1, fail_after := " + std::to_string(boundary) + ")",
		    "injected late catalog failure");
		RequireErrorContains(connection, "FROM rfc0012_relation_a()", "does not exist");
		RequireErrorContains(connection, "FROM rfc0012_relation_b()", "does not exist");
		RequireErrorContains(connection, "FROM rfc0012_relation_old()", "does not exist");
		RequireEmptyInventory(connection);
		Require(control->LastCandidateReleased(), "failed initial load retained its staged generation");
	}
}

void TestTransactionAndPreparedSnapshots() {
	duckdb::DuckDB database(nullptr);
	auto control = RegisterNativeCoordinatorTrial(database);
	duckdb::Connection publisher(database);
	duckdb::Connection old_snapshot(database);
	duckdb::Connection prepared(database);

	RequirePublication(publisher, 1, true);
	Query(old_snapshot, "BEGIN TRANSACTION");
	RequireRelation(old_snapshot, "rfc0012_relation_a", 1);
	Query(prepared, "PREPARE rfc0012_bound AS SELECT generation, relation, input "
	                "FROM rfc0012_relation_a(input := 41)");
	Query(prepared, "PREPARE rfc0012_deferred AS SELECT generation, relation, input "
	                "FROM rfc0012_relation_a(input := $1)");
	Query(prepared, "PREPARE rfc0012_external_parameter AS SELECT generation, relation, input "
	                "FROM rfc0012_relation_a(input := 43) WHERE $1 = 1");

	RequirePublication(publisher, 2, true);
	RequireRelation(publisher, "rfc0012_relation_c", 2);
	RequireErrorContains(publisher, "FROM rfc0012_relation_old()", "does not exist");
	RequireInventory(publisher, 2, "rfc0012_relation_a,rfc0012_relation_b,rfc0012_relation_c");

	RequireRelation(old_snapshot, "rfc0012_relation_a", 1);
	RequireRelation(old_snapshot, "rfc0012_relation_old", 1);
	RequireErrorContains(old_snapshot, "FROM rfc0012_relation_c()", "does not exist");
	RequireInventory(old_snapshot, 1, "rfc0012_relation_a,rfc0012_relation_b,rfc0012_relation_old");

	auto bound = Query(prepared, "EXECUTE rfc0012_bound");
	Require(bound->GetValue(0, 0).GetValue<std::uint64_t>() == 1 &&
	            bound->GetValue(2, 0).GetValue<std::int64_t>() == 41,
	        "fully bound prepared relation did not retain its generation");
	auto deferred = Query(prepared, "EXECUTE rfc0012_deferred(42)");
	Require(deferred->GetValue(0, 0).GetValue<std::uint64_t>() == 2 &&
	            deferred->GetValue(2, 0).GetValue<std::int64_t>() == 42,
	        "deferred table-function argument did not bind against the execution generation");
	auto external = Query(prepared, "EXECUTE rfc0012_external_parameter(1)");
	Require(external->GetValue(0, 0).GetValue<std::uint64_t>() == 1 &&
	            external->GetValue(2, 0).GetValue<std::int64_t>() == 43,
	        "parameter outside the relation function reselected its fully bound generation");
	RequirePublication(publisher, 3, true);
	auto deferred_again = Query(prepared, "EXECUTE rfc0012_deferred(44)");
	Require(deferred_again->GetValue(0, 0).GetValue<std::uint64_t>() == 3 &&
	            deferred_again->GetValue(2, 0).GetValue<std::int64_t>() == 44,
	        "deferred table-function argument did not rebind across a second publication");
	Query(old_snapshot, "ROLLBACK");
	Query(prepared, "DEALLOCATE rfc0012_bound");
	Query(prepared, "DEALLOCATE rfc0012_deferred");
	Query(prepared, "DEALLOCATE rfc0012_external_parameter");
	RequireLiveGenerations(control, 1, "ended transaction or deallocated plan retained an old generation");
}

void TestManagementRepetitionRules() {
	duckdb::DuckDB database(nullptr);
	auto control = RegisterNativeCoordinatorTrial(database);
	duckdb::Connection connection(database);
	RequirePublication(connection, 1, true);

	auto pruned = Query(connection, "SELECT * FROM rfc0012_publish_generation(generation := 2) WHERE FALSE");
	Require(pruned->RowCount() == 0, "a pruned management scan unexpectedly returned a row");
	RequireRelation(connection, "rfc0012_relation_a", 1);
	Require(control->LiveGenerationCount() == 1, "a pruned management scan executed an unrequested publication effect");

	Query(connection, "BEGIN TRANSACTION");
	RequireErrorContains(connection, "CALL rfc0012_publish_generation(generation := 2)", "autocommit");
	Query(connection, "ROLLBACK");
	RequireErrorContains(connection,
	                     "SELECT * FROM rfc0012_publish_generation(generation := 2), "
	                     "rfc0012_publish_generation(generation := 3)",
	                     "one lifecycle invocation");

	Query(connection, "PREPARE rfc0012_publish AS "
	                  "SELECT * FROM rfc0012_publish_generation(generation := $1)");
	auto changed = Query(connection, "EXECUTE rfc0012_publish(2)");
	Require(changed->GetValue(0, 0).GetValue<std::uint64_t>() == 2 && changed->GetValue(1, 0).GetValue<bool>(),
	        "prepared publication did not execute once against the current generation");
	auto unchanged = Query(connection, "EXECUTE rfc0012_publish(2)");
	Require(!unchanged->GetValue(1, 0).GetValue<bool>(),
	        "repeated prepared publication did not rebind to the current generation");
}

void TestUnrelatedSystemCollisionIsAtomic() {
	duckdb::DuckDB database(nullptr);
	RegisterNativeCoordinatorTrial(database);
	duckdb::Connection connection(database);
	RequirePublication(connection, 1, true);
	RegisterUnrelatedSystemFunction(database, "rfc0012_relation_c");

	RequireErrorContains(connection, "CALL rfc0012_publish_generation(generation := 2)", "conflicts");
	RequireRelation(connection, "rfc0012_relation_a", 1);
	RequireRelation(connection, "rfc0012_relation_old", 1);
	RequireInventory(connection, 1, "rfc0012_relation_a,rfc0012_relation_b,rfc0012_relation_old");
	auto unrelated = Query(connection, "FROM system.main.rfc0012_relation_c()");
	Require(unrelated->GetValue(0, 0).GetValue<std::uint64_t>() == 999,
	        "collision handling replaced or merged the unrelated table function");
}

void TestLateSystemOverloadCannotInterleave() {
	duckdb::DuckDB database(nullptr);
	auto control = RegisterNativeCoordinatorTrial(database);
	duckdb::Connection publisher(database);
	duckdb::Connection observer(database);
	RequirePublication(observer, 1, true);

	control->PauseNextPublicationAfterPreflight();
	std::string publication_error;
	std::thread publication([&]() {
		auto result = publisher.Query("CALL rfc0012_publish_generation(generation := 2)");
		if (result->HasError()) {
			publication_error = result->GetError();
		}
	});
	const auto paused = control->WaitForPublicationPause(std::chrono::seconds(5));
	if (!paused) {
		publication.join();
		Require(false, "publication did not reach its post-ownership preflight boundary: " + publication_error);
	}
	std::string registration_error;
	try {
		RegisterUnrelatedSystemOverload(database, "rfc0012_relation_a");
	} catch (const std::exception &error) {
		registration_error = error.what();
	}
	control->ResumePublication();
	publication.join();
	Require(registration_error.find("write-write conflict") != std::string::npos,
	        "real system transaction did not reject the late external overload: " + registration_error);
	Require(publication_error.empty(), "rejected late overload damaged publication: " + publication_error);
	RequireRelation(observer, "rfc0012_relation_a", 2);
	RequireRelation(observer, "rfc0012_relation_c", 2);
	RequireInventory(observer, 2, "rfc0012_relation_a,rfc0012_relation_b,rfc0012_relation_c");
}

void TestMacroPreflightAndLateShadow() {
	duckdb::DuckDB database(nullptr);
	auto control = RegisterNativeCoordinatorTrial(database);
	duckdb::Connection publisher(database);
	duckdb::Connection ddl(database);
	RequirePublication(publisher, 1, true);

	Query(ddl, "CREATE MACRO rfc0012_relation_c(input := NULL) AS TABLE "
	           "SELECT 700::UBIGINT AS generation, 'macro'::VARCHAR AS relation, input::BIGINT AS input");
	RequireErrorContains(publisher, "CALL rfc0012_publish_generation(generation := 2)", "table macro");
	Query(ddl, "DROP MACRO rfc0012_relation_c");

	control->PauseNextPublicationAfterPreflight();
	std::string publication_error;
	std::thread publication([&]() {
		auto result = publisher.Query("CALL rfc0012_publish_generation(generation := 2)");
		if (result->HasError()) {
			publication_error = result->GetError();
		}
	});
	Require(control->WaitForPublicationPause(std::chrono::seconds(5)),
	        "publication did not reach the controlled post-preflight boundary");
	Query(ddl, "CREATE MACRO rfc0012_relation_c(input := NULL) AS TABLE "
	           "SELECT 701::UBIGINT AS generation, 'late_macro'::VARCHAR AS relation, input::BIGINT AS input");
	control->ResumePublication();
	publication.join();
	Require(publication_error.empty(), "late user macro prevented canonical system publication: " + publication_error);

	auto shadow = Query(ddl, "FROM rfc0012_relation_c(input := 9)");
	Require(shadow->GetValue(0, 0).GetValue<std::uint64_t>() == 701 &&
	            shadow->GetValue(1, 0).ToString() == "late_macro",
	        "unqualified name did not follow DuckDB's later user-macro shadowing rule");
	RequireRelation(ddl, "rfc0012_relation_c", 2, 9, "system.main");
	RequireInventory(ddl, 2, "rfc0012_relation_a,rfc0012_relation_b,rfc0012_relation_c");
}

void TestConcurrentPublishersNeverMixGenerations() {
	duckdb::DuckDB database(nullptr);
	auto control = RegisterNativeCoordinatorTrial(database);
	duckdb::Connection first(database);
	duckdb::Connection second(database);
	duckdb::Connection observer(database);
	RequirePublication(observer, 1, true);

	control->PauseNextPublicationAfterPreflight();
	std::string first_error;
	std::string second_error;
	std::thread first_thread([&]() {
		auto result = first.Query("CALL rfc0012_publish_generation(generation := 2)");
		if (result->HasError()) {
			first_error = result->GetError();
		}
	});
	Require(control->WaitForPublicationPause(std::chrono::seconds(5)),
	        "first publisher did not reach the controlled boundary");
	std::thread second_thread([&]() {
		auto result = second.Query("CALL rfc0012_publish_generation(generation := 3)");
		if (result->HasError()) {
			second_error = result->GetError();
		}
	});
	RequirePublicationWaiter(control);
	control->ResumePublication();
	first_thread.join();
	second_thread.join();

	Require(first_error.empty(), "serialized first publication failed: " + first_error);
	Require(!second_error.empty(), "stale concurrent publisher unexpectedly overwrote the committed generation");
	RequireRelation(observer, "rfc0012_relation_a", 2);
	RequireRelation(observer, "rfc0012_relation_c", 2);
	RequireErrorContains(observer, "FROM rfc0012_relation_old()", "does not exist");
	RequireInventory(observer, 2, "rfc0012_relation_a,rfc0012_relation_b,rfc0012_relation_c");
}

void TestUncommittedPublicationIsInvisible() {
	duckdb::DuckDB database(nullptr);
	auto control = RegisterNativeCoordinatorTrial(database);
	duckdb::Connection publisher(database);
	duckdb::Connection observer(database);
	RequirePublication(observer, 1, true);

	control->PauseNextPublicationAfterMutation(9);
	std::string publication_error;
	std::thread publication([&]() {
		auto result = publisher.Query("CALL rfc0012_publish_generation(generation := 2)");
		if (result->HasError()) {
			publication_error = result->GetError();
		}
	});
	Require(control->WaitForPublicationPause(std::chrono::seconds(5)),
	        "publication did not pause after a late catalog mutation");
	RequireRelation(observer, "rfc0012_relation_a", 1);
	RequireRelation(observer, "rfc0012_relation_old", 1);
	RequireErrorContains(observer, "FROM rfc0012_relation_c()", "does not exist");
	RequireInventory(observer, 1, "rfc0012_relation_a,rfc0012_relation_b,rfc0012_relation_old");
	control->ResumePublication();
	publication.join();
	Require(publication_error.empty(), "late-paused publication failed: " + publication_error);
	RequireRelation(observer, "rfc0012_relation_a", 2);
	RequireRelation(observer, "rfc0012_relation_c", 2);
	RequireInventory(observer, 2, "rfc0012_relation_a,rfc0012_relation_b,rfc0012_relation_c");
}

void TestWaitingPublicationCanBeInterrupted() {
	duckdb::DuckDB database(nullptr);
	auto control = RegisterNativeCoordinatorTrial(database);
	duckdb::Connection first(database);
	duckdb::Connection waiting(database);
	duckdb::Connection observer(database);
	RequirePublication(observer, 1, true);

	control->PauseNextPublicationAfterPreflight();
	std::string first_error;
	std::string waiting_error;
	std::thread first_thread([&]() {
		auto result = first.Query("CALL rfc0012_publish_generation(generation := 2)");
		if (result->HasError()) {
			first_error = result->GetError();
		}
	});
	Require(control->WaitForPublicationPause(std::chrono::seconds(5)),
	        "first publication did not reach the controlled boundary");
	std::thread waiting_thread([&]() {
		auto result = waiting.Query("CALL rfc0012_publish_generation(generation := 3)");
		if (result->HasError()) {
			waiting_error = result->GetError();
		}
	});
	RequirePublicationWaiter(control);
	waiting.Interrupt();
	waiting_thread.join();
	Require(waiting_error.find("Interrupted") != std::string::npos,
	        "interrupted publication remained blocked or lost its cancellation: " + waiting_error);
	control->ResumePublication();
	first_thread.join();
	Require(first_error.empty(), "lock-owning publication failed after waiter cancellation: " + first_error);
	RequireRelation(observer, "rfc0012_relation_a", 2);
}

void TestCoordinatorCloseDrainsOwnerAndRejectsWaiter() {
	std::shared_ptr<CoordinatorTrialControl> control;
	{
		duckdb::DuckDB database(nullptr);
		control = RegisterNativeCoordinatorTrial(database);
		duckdb::Connection first(database);
		duckdb::Connection waiting(database);
		duckdb::Connection observer(database);
		RequirePublication(observer, 1, true);

		control->PauseNextPublicationAfterPreflight();
		std::string first_error;
		std::string waiting_error;
		std::thread first_thread([&]() {
			auto result = first.Query("CALL rfc0012_publish_generation(generation := 2)");
			if (result->HasError()) {
				first_error = result->GetError();
			}
		});
		Require(control->WaitForPublicationPause(std::chrono::seconds(5)),
		        "shutdown owner did not reach the controlled boundary");
		std::thread waiting_thread([&]() {
			auto result = waiting.Query("CALL rfc0012_publish_generation(generation := 3)");
			if (result->HasError()) {
				waiting_error = result->GetError();
			}
		});
		RequirePublicationWaiter(control);
		control->BeginShutdown();
		waiting_thread.join();
		Require(waiting_error.find("closing") != std::string::npos,
		        "shutdown did not reject the queued publication: " + waiting_error);
		control->ResumePublication();
		first_thread.join();
		Require(first_error.empty(), "shutdown did not drain the lock-owning publication: " + first_error);
		RequireRelation(observer, "rfc0012_relation_a", 2);
		RequireErrorContains(observer, "CALL rfc0012_publish_generation(generation := 3)", "closing");
	}
	Require(control->LiveGenerationCount() == 0, "shutdown drain retained a registry generation");
}

void TestInFlightScanRetainsOnlyItsGeneration() {
	std::shared_ptr<CoordinatorTrialControl> control;
	{
		duckdb::DuckDB database(nullptr);
		control = RegisterNativeCoordinatorTrial(database);
		duckdb::Connection reader(database);
		duckdb::Connection publisher(database);
		duckdb::Connection observer(database);
		RequirePublication(observer, 1, true);

		control->PauseNextRelationScan();
		std::string reader_error;
		std::uint64_t reader_generation = 0;
		std::thread reader_thread([&]() {
			auto result = reader.Query("SELECT generation FROM rfc0012_relation_a(input := 1)");
			if (result->HasError()) {
				reader_error = result->GetError();
			} else {
				reader_generation = result->GetValue(0, 0).GetValue<std::uint64_t>();
			}
		});
		const auto paused = control->WaitForRelationScanPause(std::chrono::seconds(5));
		if (!paused) {
			reader_thread.join();
			Require(false, "relation scan did not reach the controlled execution boundary: " + reader_error);
		}

		std::atomic<bool> publication_done {false};
		std::string publication_error;
		std::thread publication_thread([&]() {
			auto result = publisher.Query("CALL rfc0012_publish_generation(generation := 2)");
			if (result->HasError()) {
				publication_error = result->GetError();
			}
			publication_done.store(true, std::memory_order_release);
		});
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!publication_done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		if (!publication_done.load(std::memory_order_acquire)) {
			control->ResumeRelationScan();
			reader_thread.join();
			publication_thread.join();
			Require(false, "in-flight scan blocked independent package publication");
		}
		publication_thread.join();
		Require(publication_error.empty(), "publication failed beside an in-flight scan: " + publication_error);
		Require(control->LiveGenerationCount() >= 2, "in-flight scan did not retain its old immutable generation");
		RequireRelation(observer, "rfc0012_relation_a", 2);

		control->ResumeRelationScan();
		reader_thread.join();
		Require(reader_error.empty() && reader_generation == 1,
		        "in-flight scan changed generation across publication: " + reader_error);
		RequireLiveGenerations(control, 1, "completed scan retained its old immutable generation");
	}
	Require(control->LiveGenerationCount() == 0, "scan lifecycle shutdown retained a generation");
}

void TestShutdownReleasesEveryGeneration() {
	std::shared_ptr<CoordinatorTrialControl> control;
	{
		duckdb::DuckDB database(nullptr);
		control = RegisterNativeCoordinatorTrial(database);
		duckdb::Connection connection(database);
		RequirePublication(connection, 1, true);
		Query(connection, "PREPARE rfc0012_lifetime AS SELECT * FROM rfc0012_relation_a(input := 1)");
		RequirePublication(connection, 2, true);
		Require(control->LiveGenerationCount() >= 2,
		        "prepared plan did not retain its immutable old generation during database life");
		Query(connection, "DEALLOCATE rfc0012_lifetime");
		Require(control->LiveGenerationCount() == 1, "deallocated prepared plan retained its immutable old generation");
	}
	Require(control->DatabaseShutdownObserved(),
	        "DatabaseInstance-owned lifecycle hook did not close the publication coordinator");
	Require(control->LiveGenerationCount() == 0, "database shutdown retained a registry generation");
}

} // namespace

int main() {
	try {
		TestInitialFailureIsAtomic();
		TestAtomicFailureAndCallSurface();
		TestTransactionAndPreparedSnapshots();
		TestManagementRepetitionRules();
		TestUnrelatedSystemCollisionIsAtomic();
		TestLateSystemOverloadCannotInterleave();
		TestMacroPreflightAndLateShadow();
		TestConcurrentPublishersNeverMixGenerations();
		TestUncommittedPublicationIsInvisible();
		TestWaitingPublicationCanBeInterrupted();
		TestCoordinatorCloseDrainsOwnerAndRejectsWaiter();
		TestInFlightScanRetainsOnlyItsGeneration();
		TestShutdownReleasesEveryGeneration();
		std::cout << "RFC 0012 native coordinator trial passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "RFC 0012 native coordinator trial failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
