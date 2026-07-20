#include "duckdb_api/scan_planner.hpp"
#include "connector/support/connector_catalog_test_fixtures.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "semantics/support/scan_plan_contract_test_support.hpp"
#include "support/require.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using duckdb_api_test::Require;
using duckdb_api_test::scan_plan_contract::FindRelation;

// Equal-valued duplicates have distinct occurrence identities. The matrix also
// contains FALSE and NULL equality results plus a row that proves why a partial
// visibility candidate is unsafe for `visibility = 'private' OR archived =
// FALSE`.
const char BASE_OCCURRENCES[] = "(VALUES (101, 'private', FALSE, TRUE, TRUE), (102, 'private', FALSE, TRUE, TRUE), "
                                "(103, 'private', TRUE, TRUE, TRUE), (104, 'public', FALSE, TRUE, FALSE), "
                                "(105, NULL, FALSE, FALSE, FALSE), (106, 'internal', TRUE, FALSE, FALSE)) "
                                "AS base(occurrence_id, visibility, archived, github_visibility_private_result, "
                                "controlled_exact_visibility_private_result)";

enum class TruthValue { FALSE_VALUE, TRUE_VALUE, NULL_VALUE };

struct TruthOccurrence {
	std::int64_t occurrence_id;
	TruthValue value;

	bool operator==(const TruthOccurrence &other) const {
		return occurrence_id == other.occurrence_id && value == other.value;
	}
};

struct RemoteLaw {
	std::string truth_sql;
	std::string selected_occurrences_sql;
};

std::size_t FindColumn(const duckdb_api::CompiledRelation &relation, const std::string &name) {
	for (std::size_t index = 0; index < relation.Columns().size(); index++) {
		if (relation.Columns()[index].name == name) {
			return index;
		}
	}
	throw std::logic_error("law fixture relation is missing required column: " + name);
}

duckdb_api::RequestedPredicate VisibilityPrivate(const duckdb_api::CompiledRelation &relation) {
	return duckdb_api::RequestedPredicate::Comparison(FindColumn(relation, "visibility"),
	                                                  duckdb_api::RequestedPredicateValueKind::VARCHAR,
	                                                  duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
	                                                  duckdb_api::RequestedPredicateValue::Varchar("private"));
}

duckdb_api::ScanRequest CandidateRequest(const duckdb_api::CompiledConnector &connector,
                                         const duckdb_api::CompiledRelation &relation,
                                         duckdb_api::RequestedPredicate candidate,
                                         duckdb_api::RetainedPredicateScope retained_scope) {
	auto request = relation.Authentication().Requirement() == duckdb_api::CompiledCredentialRequirement::REQUIRED
	                   ? duckdb_api::BuildConservativeScanRequest(
	                         connector, relation.Name(), duckdb_api::LogicalSecretReference::Named("law_secret"))
	                   : duckdb_api::BuildConservativeScanRequest(connector, relation.Name(),
	                                                              duckdb_api::LogicalSecretReference());
	request.requested_predicate = std::move(candidate);
	request.retained_predicate_scope = retained_scope;
	request.capabilities.selective_predicate = true;
	request.capabilities.retains_predicate = true;
	return request;
}

const duckdb_api::CompiledOperation &SelectedOperation(const duckdb_api::CompiledRelation &relation,
                                                       const duckdb_api::ScanPlan &plan) {
	const duckdb_api::CompiledOperation *selected = nullptr;
	for (const auto &operation : relation.Operations()) {
		if (operation.name == plan.Operation().operation_name) {
			Require(selected == nullptr, "plan operation identity matched more than one Connector operation");
			selected = &operation;
		}
	}
	Require(selected != nullptr, "plan operation identity was absent from its Connector relation");
	return *selected;
}

