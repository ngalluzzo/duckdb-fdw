#include "scan_planner_internal.hpp"

#include "graphql_operation_planner.hpp"
#include "predicate_classifier.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace duckdb_api {
namespace scan_planner_internal {

std::uint64_t BoundedProduct(std::uint64_t left, std::uint64_t right, std::uint64_t ceiling, const char *field) {
	if (left == 0 || right == 0 || left > std::numeric_limits<std::uint64_t>::max() / right) {
		throw std::logic_error(std::string("selected pagination overflows ") + field);
	}
	return std::min(left * right, ceiling);
}

namespace {

bool IsSupportedLogicalType(const std::string &logical_type) {
	return logical_type == "BIGINT" || logical_type == "VARCHAR" || logical_type == "BOOLEAN";
}

bool Contains(const std::vector<std::string> &values, const std::string &expected) {
	return std::find(values.begin(), values.end(), expected) != values.end();
}

bool OriginsEqual(const CompiledRestOrigin &left, const CompiledRestOrigin &right) {
	return left.scheme == right.scheme && left.host.Value() == right.host.Value() && left.port == right.port;
}

std::vector<std::string> ProjectedColumnNames(const std::vector<CompiledColumn> &columns) {
	std::vector<std::string> result;
	result.reserve(columns.size());
	for (const auto &column : columns) {
		result.push_back(column.name);
	}
	return result;
}

void ValidateSchema(const CompiledRelation &relation) {
	const auto &columns = relation.Columns();
	if (columns.empty()) {
		throw std::logic_error("selected relation contains no output schema");
	}
	for (std::size_t index = 0; index < columns.size(); index++) {
		const auto &column = columns[index];
		if (column.name.empty() || !IsSupportedLogicalType(column.logical_type) || column.extractor.empty()) {
			throw std::logic_error("selected relation contains an unsupported output column");
		}
		for (std::size_t other = index + 1; other < columns.size(); other++) {
			if (column.name == columns[other].name) {
				throw std::logic_error("selected relation contains a duplicate output column");
			}
		}
	}
}

void ValidateSourceShape(const CompiledOperation &operation, const CompiledResourceCeilings &ceilings) {
	const auto &rest = operation.Rest();
	if (rest.response_source == CompiledResponseSource::JSON_PATH_MANY) {
		if (operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY || rest.records_extractor.empty() ||
		    rest.records_extractor == "$") {
			throw std::logic_error("JSON-path response contains contradictory source cardinality or extraction");
		}
		return;
	}
	if (rest.response_source == CompiledResponseSource::ROOT_ARRAY) {
		if (operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY || rest.records_extractor != "$") {
			throw std::logic_error("root-array response contains contradictory cardinality or extraction");
		}
		return;
	}
	if (rest.response_source == CompiledResponseSource::ROOT_OBJECT) {
		if (operation.cardinality != CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS ||
		    rest.records_extractor != "$" || ceilings.MaxRecordsPerPage() != 1 || ceilings.MaxRecordsPerScan() != 1) {
			throw std::logic_error("root-object response contains contradictory cardinality, extraction, or budget");
		}
		return;
	}
	throw std::logic_error("selected relation contains an unsupported response source");
}

void ValidatePagination(const CompiledOperation &operation, const CompiledResourceCeilings &ceilings,
                        const CompiledNetworkPolicy &network_policy) {
	const auto &rest = operation.Rest();
	const auto &pagination = rest.pagination;
	if (pagination.Strategy() == CompiledPaginationStrategy::DISABLED) {
		if (ceilings.MaxRecordsPerPage() != ceilings.MaxRecordsPerScan() ||
		    (ceilings.HasResponseByteNarrowing() &&
		     ceilings.MaxResponseBytesPerPage() != ceilings.MaxResponseBytesPerScan())) {
			throw std::logic_error("unpaginated relation contains contradictory page and scan resource scopes");
		}
		return;
	}
	if (pagination.Strategy() != CompiledPaginationStrategy::LINK_HEADER ||
	    PlanPageDependency(pagination.Dependency()) != PlannedPageDependency::SEQUENTIAL ||
	    PlanPageConsistency(pagination.Consistency()) != PlannedPageConsistency::MUTABLE ||
	    PlanLinkRelation(pagination.LinkRelation()) != PlannedLinkRelation::NEXT ||
	    PlanTargetScope(pagination.TargetScope()) != PlannedContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH ||
	    pagination.SupportsTotal() || pagination.SupportsResume() || rest.retry_enabled ||
	    operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY ||
	    (rest.response_source != CompiledResponseSource::JSON_PATH_MANY &&
	     rest.response_source != CompiledResponseSource::ROOT_ARRAY)) {
		throw std::logic_error("selected relation contains an unsupported pagination capability profile");
	}
	if (pagination.PageSizeParameter().empty() || pagination.PageNumberParameter().empty() ||
	    pagination.PageSizeParameter() == pagination.PageNumberParameter() || pagination.PageSize() == 0 ||
	    pagination.FirstPage() == 0 || pagination.PageIncrement() != 1 || pagination.MaxPagesPerScan() == 0 ||
	    pagination.MaxPagesPerScan() > PAGINATION_MAX_PAGES_PER_SCAN) {
		throw std::logic_error("selected pagination contains an unsupported typed page transition");
	}

	// Connector supplies typed bindings. Comparing them to the structural request
	// proves consistency; this planner never discovers pagination by parsing query
	// text or parameter names.
	const auto &query = rest.request.query_parameters;
	if (query.size() != 2 || query[0].name != pagination.PageSizeParameter() ||
	    query[0].encoded_value != std::to_string(pagination.PageSize()) ||
	    query[1].name != pagination.PageNumberParameter() ||
	    query[1].encoded_value != std::to_string(pagination.FirstPage())) {
		throw std::logic_error("selected pagination disagrees with its structural initial request");
	}
	if (!ceilings.HasResponseByteNarrowing() || ceilings.MaxResponseBytesPerPage() == 0 ||
	    ceilings.MaxResponseBytesPerScan() < ceilings.MaxResponseBytesPerPage() ||
	    ceilings.MaxResponseBytesPerPage() > network_policy.max_response_bytes ||
	    ceilings.MaxResponseBytesPerPage() > PAGINATION_MAX_RESPONSE_BYTES_PER_PAGE ||
	    ceilings.MaxResponseBytesPerScan() > PAGINATION_MAX_RESPONSE_BYTES_PER_SCAN ||
	    ceilings.MaxRecordsPerPage() == 0 || ceilings.MaxRecordsPerScan() < ceilings.MaxRecordsPerPage() ||
	    pagination.PageSize() > ceilings.MaxRecordsPerPage() ||
	    ceilings.MaxRecordsPerPage() > PAGINATION_MAX_DECODED_RECORDS_PER_PAGE ||
	    ceilings.MaxRecordsPerScan() > PAGINATION_MAX_DECODED_RECORDS_PER_SCAN ||
	    ceilings.MaxExtractedStringBytes() == 0 ||
	    ceilings.MaxExtractedStringBytes() > PAGINATION_MAX_EXTRACTED_STRING_BYTES) {
		throw std::logic_error("selected pagination contains an invalid page or scan resource envelope");
	}
	const auto max_pages = pagination.MaxPagesPerScan();
	if (ceilings.MaxResponseBytesPerScan() > BoundedProduct(ceilings.MaxResponseBytesPerPage(), max_pages,
	                                                        PAGINATION_MAX_RESPONSE_BYTES_PER_SCAN,
	                                                        "response-byte scope") ||
	    ceilings.MaxRecordsPerScan() > BoundedProduct(ceilings.MaxRecordsPerPage(), max_pages,
	                                                  PAGINATION_MAX_DECODED_RECORDS_PER_SCAN,
	                                                  "decoded-record scope")) {
		throw std::logic_error("selected pagination scan resource scope exceeds its bounded page sequence");
	}
}

void ValidateExecutableOrigin(const CompiledRestOrigin &origin, const CompiledNetworkPolicy &policy) {
	const auto scheme = UrlSchemeName(origin.scheme);
	if (origin.port == 0 || !Contains(policy.allowed_schemes, scheme) ||
	    !Contains(policy.allowed_hosts, origin.host.Value())) {
		throw std::logic_error("selected operation origin is outside connector network policy");
	}
	if (policy.redirects_enabled || policy.private_addresses_enabled || policy.link_local_addresses_enabled) {
		throw std::logic_error("connector network policy exceeds the supported planner authority");
	}
	if (origin.scheme == CompiledUrlScheme::HTTPS) {
		if (origin.port != 443 || policy.loopback_addresses_enabled) {
			throw std::logic_error("HTTPS operation does not use the supported exact public authority profile");
		}
		return;
	}
	if (origin.scheme == CompiledUrlScheme::HTTP && policy.loopback_addresses_enabled) {
		return;
	}
	throw std::logic_error("cleartext operation lacks private controlled loopback authority");
}

bool IsControlledCompleteRootArray(const CompiledRelation &relation, const CompiledOperation &operation) {
	bool found_selected_mapping = false;
	for (const auto &mapping : relation.PredicateMappings()) {
		if (mapping.OperationName() != operation.name) {
			continue;
		}
		found_selected_mapping = true;
		if (mapping.Accuracy() != CompiledPredicateAccuracy::EXACT ||
		    mapping.ProofIdentity() !=
		        CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY ||
		    mapping.BaseDomain() != CompiledPredicateBaseDomain::CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES ||
		    mapping.OccurrencePreservation() !=
		        CompiledPredicateOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES) {
			return false;
		}
	}
	return found_selected_mapping;
}

void ValidateOperation(const CompiledRelation &relation, const CompiledOperation &operation,
                       const CompiledNetworkPolicy &network_policy) {
	const auto &ceilings = relation.ResourceCeilings();
	if (operation.name.empty() || ceilings.MaxRecordsPerPage() == 0 || ceilings.MaxRecordsPerScan() == 0 ||
	    ceilings.MaxExtractedStringBytes() == 0) {
		throw std::logic_error("selected relation contains an unsupported base operation or resource declaration");
	}
	if (operation.Protocol() == CompiledProtocol::GRAPHQL) {
		ValidateGraphqlOperationProfile(relation, operation, network_policy);
		return;
	}
	if (operation.Protocol() != CompiledProtocol::REST) {
		throw std::logic_error("selected relation contains an unknown protocol alternative");
	}
	const auto &rest = operation.Rest();
	if (rest.retry_enabled || rest.request.path.empty() || rest.request.path.front() != '/') {
		throw std::logic_error("selected REST operation contains an unsupported request or retry declaration");
	}
	if (rest.response_source == CompiledResponseSource::ROOT_ARRAY &&
	    rest.pagination.Strategy() == CompiledPaginationStrategy::DISABLED) {
		if (!IsControlledCompleteRootArray(relation, operation)) {
			throw std::logic_error("root-array response requires an explicit supported pagination declaration");
		}
	}
	(void)PlanProtocol(operation.Protocol());
	(void)PlanMethod(rest.method);
	(void)PlanReplaySafety(rest.replay_safety);
	(void)PlanCardinality(operation.cardinality);
	(void)PlanResponseSource(rest.response_source);
	ValidateSourceShape(operation, ceilings);
	ValidateExecutableOrigin(rest.request.origin, network_policy);
	ValidatePagination(operation, ceilings, network_policy);
}

bool HasBinding(const predicate_classifier::CandidateInputBindings &bindings, const std::string &name) {
	return std::any_of(
	    bindings.values.begin(), bindings.values.end(),
	    [&name](const predicate_classifier::CandidateInputBinding &binding) { return binding.name == name; });
}

bool HasEveryBinding(const predicate_classifier::CandidateInputBindings &bindings,
                     const std::vector<std::string> &names) {
	return std::all_of(names.begin(), names.end(),
	                   [&bindings](const std::string &name) { return HasBinding(bindings, name); });
}

struct EligibleOperation {
	const CompiledOperation *operation;
	std::size_t specificity;
	std::int32_t priority;
};

EligibleOperation EvaluateEligibility(const CompiledRelation &relation, const CompiledOperation &operation,
                                      const ScanRequest &request) {
	const auto bindings = predicate_classifier::ResolveCandidateInputBindings(relation, operation, request);
	if (bindings.conflicting || !HasEveryBinding(bindings, operation.selector.RequiredInputs()) ||
	    std::any_of(operation.selector.ForbiddenInputs().begin(), operation.selector.ForbiddenInputs().end(),
	                [&bindings](const std::string &name) { return HasBinding(bindings, name); })) {
		return {nullptr, 0, 0};
	}

	std::size_t largest_satisfied_alternative = 0;
	if (!operation.selector.AnyInputSets().empty()) {
		bool has_satisfied_alternative = false;
		for (const auto &alternative : operation.selector.AnyInputSets()) {
			if (HasEveryBinding(bindings, alternative)) {
				has_satisfied_alternative = true;
				largest_satisfied_alternative = std::max(largest_satisfied_alternative, alternative.size());
			}
		}
		if (!has_satisfied_alternative) {
			return {nullptr, 0, 0};
		}
	}

	return EligibleOperation {&operation, operation.selector.RequiredInputs().size() + largest_satisfied_alternative,
	                          operation.selector.Priority()};
}

bool HasHigherRank(const EligibleOperation &left, const EligibleOperation &right) {
	return left.specificity > right.specificity ||
	       (left.specificity == right.specificity && left.priority > right.priority);
}

bool HasEqualRank(const EligibleOperation &left, const EligibleOperation &right) {
	return left.specificity == right.specificity && left.priority == right.priority;
}

bool ContainsOpaqueUnsupportedPosition(const RequestedPredicate &candidate) {
	switch (candidate.Kind()) {
	case RequestedPredicateKind::UNSUPPORTED:
		return true;
	case RequestedPredicateKind::CONJUNCTION:
	case RequestedPredicateKind::DISJUNCTION:
	case RequestedPredicateKind::NEGATION:
		for (const auto &child : candidate.Children()) {
			if (ContainsOpaqueUnsupportedPosition(child)) {
				return true;
			}
		}
		return false;
	case RequestedPredicateKind::UNRESTRICTED:
	case RequestedPredicateKind::COMPARISON:
		return false;
	}
	return false;
}

void ValidateRequest(const CompiledConnector &connector, const CompiledRelation &relation, const ScanRequest &request) {
	if (request.connector_name != connector.ConnectorName() || request.relation_name != relation.Name() ||
	    !request.explicit_inputs.empty() || request.projected_columns != ProjectedColumnNames(relation.Columns()) ||
	    !request.orderings.empty() || request.has_limit || request.has_offset || request.capabilities.projection ||
	    request.capabilities.filter || request.capabilities.ordering || request.capabilities.limit ||
	    request.capabilities.offset || request.capabilities.progress || !request.capabilities.cancellation) {
		throw std::logic_error("planner received a non-conservative scan request for the selected relation");
	}
	switch (request.retained_predicate_scope) {
	case RetainedPredicateScope::UNRESTRICTED:
	case RetainedPredicateScope::REQUESTED_PREDICATE:
	case RetainedPredicateScope::COMPLETE_DUCKDB_FILTER:
		break;
	default:
		throw std::logic_error("planner received an unknown retained-predicate scope");
	}
	const auto requested_kind = request.requested_predicate.Kind();
	if ((requested_kind == RequestedPredicateKind::UNRESTRICTED &&
	     request.retained_predicate_scope == RetainedPredicateScope::REQUESTED_PREDICATE) ||
	    (requested_kind != RequestedPredicateKind::UNRESTRICTED &&
	     request.retained_predicate_scope == RetainedPredicateScope::UNRESTRICTED)) {
		throw std::logic_error("planner received contradictory requested and retained predicate states");
	}
	// COMPLETE_DUCKDB_FILTER means Query retained more structure than it could
	// represent. The candidate must therefore contain an opaque position proving
	// where that unseen structure lives. Otherwise a fully represented p AND p
	// could actually be a fragment of (p AND p) OR q and selecting p would lose
	// DuckDB-true rows. Fully represented trees use REQUESTED_PREDICATE; a
	// represented p AND Unsupported remains safely selectable as a Superset.
	if (requested_kind != RequestedPredicateKind::UNRESTRICTED &&
	    request.retained_predicate_scope == RetainedPredicateScope::COMPLETE_DUCKDB_FILTER &&
	    !ContainsOpaqueUnsupportedPosition(request.requested_predicate)) {
		throw std::logic_error("fully represented candidate contradicts its opaque complete-filter scope");
	}
	switch (requested_kind) {
	case RequestedPredicateKind::UNRESTRICTED:
	case RequestedPredicateKind::COMPARISON:
	case RequestedPredicateKind::CONJUNCTION:
	case RequestedPredicateKind::DISJUNCTION:
	case RequestedPredicateKind::NEGATION:
	case RequestedPredicateKind::UNSUPPORTED:
		return;
	}
	throw std::logic_error("planner received an unknown requested-predicate state");
}

void ValidateAuthentication(const CompiledRelation &relation, const CompiledOperation &operation,
                            const ScanRequest &request) {
	const auto &authentication = relation.Authentication();
	const auto requirement = authentication.Requirement();
	if (requirement == CompiledCredentialRequirement::NONE) {
		if (!authentication.LogicalCredential().empty() ||
		    authentication.Authenticator() != CompiledAuthenticator::NONE ||
		    authentication.Placement() != CompiledCredentialPlacement::NONE ||
		    authentication.Destination() != nullptr) {
			throw std::logic_error("anonymous relation contains a contradictory authentication policy");
		}
		if (request.secret_reference.IsPresent()) {
			throw std::logic_error("anonymous relation received a surplus logical secret reference");
		}
		return;
	}
	if (requirement != CompiledCredentialRequirement::REQUIRED || authentication.LogicalCredential().empty() ||
	    authentication.Authenticator() != CompiledAuthenticator::BEARER ||
	    authentication.Placement() != CompiledCredentialPlacement::AUTHORIZATION_HEADER ||
	    authentication.Destination() == nullptr) {
		throw std::logic_error("authenticated relation contains a contradictory authentication policy");
	}
	const auto &origin = operation.Protocol() == CompiledProtocol::REST ? operation.Rest().request.origin
	                                                                    : operation.Graphql().endpoint_origin;
	if (!OriginsEqual(*authentication.Destination(), origin)) {
		throw std::logic_error("authenticated relation credential destination differs from the selected operation");
	}
	if (!request.secret_reference.IsPresent()) {
		throw std::logic_error("authenticated relation is missing its logical secret reference");
	}
	if (!request.capabilities.secret_manager) {
		throw std::logic_error("authenticated relation requires unavailable Secret Manager capability");
	}
}

} // namespace

SelectedRelationOperation ValidateAndSelectOperation(const CompiledConnector &connector, const ScanRequest &request) {
	if (connector.Origin() != CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA || connector.ConnectorName().empty() ||
	    connector.Version().empty()) {
		throw std::logic_error("planner received incomplete native connector identity");
	}
	if (request.connector_name != connector.ConnectorName()) {
		throw std::logic_error("planner received a request for another connector");
	}

	// Exact lookup is the only selection path. Unknown or case-varied names fail;
	// no relation, including the anonymous relation, is a runtime fallback.
	const auto *relation = connector.FindRelation(request.relation_name);
	if (relation == nullptr) {
		throw PlanningError(PlanningErrorCode::INVALID_CONTRACT, "planner request names an unknown exact relation");
	}

	ValidateSchema(*relation);
	ValidateRequest(connector, *relation, request);
	for (const auto &operation : relation->Operations()) {
		ValidateOperation(*relation, operation, connector.NetworkPolicy());
	}

	// Predicate mappings are operation-scoped, so each candidate derives its own
	// immutable binding set. Declaration order is never a tie-breaker. The sole
	// fallback is considered only after all non-fallback operations are proven
	// ineligible; only the selected operation reaches full classification.
	EligibleOperation winner {nullptr, 0, 0};
	bool winner_is_tied = false;
	const CompiledOperation *fallback = nullptr;
	for (const auto &operation : relation->Operations()) {
		if (operation.fallback) {
			if (fallback != nullptr) {
				throw PlanningError(PlanningErrorCode::OPERATION_SELECTION_FAILED,
				                    "operation selection contains multiple fallback operations");
			}
			fallback = &operation;
			continue;
		}
		const auto candidate = EvaluateEligibility(*relation, operation, request);
		if (candidate.operation == nullptr) {
			continue;
		}
		if (winner.operation == nullptr || HasHigherRank(candidate, winner)) {
			winner = candidate;
			winner_is_tied = false;
		} else if (HasEqualRank(candidate, winner)) {
			winner_is_tied = true;
		}
	}
	if (winner_is_tied) {
		throw PlanningError(PlanningErrorCode::OPERATION_SELECTION_FAILED,
		                    "operation selection has multiple equally ranked eligible base operations");
	}
	if (winner.operation == nullptr && fallback != nullptr) {
		winner = EvaluateEligibility(*relation, *fallback, request);
	}
	if (winner.operation == nullptr) {
		throw PlanningError(PlanningErrorCode::OPERATION_SELECTION_FAILED,
		                    "operation selection has no eligible base operation");
	}
	const auto &operation = *winner.operation;
	ValidateAuthentication(*relation, operation, request);
	return {relation, &operation};
}

} // namespace scan_planner_internal
} // namespace duckdb_api
