#include "semantics/support/runtime_rest_predicate_plan_test_fixtures.hpp"

#include "connector/support/package_generation_test_fixtures.hpp"
#include "duckdb_api/scan_planner.hpp"
#include "semantics/support/scan_plan_test_access.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace duckdb_api_test {
namespace {

const char RUNTIME_EXACT_REST_PREDICATE_RELATION[] = "bigint_predicates";
const char RUNTIME_EXACT_DOUBLE_REST_PREDICATE_RELATION[] = "double_predicates";

duckdb_api::ScanRequest RuntimeRestPredicateRequest(const duckdb_api::CompiledConnector &connector,
                                                    const std::string &relation_name, bool selective_predicate) {
	auto request =
	    duckdb_api::BuildConservativeScanRequest(connector, relation_name, duckdb_api::LogicalSecretReference());
	request.requested_predicate = duckdb_api::RequestedPredicate::Comparison(
	    1, duckdb_api::RequestedPredicateValueKind::BIGINT, duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
	    duckdb_api::RequestedPredicateValue::BigInt(42));
	request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
	request.capabilities.selective_predicate = selective_predicate;
	request.capabilities.retains_predicate = true;
	return request;
}

duckdb_api::ScanPlan BuildRuntimeRestPredicatePlan(const duckdb_api::CompiledPackageGeneration &generation,
                                                   const std::string &relation_name, bool selective_predicate) {
	return duckdb_api::BuildConservativeScanPlan(
	    generation.Connector(),
	    RuntimeRestPredicateRequest(generation.Connector(), relation_name, selective_predicate));
}

duckdb_api::ScanRequest RuntimeDoubleRestPredicateRequest(const duckdb_api::CompiledConnector &connector,
                                                          const std::string &relation_name) {
	auto request =
	    duckdb_api::BuildConservativeScanRequest(connector, relation_name, duckdb_api::LogicalSecretReference());
	request.requested_predicate = duckdb_api::RequestedPredicate::Comparison(
	    1, duckdb_api::RequestedPredicateValueKind::DOUBLE, duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
	    duckdb_api::RequestedPredicateValue::Double(3.5));
	request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
	request.capabilities.selective_predicate = true;
	request.capabilities.retains_predicate = true;
	return request;
}

std::size_t ConditionalBindingIndex(const duckdb_api::PlannedRestOperation &operation) {
	for (std::size_t index = 0; index < operation.query_bindings.size(); index++) {
		if (operation.query_bindings[index].Source() == duckdb_api::PlannedRestQueryValueSource::CONDITIONAL_INPUT) {
			return index;
		}
	}
	throw std::logic_error("runtime REST predicate fixture lost its conditional binding");
}

} // namespace

duckdb_api::ScanPlan BuildRuntimeExactRestPredicatePlanFixture() {
	const auto generation = BuildTypedPredicatePackageGenerationFixture();
	return BuildRuntimeRestPredicatePlan(generation, RUNTIME_EXACT_REST_PREDICATE_RELATION, true);
}

duckdb_api::ScanPlan BuildRuntimeExactDoubleRestPredicatePlanFixture() {
	const auto generation = BuildTypedPredicatePackageGenerationFixture();
	return duckdb_api::BuildConservativeScanPlan(
	    generation.Connector(),
	    RuntimeDoubleRestPredicateRequest(generation.Connector(), RUNTIME_EXACT_DOUBLE_REST_PREDICATE_RELATION));
}

duckdb_api::ScanPlan BuildRuntimeResidualOnlyRestPredicatePlanFixture() {
	const auto generation = BuildResidualPredicatePackageGenerationFixture();
	return BuildRuntimeRestPredicatePlan(generation, PACKAGE_RESIDUAL_PREDICATE_RELATION, false);
}

duckdb_api::ScanPlan BuildRuntimeNativePredicateIsolationPlanFixture() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto *relation = connector.FindRelation("authenticated_repositories");
	if (relation == nullptr) {
		throw std::logic_error("native runtime isolation fixture lost its repository relation");
	}
	auto request = duckdb_api::BuildConservativeScanRequest(
	    connector, relation->Name(), duckdb_api::LogicalSecretReference::Named("runtime_isolation_secret"));
	request.requested_predicate = duckdb_api::RequestedPredicate::Comparison(
	    5, duckdb_api::RequestedPredicateValueKind::VARCHAR, duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
	    duckdb_api::RequestedPredicateValue::Varchar("private"));
	request.retained_predicate_scope = duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE;
	request.capabilities.selective_predicate = true;
	request.capabilities.retains_predicate = true;
	return duckdb_api::BuildConservativeScanPlan(connector, request);
}

duckdb_api::ScanPlan
BuildRuntimeRestPredicatePlanCounterexample(RuntimeRestPredicatePlanCounterexample counterexample) {
	return ScanPlanTestAccess::RuntimeRestPredicate(BuildRuntimeExactRestPredicatePlanFixture(), counterexample);
}