RemoteLaw DeriveRemoteLaw(const duckdb_api::ScanPlan &plan, const duckdb_api::CompiledRelation &relation) {
	if (plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN) {
		Require(plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::UNSUPPORTED &&
		            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE,
		        "unrestricted plan carried selective accuracy or input authority");
		return {"TRUE", "TRUE"};
	}
	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE &&
	            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::VISIBILITY_PRIVATE,
	        "law oracle received an unknown selective plan shape");

	const auto &operation = SelectedOperation(relation, plan);
	const duckdb_api::CompiledPredicateMapping *selected_mapping = nullptr;
	for (const auto &mapping : relation.PredicateMappings()) {
		if (mapping.OperationName() != operation.name || mapping.ColumnName() != "visibility" ||
		    mapping.Operator() != duckdb_api::CompiledPredicateOperator::EQUALS ||
		    mapping.Literal() != duckdb_api::CompiledPredicateLiteral::VARCHAR_PRIVATE ||
		    mapping.InputPlacement() != duckdb_api::CompiledPredicateInputPlacement::REST_QUERY_PARAMETER ||
		    mapping.EncodedRemoteValue() != "private") {
			continue;
		}
		const bool accuracy_matches = (plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::EXACT &&
		                               mapping.Accuracy() == duckdb_api::CompiledPredicateAccuracy::EXACT) ||
		                              (plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::SUPERSET &&
		                               mapping.Accuracy() == duckdb_api::CompiledPredicateAccuracy::SUPERSET);
		if (accuracy_matches) {
			Require(selected_mapping == nullptr, "selective plan retained multiple executable mapping meanings");
			selected_mapping = &mapping;
		}
	}
	Require(selected_mapping != nullptr, "selective plan had no matching Connector mapping meaning");

	const std::string remote_truth = "visibility = 'private'";
	if (plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::EXACT) {
		Require(selected_mapping->ProofIdentity() ==
		                duckdb_api::CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY &&
		            operation.name == "controlled_exact_repositories" &&
		            selected_mapping->RemoteInputName() == "visibility" &&
		            selected_mapping->OccurrencePreservation() ==
		                duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES,
		        "Exact plan was disconnected from its exact occurrence proof");
		return {remote_truth, "controlled_exact_visibility_private_result IS TRUE"};
	}

	Require(plan.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::SUPERSET &&
	            selected_mapping->ProofIdentity() ==
	                duckdb_api::CompiledPredicateProofIdentity::GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY &&
	            operation.name == "github_authenticated_repositories" &&
	            selected_mapping->RemoteInputName() == "visibility" &&
	            selected_mapping->OccurrencePreservation() ==
	                duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_ALL_MATCHING_BASE_OCCURRENCES,
	        "Superset plan was disconnected from its occurrence-preservation proof");
	// This column is the deterministic response fixture for the declared GitHub
	// operation plus visibility=private input. It preserves every mapped private
	// occurrence and deliberately returns one public extra; it is not a test-only
	// substitution of remote TRUE.
	return {"github_visibility_private_result", "github_visibility_private_result IS TRUE"};
}

std::vector<std::int64_t> SelectedOccurrences(duckdb::Connection &connection, const std::string &where) {
	auto result = connection.Query("SELECT occurrence_id FROM " + std::string(BASE_OCCURRENCES) + " WHERE " + where +
	                               " ORDER BY occurrence_id");
	if (result->HasError()) {
		throw std::runtime_error("predicate law query failed: " + result->GetError());
	}
	std::vector<std::int64_t> occurrences;
	for (duckdb::idx_t row = 0; row < result->RowCount(); row++) {
		occurrences.push_back(result->GetValue(0, row).GetValue<std::int64_t>());
	}
	return occurrences;
}

std::vector<TruthOccurrence> TruthVector(duckdb::Connection &connection, const std::string &predicate) {
	auto result = connection.Query("SELECT occurrence_id, " + predicate + " FROM " + std::string(BASE_OCCURRENCES) +
	                               " ORDER BY occurrence_id");
	if (result->HasError()) {
		throw std::runtime_error("predicate truth-vector query failed: " + result->GetError());
	}
	std::vector<TruthOccurrence> values;
	for (duckdb::idx_t row = 0; row < result->RowCount(); row++) {
		const auto value = result->GetValue(1, row);
		values.push_back({result->GetValue(0, row).GetValue<std::int64_t>(), value.IsNull() ? TruthValue::NULL_VALUE
		                                                                     : value.GetValue<bool>()
		                                                                         ? TruthValue::TRUE_VALUE
		                                                                         : TruthValue::FALSE_VALUE});
	}
	return values;
}

