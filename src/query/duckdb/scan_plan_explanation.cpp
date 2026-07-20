#include "scan_plan_explanation.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb_api/scan_plan.hpp"
#include "duckdb_api/scan_request.hpp"

#include <sstream>
#include <string>

namespace duckdb {
namespace duckdb_api_query_internal {

const char *PredicateNameForExplanation(duckdb_api::PlannedPredicate predicate) {
	if (predicate == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN) {
		return "unrestricted";
	}
	if (predicate == duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE) {
		return "visibility_equals_private";
	}
	if (predicate == duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER) {
		return "complete_duckdb_filter";
	}
	throw InternalException("duckdb_api scan plan contains an unknown predicate state");
}

namespace {

const char *AccuracyName(duckdb_api::RemotePredicateAccuracy accuracy) {
	switch (accuracy) {
	case duckdb_api::RemotePredicateAccuracy::UNSUPPORTED:
		return "unsupported";
	case duckdb_api::RemotePredicateAccuracy::SUPERSET:
		return "superset";
	case duckdb_api::RemotePredicateAccuracy::EXACT:
		return "exact";
	}
	throw InternalException("duckdb_api scan plan contains an unknown remote accuracy");
}

const char *OwnerName(duckdb_api::RelationalOwner owner) {
	switch (owner) {
	case duckdb_api::RelationalOwner::DUCKDB:
		return "duckdb";
	}
	throw InternalException("duckdb_api scan plan contains an unknown relational owner");
}

const char *DelegationName(duckdb_api::RelationalDelegation delegation) {
	switch (delegation) {
	case duckdb_api::RelationalDelegation::NONE:
		return "none";
	}
	throw InternalException("duckdb_api scan plan contains an unknown relational delegation");
}

const char *CategoryName(duckdb_api::PredicateDecisionCategory category) {
	switch (category) {
	case duckdb_api::PredicateDecisionCategory::EXACT:
		return "exact";
	case duckdb_api::PredicateDecisionCategory::SUPERSET:
		return "superset";
	case duckdb_api::PredicateDecisionCategory::UNSUPPORTED:
		return "unsupported";
	case duckdb_api::PredicateDecisionCategory::AMBIGUOUS:
		return "ambiguous";
	}
	throw InternalException("duckdb_api scan plan contains an unknown predicate category");
}

const char *ReasonName(duckdb_api::PredicateDecisionReason reason) {
	switch (reason) {
	case duckdb_api::PredicateDecisionReason::NO_REMOTE_CANDIDATE:
		return "no_remote_candidate";
	case duckdb_api::PredicateDecisionReason::SELECTED_EXACT_MAPPING:
		return "selected_exact_mapping";
	case duckdb_api::PredicateDecisionReason::SELECTED_SUPERSET_MAPPING:
		return "selected_superset_mapping";
	case duckdb_api::PredicateDecisionReason::STRUCTURE_UNSUPPORTED:
		return "structure_unsupported";
	case duckdb_api::PredicateDecisionReason::CAPABILITY_UNAVAILABLE:
		return "capability_unavailable";
	case duckdb_api::PredicateDecisionReason::MAPPING_UNAVAILABLE:
		return "mapping_unavailable";
	case duckdb_api::PredicateDecisionReason::DISJUNCTION_ENCODING_UNAVAILABLE:
		return "disjunction_encoding_unavailable";
	case duckdb_api::PredicateDecisionReason::COMPLEMENT_ENCODING_UNAVAILABLE:
		return "complement_encoding_unavailable";
	case duckdb_api::PredicateDecisionReason::AMBIGUOUS_CONDITIONAL_INPUT:
		return "ambiguous_conditional_input";
	}
	throw InternalException("duckdb_api scan plan contains an unknown predicate reason");
}

const char *ScopeName(duckdb_api::RetainedPredicateScope scope) {
	switch (scope) {
	case duckdb_api::RetainedPredicateScope::UNRESTRICTED:
		return "unrestricted";
	case duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE:
		return "requested_predicate";
	case duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER:
		return "complete_duckdb_filter";
	}
	throw InternalException("duckdb_api scan request contains an unknown retained-filter scope");
}

std::string ProjectionClosure(const duckdb_api::ScanPlan &plan) {
	std::string result;
	for (std::size_t index = 0; index < plan.OutputColumns().size(); index++) {
		if (index > 0) {
			result += ',';
		}
		result += plan.OutputColumns()[index].name;
	}
	return result;
}

const char *CapabilityName(bool available) {
	return available ? "available" : "unavailable";
}

const char *ProtocolName(duckdb_api::PlannedProtocol protocol) {
	switch (protocol) {
	case duckdb_api::PlannedProtocol::REST:
		return "rest";
	case duckdb_api::PlannedProtocol::GRAPHQL:
		return "graphql";
	}
	throw InternalException("duckdb_api scan plan contains an unknown protocol");
}

const char *SchemeName(duckdb_api::PlannedUrlScheme scheme) {
	switch (scheme) {
	case duckdb_api::PlannedUrlScheme::HTTP:
		return "http";
	case duckdb_api::PlannedUrlScheme::HTTPS:
		return "https";
	}
	throw InternalException("duckdb_api scan plan contains an unknown endpoint scheme");
}

std::string EndpointIdentity(const duckdb_api::PlannedHttpOrigin &origin, const std::string &path) {
	std::ostringstream result;
	result << SchemeName(origin.scheme) << "://" << origin.host << ':' << origin.port << path;
	return result.str();
}

std::string OperationName(const duckdb_api::PlannedProtocolOperation &operation) {
	switch (operation.Protocol()) {
	case duckdb_api::PlannedProtocol::REST:
		return operation.Rest().operation_name;
	case duckdb_api::PlannedProtocol::GRAPHQL:
		return operation.Graphql().operation_name;
	}
	throw InternalException("duckdb_api scan plan contains an unknown protocol");
}

std::string EndpointIdentity(const duckdb_api::PlannedProtocolOperation &operation) {
	switch (operation.Protocol()) {
	case duckdb_api::PlannedProtocol::REST:
		return EndpointIdentity(operation.Rest().origin, operation.Rest().path);
	case duckdb_api::PlannedProtocol::GRAPHQL:
		return EndpointIdentity(operation.Graphql().origin, operation.Graphql().path);
	}
	throw InternalException("duckdb_api scan plan contains an unknown protocol");
}

const char *OperationKind(const duckdb_api::PlannedProtocolOperation &operation) {
	switch (operation.Protocol()) {
	case duckdb_api::PlannedProtocol::REST:
		switch (operation.Rest().method) {
		case duckdb_api::PlannedHttpMethod::GET:
			return "get";
		}
		throw InternalException("duckdb_api REST plan contains an unknown method");
	case duckdb_api::PlannedProtocol::GRAPHQL:
		switch (operation.Graphql().kind) {
		case duckdb_api::PlannedGraphqlOperationKind::QUERY:
			return "query";
		}
		throw InternalException("duckdb_api GraphQL plan contains an unknown operation kind");
	}
	throw InternalException("duckdb_api scan plan contains an unknown protocol");
}

const char *PartialDataPolicy(const duckdb_api::PlannedProtocolOperation &operation) {
	if (operation.Protocol() == duckdb_api::PlannedProtocol::REST) {
		return "not_applicable";
	}
	switch (operation.Graphql().response.partial_data) {
	case duckdb_api::PlannedGraphqlPartialDataPolicy::FAIL_ON_ANY_ERROR:
		return "fail_on_any_error";
	}
	throw InternalException("duckdb_api GraphQL plan contains an unknown partial-data policy");
}

const char *PaginationStrategyName(duckdb_api::PlannedPaginationStrategy strategy) {
	switch (strategy) {
	case duckdb_api::PlannedPaginationStrategy::DISABLED:
		return "disabled";
	case duckdb_api::PlannedPaginationStrategy::LINK_HEADER:
		return "link_header";
	case duckdb_api::PlannedPaginationStrategy::GRAPHQL_CURSOR:
		return "graphql_cursor";
	}
	throw InternalException("duckdb_api scan plan contains an unknown pagination strategy");
}

const char *PageDependencyName(duckdb_api::PlannedPageDependency dependency) {
	switch (dependency) {
	case duckdb_api::PlannedPageDependency::SEQUENTIAL:
		return "sequential";
	}
	throw InternalException("duckdb_api Link pagination plan contains an unknown dependency");
}

const char *PageConsistencyName(duckdb_api::PlannedPageConsistency consistency) {
	switch (consistency) {
	case duckdb_api::PlannedPageConsistency::MUTABLE:
		return "mutable";
	}
	throw InternalException("duckdb_api Link pagination plan contains an unknown consistency");
}

const char *GraphqlDependencyName(duckdb_api::PlannedGraphqlCursorDependency dependency) {
	switch (dependency) {
	case duckdb_api::PlannedGraphqlCursorDependency::SEQUENTIAL:
		return "sequential";
	}
	throw InternalException("duckdb_api GraphQL cursor plan contains an unknown dependency");
}

const char *GraphqlConsistencyName(duckdb_api::PlannedGraphqlCursorConsistency consistency) {
	switch (consistency) {
	case duckdb_api::PlannedGraphqlCursorConsistency::MUTABLE:
		return "mutable";
	}
	throw InternalException("duckdb_api GraphQL cursor plan contains an unknown consistency");
}

std::string NullableColumns(const duckdb_api::ScanPlan &plan) {
	std::string result;
	for (const auto &column : plan.OutputColumns()) {
		if (!column.nullable) {
			continue;
		}
		if (!result.empty()) {
			result += ',';
		}
		result += column.name;
	}
	return result.empty() ? "none" : result;
}

void AddPaginationFacts(InsertionOrderPreservingMap<string> &result, const duckdb_api::ScanPlan &plan) {
	const auto strategy = plan.Pagination().Strategy();
	result["Pagination Strategy"] = PaginationStrategyName(strategy);
	if (strategy == duckdb_api::PlannedPaginationStrategy::DISABLED) {
		result["Page Dependency"] = "none";
		result["Page Consistency"] = "none";
		result["Page Size"] = "none";
		result["Maximum Pages"] = "none";
		result["Page Row Bound"] = std::to_string(plan.Budgets().decoded_records);
		result["Scan Row Bound"] = "none";
		result["Page Body Bytes"] = std::to_string(plan.Budgets().serialized_request_body_bytes);
		result["Scan Body Bytes"] = "none";
		result["Total Support"] = "unavailable";
		result["Resume Support"] = "unavailable";
		return;
	}
	if (strategy == duckdb_api::PlannedPaginationStrategy::LINK_HEADER) {
		result["Page Dependency"] = PageDependencyName(plan.Pagination().Dependency());
		result["Page Consistency"] = PageConsistencyName(plan.Pagination().Consistency());
		result["Page Size"] = std::to_string(plan.Pagination().Target().page_size);
		result["Maximum Pages"] = std::to_string(plan.Pagination().ScanBudgets().pages);
		result["Total Support"] = plan.Pagination().SupportsTotal() ? "available" : "unavailable";
		result["Resume Support"] = plan.Pagination().SupportsResume() ? "available" : "unavailable";
	} else {
		const auto &cursor = plan.Pagination().GraphqlCursor();
		result["Page Dependency"] = GraphqlDependencyName(cursor.dependency);
		result["Page Consistency"] = GraphqlConsistencyName(cursor.consistency);
		result["Page Size"] = std::to_string(cursor.page_size);
		result["Maximum Pages"] = std::to_string(cursor.max_pages_per_scan);
		result["Total Support"] = cursor.supports_total ? "available" : "unavailable";
		result["Resume Support"] = cursor.supports_resume ? "available" : "unavailable";
	}
	result["Page Body Bytes"] = std::to_string(plan.Pagination().PageBudgets().serialized_request_body_bytes);
	result["Scan Body Bytes"] = std::to_string(plan.Pagination().ScanBudgets().serialized_request_body_bytes);
	result["Page Row Bound"] = std::to_string(plan.Pagination().PageBudgets().decoded_records);
	result["Scan Row Bound"] = std::to_string(plan.Pagination().ScanBudgets().decoded_records);
}

} // namespace

InsertionOrderPreservingMap<string> ExplainSelectedScan(const duckdb_api::ScanRequest &request,
                                                        const duckdb_api::ScanPlan &plan) {
	InsertionOrderPreservingMap<string> result;
	result["Relation"] = plan.RelationName();
	result["Protocol"] = ProtocolName(plan.Operation().Protocol());
	result["Operation Identity"] = OperationName(plan.Operation());
	result["Operation Kind"] = OperationKind(plan.Operation());
	result["Endpoint"] = EndpointIdentity(plan.Operation());
	result["Partial Data"] = PartialDataPolicy(plan.Operation());
	result["Nullable Columns"] = NullableColumns(plan);
	AddPaginationFacts(result, plan);
	result["Stable Row Order"] = "none";
	result["Snapshot Guarantee"] = "none";
	result["Candidate"] = request.requested_predicate.Snapshot();
	result["Remote Predicate"] = PredicateNameForExplanation(plan.RemotePredicate());
	result["Remote Accuracy"] = AccuracyName(plan.RemoteAccuracy());
	result["Offered Filter Scope"] = ScopeName(request.retained_predicate_scope);
	result["Filter Action"] = "retained";
	result["Residual Predicate"] = PredicateNameForExplanation(plan.ResidualPredicate());
	result["Residual Owner"] = OwnerName(plan.ResidualOwner());
	result["Filter Owner"] = OwnerName(plan.Ownership().filter);
	result["Projection Closure"] = ProjectionClosure(plan);
	result["Projection Owner"] = OwnerName(plan.Ownership().projection);
	result["Ordering Owner"] = OwnerName(plan.Ownership().ordering);
	result["Limit Owner"] = OwnerName(plan.Ownership().limit);
	result["Offset Owner"] = OwnerName(plan.Ownership().offset);
	result["Remote Ordering"] = DelegationName(plan.RemoteOrdering());
	result["Runtime Ordering"] = DelegationName(plan.RuntimeOrdering());
	result["Remote Limit"] = DelegationName(plan.RemoteLimit());
	result["Runtime Limit"] = DelegationName(plan.RuntimeLimit());
	result["Remote Offset"] = DelegationName(plan.RemoteOffset());
	result["Runtime Offset"] = DelegationName(plan.RuntimeOffset());
	result["Projection Metadata"] = CapabilityName(request.capabilities.projection);
	result["Generic Filter Execution"] = CapabilityName(request.capabilities.filter);
	result["Candidate Inspection"] = CapabilityName(request.capabilities.selective_predicate);
	result["DuckDB Residual Retention"] = request.capabilities.retains_predicate ? "verified" : "unavailable";
	result["Ordering Metadata"] = CapabilityName(request.capabilities.ordering);
	result["Limit Metadata"] = CapabilityName(request.capabilities.limit);
	result["Offset Metadata"] = CapabilityName(request.capabilities.offset);
	result["Classification Category"] = CategoryName(plan.PredicateCategory());
	result["Classification Reason"] = ReasonName(plan.PredicateReason());
	result["Classification Detail"] = plan.ClassificationReason();
	return result;
}

} // namespace duckdb_api_query_internal
} // namespace duckdb