duckdb_api::ScanPlan BuildRuntimeRestSchemaCounterexample(RuntimeRestSchemaCounterexample counterexample) {
	const auto generation = BuildPackageCompatibilityFixture(PackageCompatibilityFixture::ARRAY_BASELINE);
	auto request = duckdb_api::BuildConservativeScanRequest(generation.Connector(), PACKAGE_TYPED_RELATION,
	                                                        duckdb_api::LogicalSecretReference());
	return ScanPlanTestAccess::RuntimeRestSchema(duckdb_api::BuildConservativeScanPlan(generation.Connector(), request),
	                                             counterexample);
}

duckdb_api::ScanPlan ScanPlanTestAccess::RuntimeRestPredicate(duckdb_api::ScanPlan plan,
                                                              RuntimeRestPredicatePlanCounterexample counterexample) {
	auto operation = plan.Operation().Rest();
	const auto index = ConditionalBindingIndex(operation);
	auto &binding = operation.query_bindings[index];
	switch (counterexample) {
	case RuntimeRestPredicatePlanCounterexample::CONDITIONAL_SOURCE_ID:
		binding.source_id = "other_rank";
		break;
	case RuntimeRestPredicatePlanCounterexample::CONDITIONAL_SCALAR_KIND:
		binding.kind = duckdb_api::PlannedRestScalarKind::VARCHAR;
		binding.bigint_value = 0;
		binding.varchar_value = "42";
		break;
	case RuntimeRestPredicatePlanCounterexample::CONDITIONAL_TYPED_VALUE:
		binding.bigint_value = 41;
		binding.encoded_value = "41";
		break;
	case RuntimeRestPredicatePlanCounterexample::NONCANONICAL_ENCODED_VALUE:
		binding.encoded_value = "0042";
		break;
	case RuntimeRestPredicatePlanCounterexample::DUPLICATE_CONDITIONAL_BINDING: {
		auto duplicate = binding;
		duplicate.name = "rank_filter_duplicate";
		operation.query_bindings.push_back(std::move(duplicate));
		break;
	}
	case RuntimeRestPredicatePlanCounterexample::COUNT:
	default:
		throw std::invalid_argument("unknown runtime REST predicate plan counterexample");
	}
	plan.operation = std::make_shared<const duckdb_api::PlannedProtocolOperation>(
	    duckdb_api::PlannedProtocolOperation::FromRest(std::move(operation)));
	return plan;
}

duckdb_api::ScanPlan ScanPlanTestAccess::RuntimeRestSchema(duckdb_api::ScanPlan plan,
                                                           RuntimeRestSchemaCounterexample counterexample) {
	auto operation = plan.Operation().Rest();
	if (operation.result_columns.size() < 2 || plan.output_columns.size() < 2) {
		throw std::logic_error("runtime REST schema fixture lost its ARRAY column");
	}
	auto &result = operation.result_columns[1];
	switch (counterexample) {
	case RuntimeRestSchemaCounterexample::RESULT_NAME:
		result.name = "other_label";
		break;
	case RuntimeRestSchemaCounterexample::RESULT_SHAPE:
		result.shape = duckdb_api::PlannedResultShape::SCALAR;
		break;
	case RuntimeRestSchemaCounterexample::RESULT_ELEMENT_KIND:
		result.scalar_kind = duckdb_api::PlannedRestScalarKind::BIGINT;
		break;
	case RuntimeRestSchemaCounterexample::RESULT_ELEMENT_NULLABILITY:
		result.element_nullable = !result.element_nullable;
		break;
	case RuntimeRestSchemaCounterexample::RESULT_OUTER_NULLABILITY:
		result.nullable = !result.nullable;
		break;
	case RuntimeRestSchemaCounterexample::RESULT_PATH:
		result.response_path.segments.push_back("other");
		break;
	case RuntimeRestSchemaCounterexample::RESULT_ARITY:
		operation.result_columns.clear();
		break;
	case RuntimeRestSchemaCounterexample::RESULT_ORDER:
		std::swap(operation.result_columns[0], operation.result_columns[1]);
		break;
	case RuntimeRestSchemaCounterexample::OUTPUT_NAME:
		plan.output_columns[1].name = "other_label";
		break;
	case RuntimeRestSchemaCounterexample::OUTPUT_NAME_ORDER:
		std::swap(plan.output_columns[0].name, plan.output_columns[1].name);
		break;
	case RuntimeRestSchemaCounterexample::OUTPUT_ARITY:
		plan.output_columns.pop_back();
		break;
	case RuntimeRestSchemaCounterexample::OUTPUT_SHAPE:
		plan.output_columns[1].shape = duckdb_api::PlannedColumnShape::SCALAR;
		break;
	case RuntimeRestSchemaCounterexample::COUNT:
	default:
		throw std::invalid_argument("unknown runtime REST schema counterexample");
	}
	plan.operation = std::make_shared<const duckdb_api::PlannedProtocolOperation>(
	    duckdb_api::PlannedProtocolOperation::FromRest(std::move(operation)));
	return plan;
}

} // namespace duckdb_api_test