bool ContainsAllOccurrences(const std::vector<std::int64_t> &container, const std::vector<std::int64_t> &required) {
	for (const auto occurrence : required) {
		if (std::count(container.begin(), container.end(), occurrence) <
		    std::count(required.begin(), required.end(), occurrence)) {
			return false;
		}
	}
	return true;
}

bool DuckDbTruthImpliesRemoteTruth(const std::vector<TruthOccurrence> &duckdb_truth,
                                   const std::vector<TruthOccurrence> &remote_truth) {
	if (duckdb_truth.size() != remote_truth.size()) {
		return false;
	}
	for (std::size_t index = 0; index < duckdb_truth.size(); index++) {
		if (duckdb_truth[index].occurrence_id != remote_truth[index].occurrence_id ||
		    (duckdb_truth[index].value == TruthValue::TRUE_VALUE &&
		     remote_truth[index].value != TruthValue::TRUE_VALUE)) {
			return false;
		}
	}
	return true;
}

void RequireDuckDbOwnership(const duckdb_api::ScanPlan &plan, const std::string &context) {
	Require(plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().projection == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().ordering == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().offset == duckdb_api::RelationalOwner::DUCKDB,
	        context + " transferred a relational owner from DuckDB");
}

void RequireCompositionLaw(duckdb::Connection &connection, const duckdb_api::ScanPlan &plan,
                           const duckdb_api::CompiledRelation &relation, const std::string &duckdb_predicate,
                           duckdb_api::PredicateDecisionCategory expected_category, const std::string &context) {
	const auto remote = DeriveRemoteLaw(plan, relation);
	const auto duckdb_only = SelectedOccurrences(connection, duckdb_predicate);
	const auto remote_occurrences = SelectedOccurrences(connection, remote.selected_occurrences_sql);
	const auto duckdb_truth = TruthVector(connection, duckdb_predicate);
	const auto remote_truth = TruthVector(connection, remote.truth_sql);
	const auto composed =
	    SelectedOccurrences(connection, "(" + remote.selected_occurrences_sql + ") AND (" + duckdb_predicate + ")");
	Require(plan.PredicateCategory() == expected_category && composed == duckdb_only,
	        context + " changed the DuckDB-only result bag");
	RequireDuckDbOwnership(plan, context);

	if (expected_category == duckdb_api::PredicateDecisionCategory::EXACT) {
		Require(duckdb_truth == remote_truth && remote_occurrences == duckdb_only,
		        context + " changed a per-occurrence TRUE/FALSE/NULL result or exact occurrence bag");
		Require(
		    std::count_if(duckdb_truth.begin(), duckdb_truth.end(),
		                  [](const TruthOccurrence &value) { return value.value == TruthValue::TRUE_VALUE; }) == 3 &&
		        std::count_if(duckdb_truth.begin(), duckdb_truth.end(),
		                      [](const TruthOccurrence &value) { return value.value == TruthValue::FALSE_VALUE; }) ==
		            2 &&
		        std::count_if(duckdb_truth.begin(), duckdb_truth.end(),
		                      [](const TruthOccurrence &value) { return value.value == TruthValue::NULL_VALUE; }) == 1,
		    context + " did not exercise the complete three-valued truth domain");
	} else if (expected_category == duckdb_api::PredicateDecisionCategory::SUPERSET) {
		Require(DuckDbTruthImpliesRemoteTruth(duckdb_truth, remote_truth) &&
		            ContainsAllOccurrences(remote_occurrences, duckdb_only),
		        context + " violated per-occurrence D => R or lost required occurrence multiplicity");
	} else {
		Require(remote_occurrences == SelectedOccurrences(connection, "TRUE") &&
		            plan.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE,
		        context + " fallback did not preserve the complete base bag without input authority");
	}
}

