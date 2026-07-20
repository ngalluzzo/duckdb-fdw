#include "table_function_plan_state.hpp"
#include "table_function_bind_data.hpp"

#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_planner.hpp"
#include "query/support/query_runtime_scenarios.hpp"
#include "support/require.hpp"

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace duckdb_api_test {
namespace {

using duckdb_api_test::Require;

duckdb_api::ScanRequest RepositoryBaselineRequest(const duckdb_api::CompiledConnector &connector) {
	return duckdb_api::BuildConservativeScanRequest(connector, "authenticated_repositories",
	                                                duckdb_api::LogicalSecretReference::Named("offline_secret"));
}

duckdb_api::RequestedPredicate VisibilityPrivateCandidate() {
	return duckdb_api::RequestedPredicate::Comparison(5, duckdb_api::RequestedPredicateValueKind::VARCHAR,
	                                                  duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
	                                                  duckdb_api::RequestedPredicateValue::Varchar("private"));
}

void TestBaselineRetentionAndIndependentCopy() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	auto baseline_request = RepositoryBaselineRequest(connector);
	auto baseline_plan = duckdb_api::BuildConservativeScanPlan(connector, baseline_request);
	duckdb_api::query_internal::TableFunctionPlanState state(std::move(baseline_request), std::move(baseline_plan));
	const duckdb_api::query_internal::TableFunctionPlanState copy(state);

	Require(&state.BaselineRequest() != &copy.BaselineRequest() &&
	            &state.SelectedRequest() != &copy.SelectedRequest() && &state.SelectedPlan() != &copy.SelectedPlan(),
	        "bind-state copy shared request or selected-plan storage");
	Require(state.BaselineRequest().Snapshot() == copy.BaselineRequest().Snapshot() &&
	            state.SelectedPlan().Snapshot() == copy.SelectedPlan().Snapshot(),
	        "bind-state copy changed baseline request or selected plan value");

	auto candidate = state.BaselineRequest();
	candidate.requested_predicate = VisibilityPrivateCandidate();
	candidate.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
	candidate.capabilities.selective_predicate = true;
	candidate.capabilities.retains_predicate = true;
	auto candidate_plan = duckdb_api::BuildConservativeScanPlan(connector, candidate);
	state.ReplaceSelected(std::move(candidate), std::move(candidate_plan));

	Require(state.SelectedPlan().RemotePredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            state.SelectedPlan().RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::SUPERSET &&
	            state.SelectedPlan().ResidualPredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            state.SelectedPlan().ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB,
	        "selected state did not consume the complete selective Semantics plan");
	Require(copy.SelectedPlan().RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            copy.BaselineRequest().requested_predicate == duckdb_api::RequestedPredicate::Unrestricted(),
	        "selected-plan replacement leaked into an independently copied bind state");
	Require(state.BaselineRequest().requested_predicate == duckdb_api::RequestedPredicate::Unrestricted() &&
	            state.BaselineRequest().capabilities.HasConservativeRelationalProfile(),
	        "selected-plan replacement mutated the retained credential-free baseline request");

	const duckdb_api::query_internal::TableFunctionPlanState refined_copy(state);
	auto replacement_request = state.BaselineRequest();
	auto replacement_plan = duckdb_api::BuildConservativeScanPlan(connector, replacement_request);
	state.ReplaceSelected(std::move(replacement_request), std::move(replacement_plan));
	Require(refined_copy.SelectedPlan().RemotePredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            state.SelectedPlan().RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN,
	        "copying after refinement shared later selected-plan replacement");

	auto invalid = state.BaselineRequest();
	invalid.connector_name = "wrong-connector";
	bool failed = false;
	try {
		auto invalid_plan = duckdb_api::BuildConservativeScanPlan(connector, invalid);
		state.ReplaceSelected(std::move(invalid), std::move(invalid_plan));
	} catch (const std::logic_error &) {
		failed = true;
	}
	Require(failed && state.SelectedPlan().RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN,
	        "failed refinement changed the previously frozen selected plan");

	auto final_request = state.BaselineRequest();
	auto final_plan = duckdb_api::BuildConservativeScanPlan(connector, final_request);
	state.ReplaceSelected(std::move(final_request), std::move(final_plan));
	Require(state.SelectedPlan().RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN,
	        "replanning from the baseline request retained stale selective state");
}

void TestDuckdbBindCopiesRefineConcurrentlyFromOneDestroyedAncestor() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	auto baseline_request = RepositoryBaselineRequest(connector);
	auto baseline_plan = duckdb_api::BuildConservativeScanPlan(connector, baseline_request);
	const auto executor =
	    BuildQueryScenarioExecutor(QueryRuntimeScenario::SUCCESS, std::make_shared<QueryLifecycleProbe>());

	// Exercise the actual DuckDB FunctionData::Copy boundary. The ancestor is
	// destroyed before either child replans, so borrowed or shallow-owned plan
	// state cannot survive this test by accident.
	auto copies = [&]() {
		duckdb::duckdb_api_query_internal::DuckdbApiBindData ancestor(std::move(baseline_request),
		                                                              std::move(baseline_plan), executor);
		return std::make_pair(ancestor.Copy(), ancestor.Copy());
	}();
	auto &private_copy = static_cast<duckdb::duckdb_api_query_internal::DuckdbApiBindData &>(*copies.first);
	auto &fallback_copy = static_cast<duckdb::duckdb_api_query_internal::DuckdbApiBindData &>(*copies.second);

	Require(&private_copy.plan_state != &fallback_copy.plan_state &&
	            &private_copy.plan_state.SelectedPlan() != &fallback_copy.plan_state.SelectedPlan(),
	        "DuckDB FunctionData::Copy shared mutable plan selection state");
	Require(private_copy.executor.get() == executor.get() && fallback_copy.executor.get() == executor.get(),
	        "DuckDB FunctionData::Copy failed to share only the immutable executor service");

	std::mutex barrier_mutex;
	std::condition_variable barrier_condition;
	unsigned arrived = 0;
	bool released = false;
	auto synchronize = [&]() {
		std::unique_lock<std::mutex> guard(barrier_mutex);
		arrived++;
		if (arrived == 2) {
			released = true;
			barrier_condition.notify_all();
			return;
		}
		barrier_condition.wait(guard, [&]() { return released; });
	};

	std::exception_ptr private_failure;
	std::exception_ptr fallback_failure;
	std::thread private_thread([&]() {
		try {
			synchronize();
			auto request = private_copy.plan_state.BaselineRequest();
			request.requested_predicate = VisibilityPrivateCandidate();
			request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
			request.capabilities.selective_predicate = true;
			request.capabilities.retains_predicate = true;
			auto plan = duckdb_api::BuildConservativeScanPlan(connector, request);
			private_copy.plan_state.ReplaceSelected(std::move(request), std::move(plan));
		} catch (...) {
			private_failure = std::current_exception();
		}
	});
	std::thread fallback_thread([&]() {
		try {
			synchronize();
			auto request = fallback_copy.plan_state.BaselineRequest();
			request.requested_predicate = duckdb_api::RequestedPredicate::Unsupported(0);
			request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER;
			request.capabilities.selective_predicate = true;
			request.capabilities.retains_predicate = true;
			auto plan = duckdb_api::BuildConservativeScanPlan(connector, request);
			fallback_copy.plan_state.ReplaceSelected(std::move(request), std::move(plan));
		} catch (...) {
			fallback_failure = std::current_exception();
		}
	});
	private_thread.join();
	fallback_thread.join();
	if (private_failure) {
		std::rethrow_exception(private_failure);
	}
	if (fallback_failure) {
		std::rethrow_exception(fallback_failure);
	}

	Require(private_copy.plan_state.SelectedPlan().RemotePredicate() ==
	                duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            private_copy.plan_state.SelectedPlan().ConditionalInput() ==
	                duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE &&
	            private_copy.plan_state.SelectedPlan().ResidualPredicate() ==
	                duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE,
	        "private FunctionData copy did not retain its selective plan");
	Require(fallback_copy.plan_state.SelectedPlan().RemotePredicate() ==
	                duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            fallback_copy.plan_state.SelectedPlan().ConditionalInput() ==
	                duckdb_api::PlannedConditionalInput::NONE &&
	            fallback_copy.plan_state.SelectedPlan().ResidualPredicate() ==
	                duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER &&
	            fallback_copy.plan_state.SelectedPlan().ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB,
	        "fallback FunctionData copy did not retain its complete DuckDB residual plan");
	Require(private_copy.plan_state.BaselineRequest().requested_predicate ==
	                duckdb_api::RequestedPredicate::Unrestricted() &&
	            fallback_copy.plan_state.BaselineRequest().requested_predicate ==
	                duckdb_api::RequestedPredicate::Unrestricted(),
	        "concurrent FunctionData refinement mutated a child baseline request");
}

} // namespace

void RunTableFunctionPlanStateTests() {
	TestBaselineRetentionAndIndependentCopy();
	TestDuckdbBindCopiesRefineConcurrentlyFromOneDestroyedAncestor();
}

} // namespace duckdb_api_test

static_assert(
    std::is_same<decltype(std::declval<const duckdb_api::query_internal::TableFunctionPlanState &>().SelectedPlan()),
                 const duckdb_api::ScanPlan &>::value,
    "execution must observe the selected plan through a frozen const handoff");