void RequireExplicitSupersetExtra(duckdb::Connection &connection, const duckdb_api::ScanPlan &plan,
                                  const duckdb_api::CompiledRelation &relation) {
	const auto remote = DeriveRemoteLaw(plan, relation);
	const auto selected = SelectedOccurrences(connection, remote.selected_occurrences_sql);
	const auto duckdb_only = SelectedOccurrences(connection, "visibility = 'private'");
	Require(selected == std::vector<std::int64_t>({101, 102, 103, 104}) &&
	            duckdb_only == std::vector<std::int64_t>({101, 102, 103}),
	        "declared Superset operation fixture did not exercise one explicit extra occurrence");
}

void RequirePlanningError(const duckdb_api::CompiledConnector &connector, const duckdb_api::ScanRequest &request,
                          duckdb_api::PlanningErrorCode expected_code, const std::string &context) {
	bool rejected = false;
	try {
		(void)duckdb_api::BuildConservativeScanPlan(connector, request);
	} catch (const duckdb_api::PlanningError &error) {
		rejected = error.Code() == expected_code;
	}
	Require(rejected, context + " did not fail with the required planning error");
}

void TestProductionDecisionCompositionMatrix() {
	duckdb::DuckDB database(nullptr);
	duckdb::Connection connection(database);

	const auto native = duckdb_api::BuildNativeGithubConnector();
	const auto &native_relation = FindRelation(native, "authenticated_repositories");
	const auto visibility = VisibilityPrivate(native_relation);

	auto request =
	    CandidateRequest(native, native_relation, visibility, duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE);
	const auto superset = duckdb_api::BuildConservativeScanPlan(native, request);
	RequireCompositionLaw(connection, superset, native_relation, "visibility = 'private'",
	                      duckdb_api::PredicateDecisionCategory::SUPERSET, "native Superset leaf");
	RequireExplicitSupersetExtra(connection, superset, native_relation);
	Require(SelectedOccurrences(connection, "visibility = 'private'") == std::vector<std::int64_t>({101, 102, 103}),
	        "equal-valued duplicate rows lost their distinct occurrence identities");

	const auto controlled = duckdb_api_test::BuildExactPredicateCatalogFixture();
	const auto &controlled_relation = FindRelation(controlled, duckdb_api_test::PREDICATE_EXACT_RELATION);
	const auto exact = duckdb_api::BuildConservativeScanPlan(
	    controlled, CandidateRequest(controlled, controlled_relation, VisibilityPrivate(controlled_relation),
	                                 duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE));
	RequireCompositionLaw(connection, exact, controlled_relation, "visibility = 'private'",
	                      duckdb_api::PredicateDecisionCategory::EXACT, "controlled Exact leaf");

	const auto conjunction = duckdb_api::BuildConservativeScanPlan(
	    native, CandidateRequest(native, native_relation,
	                             duckdb_api::RequestedPredicate::Conjunction(
	                                 {visibility, duckdb_api::RequestedPredicate::Unsupported(1)}),
	                             duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER));
	RequireCompositionLaw(connection, conjunction, native_relation, "visibility = 'private' AND archived = FALSE",
	                      duckdb_api::PredicateDecisionCategory::SUPERSET, "mapped AND opaque child");

	const auto disjunction = duckdb_api::BuildConservativeScanPlan(
	    native, CandidateRequest(native, native_relation,
	                             duckdb_api::RequestedPredicate::Disjunction(
	                                 {visibility, duckdb_api::RequestedPredicate::Unsupported(1)}),
	                             duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER));
	RequireCompositionLaw(connection, disjunction, native_relation, "visibility = 'private' OR archived = FALSE",
	                      duckdb_api::PredicateDecisionCategory::UNSUPPORTED, "unencodable OR");

	const auto negation = duckdb_api::BuildConservativeScanPlan(
	    native, CandidateRequest(native, native_relation, duckdb_api::RequestedPredicate::Negation(visibility),
	                             duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE));
	RequireCompositionLaw(connection, negation, native_relation, "NOT (visibility = 'private')",
	                      duckdb_api::PredicateDecisionCategory::UNSUPPORTED, "unencodable NOT");

	auto missing_inspection =
	    CandidateRequest(native, native_relation, visibility, duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE);
	missing_inspection.capabilities.selective_predicate = false;
	const auto inspection_fallback = duckdb_api::BuildConservativeScanPlan(native, missing_inspection);
	RequireCompositionLaw(connection, inspection_fallback, native_relation, "visibility = 'private'",
	                      duckdb_api::PredicateDecisionCategory::UNSUPPORTED, "missing inspection capability");
	auto missing_retention =
	    CandidateRequest(native, native_relation, visibility, duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE);
	missing_retention.capabilities.retains_predicate = false;
	const auto retention_fallback = duckdb_api::BuildConservativeScanPlan(native, missing_retention);
	RequireCompositionLaw(connection, retention_fallback, native_relation, "visibility = 'private'",
	                      duckdb_api::PredicateDecisionCategory::UNSUPPORTED, "missing retention capability");

	const auto ambiguous_connector = duckdb_api_test::BuildAmbiguousPredicateMappingsCatalogFixture();
	const auto &ambiguous_relation =
	    FindRelation(ambiguous_connector, duckdb_api_test::PREDICATE_AMBIGUOUS_MAPPINGS_RELATION);
	const auto ambiguous = duckdb_api::BuildConservativeScanPlan(
	    ambiguous_connector,
	    CandidateRequest(ambiguous_connector, ambiguous_relation, VisibilityPrivate(ambiguous_relation),
	                     duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE));
	RequireCompositionLaw(connection, ambiguous, ambiguous_relation, "visibility = 'private'",
	                      duckdb_api::PredicateDecisionCategory::AMBIGUOUS, "incompatible mapping-input ambiguity");

	const auto baseline = duckdb_api::BuildConservativeScanPlan(
	    native, CandidateRequest(native, native_relation, duckdb_api::RequestedPredicate::Unrestricted(),
	                             duckdb_api::RetainedPredicateScope::UNRESTRICTED));
	RequireCompositionLaw(connection, baseline, native_relation, "TRUE",
	                      duckdb_api::PredicateDecisionCategory::UNSUPPORTED, "unrestricted baseline");
}

void TestInvalidMatrixIsDistinctFromFallback() {
	const auto native = duckdb_api::BuildNativeGithubConnector();
	const auto &relation = FindRelation(native, "authenticated_repositories");
	const auto partial_or = CandidateRequest(native, relation, VisibilityPrivate(relation),
	                                         duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER);
	RequirePlanningError(native, partial_or, duckdb_api::PlanningErrorCode::INVALID_CONTRACT,
	                     "comparison-only partial OR counterexample");
	const auto compound_partial_or = CandidateRequest(
	    native, relation,
	    duckdb_api::RequestedPredicate::Conjunction({VisibilityPrivate(relation), VisibilityPrivate(relation)}),
	    duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER);
	RequirePlanningError(native, compound_partial_or, duckdb_api::PlanningErrorCode::INVALID_CONTRACT,
	                     "fully represented compound partial OR counterexample");

	auto invalid_binding =
	    CandidateRequest(native, relation,
	                     duckdb_api::RequestedPredicate::Comparison(
	                         relation.Columns().size(), duckdb_api::RequestedPredicateValueKind::VARCHAR,
	                         duckdb_api::RequestedPredicateComparisonOperator::EQUALS,
	                         duckdb_api::RequestedPredicateValue::Varchar("private")),
	                     duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE);
	RequirePlanningError(native, invalid_binding, duckdb_api::PlanningErrorCode::INVALID_CONTRACT,
	                     "invalid bound ordinal");

	const auto equal_ranked = duckdb_api_test::BuildEqualRankedOperationsCatalogFixture();
	const auto &equal_relation =
	    FindRelation(equal_ranked, duckdb_api_test::PREDICATE_EQUAL_RANKED_OPERATIONS_RELATION);
	const auto equal_request = CandidateRequest(equal_ranked, equal_relation, VisibilityPrivate(equal_relation),
	                                            duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE);
	RequirePlanningError(equal_ranked, equal_request, duckdb_api::PlanningErrorCode::OPERATION_SELECTION_FAILED,
	                     "equal-ranked operation selection");
}

} // namespace

void RunPredicateCompositionLawTests() {
	TestProductionDecisionCompositionMatrix();
	TestInvalidMatrixIsDistinctFromFallback();
}
