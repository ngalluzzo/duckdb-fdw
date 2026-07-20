#include "duckdb_api/internal/connector/predicate_proof_profile.hpp"

#include <stdexcept>

namespace duckdb_api {
namespace internal {

namespace {

bool HasCanonicalGithubOrigin(const CompiledHttpOrigin &origin) {
	return origin.scheme == CompiledUrlScheme::HTTPS && origin.host.Value() == "api.github.com" && origin.port == 443;
}

bool HasCanonicalRepositoryQuery(const CompiledOperation &operation) {
	const auto &query = operation.Rest().request.query_parameters;
	return query.size() == 2 && query[0].name == "per_page" && query[0].encoded_value == "100" &&
	       query[1].name == "page" && query[1].encoded_value == "1";
}

bool HasCanonicalGithubHeaders(const CompiledOperation &operation) {
	const auto &headers = operation.Rest().request.headers;
	return headers.size() == 3 && headers[0].name == "Accept" && headers[0].value == "application/vnd.github+json" &&
	       headers[1].name == "User-Agent" && headers[1].value == "duckdb-api/0.6.0" &&
	       headers[2].name == "X-GitHub-Api-Version" && headers[2].value == "2022-11-28";
}

bool HasCanonicalRepositoryPagination(const CompiledOperation &operation) {
	const auto &pagination = operation.Rest().pagination;
	return pagination.Strategy() == CompiledPaginationStrategy::LINK_HEADER &&
	       pagination.Dependency() == CompiledPageDependency::SEQUENTIAL &&
	       pagination.Consistency() == CompiledPageConsistency::MUTABLE &&
	       pagination.LinkRelation() == CompiledLinkRelation::NEXT &&
	       pagination.TargetScope() == CompiledContinuationTargetScope::EXACT_OPERATION_ORIGIN_AND_PATH &&
	       !pagination.SupportsTotal() && !pagination.SupportsResume() &&
	       pagination.PageSizeParameter() == "per_page" && pagination.PageSize() == 100 &&
	       pagination.PageNumberParameter() == "page" && pagination.FirstPage() == 1 &&
	       pagination.PageIncrement() == 1 && pagination.MaxPagesPerScan() == 32;
}

bool HasCanonicalRepositoryAuthentication(const CompiledAuthenticationPolicy &authentication) {
	const auto *destination = authentication.Destination();
	return authentication.Requirement() == CompiledCredentialRequirement::REQUIRED &&
	       authentication.LogicalCredential() == "token" &&
	       authentication.Authenticator() == CompiledAuthenticator::BEARER &&
	       authentication.Placement() == CompiledCredentialPlacement::AUTHORIZATION_HEADER && destination != nullptr &&
	       HasCanonicalGithubOrigin(*destination);
}

bool HasInstalledGithubProfile(const std::string &relation_name, const CompiledOperation &operation,
                               const CompiledAuthenticationPolicy &authentication,
                               const CompiledPredicateMapping &mapping) {
	return relation_name == "authenticated_repositories" && operation.name == "github_authenticated_repositories" &&
	       operation.fallback && operation.cardinality == CompiledOperationCardinality::ZERO_TO_MANY &&
	       operation.Protocol() == CompiledProtocol::REST && operation.Rest().method == CompiledHttpMethod::GET &&
	       operation.Rest().replay_safety == CompiledReplaySafety::SAFE && !operation.Rest().retry_enabled &&
	       HasCanonicalGithubOrigin(operation.Rest().request.origin) &&
	       operation.Rest().request.path == "/user/repos" && HasCanonicalRepositoryQuery(operation) &&
	       HasCanonicalGithubHeaders(operation) &&
	       operation.Rest().response_source == CompiledResponseSource::ROOT_ARRAY &&
	       operation.Rest().records_extractor == "$" && HasCanonicalRepositoryPagination(operation) &&
	       HasCanonicalRepositoryAuthentication(authentication) && mapping.ColumnName() == "visibility" &&
	       mapping.Operator() == CompiledPredicateOperator::EQUALS &&
	       mapping.Literal() == CompiledPredicateLiteral::VARCHAR_PRIVATE &&
	       mapping.OperationName() == "github_authenticated_repositories" &&
	       mapping.InputPlacement() == CompiledPredicateInputPlacement::REST_QUERY_PARAMETER &&
	       mapping.RemoteInputName() == "visibility" && mapping.EncodedRemoteValue() == "private" &&
	       mapping.Accuracy() == CompiledPredicateAccuracy::SUPERSET &&
	       mapping.BaseDomain() == CompiledPredicateBaseDomain::GITHUB_AUTHENTICATED_REPOSITORY_OCCURRENCES &&
	       mapping.OccurrencePreservation() ==
	           CompiledPredicateOccurrencePreservation::PRESERVES_ALL_MATCHING_BASE_OCCURRENCES &&
	       mapping.EncodingCapability() == CompiledPredicateEncodingCapability::SINGLE_POSITIVE_REST_QUERY_INPUT;
}

bool HasControlledExactOrigin(const CompiledHttpOrigin &origin) {
	return origin.scheme == CompiledUrlScheme::HTTPS && origin.host.Value() == "predicate-proof.invalid" &&
	       origin.port == 443;
}

bool HasControlledExactHeaders(const CompiledOperation &operation) {
	const auto &headers = operation.Rest().request.headers;
	return headers.size() == 1 && headers[0].name == "X-Connector-Fixture" &&
	       headers[0].value == "exact-duplicate-repositories";
}

bool HasControlledExactSelector(const CompiledOperation &operation, const CompiledPredicateMapping &mapping) {
	const auto &selector = operation.selector;
	if (operation.fallback) {
		return selector.RequiredInputs().empty() && selector.AnyInputSets().empty() &&
		       selector.ForbiddenInputs().empty() && selector.Priority() == 0;
	}
	const bool required_binding = selector.RequiredInputs().size() == 1 &&
	                              selector.RequiredInputs()[0] == mapping.RemoteInputName() &&
	                              selector.AnyInputSets().empty();
	const bool alternative_binding = selector.RequiredInputs().empty() && selector.AnyInputSets().size() == 1 &&
	                                 selector.AnyInputSets()[0].size() == 1 &&
	                                 selector.AnyInputSets()[0][0] == mapping.RemoteInputName();
	return (required_binding || alternative_binding) && selector.ForbiddenInputs().empty();
}

bool HasControlledExactProfile(const std::string &relation_name, const CompiledOperation &operation,
                               const CompiledAuthenticationPolicy &authentication,
                               const CompiledPredicateMapping &mapping) {
	const bool controlled_operation =
	    operation.name == "controlled_exact_repositories" || operation.name == "controlled_priority_exact_repositories";
	return relation_name == "controlled_exact_repositories" && controlled_operation &&
	       operation.cardinality == CompiledOperationCardinality::ZERO_TO_MANY &&
	       operation.Protocol() == CompiledProtocol::REST && operation.Rest().method == CompiledHttpMethod::GET &&
	       operation.Rest().replay_safety == CompiledReplaySafety::SAFE && !operation.Rest().retry_enabled &&
	       HasControlledExactOrigin(operation.Rest().request.origin) &&
	       operation.Rest().request.path == "/fixtures/exact-repositories" &&
	       operation.Rest().request.query_parameters.empty() && HasControlledExactHeaders(operation) &&
	       operation.Rest().response_source == CompiledResponseSource::ROOT_ARRAY &&
	       operation.Rest().records_extractor == "$" &&
	       operation.Rest().pagination.Strategy() == CompiledPaginationStrategy::DISABLED &&
	       authentication.Requirement() == CompiledCredentialRequirement::NONE &&
	       authentication.LogicalCredential().empty() &&
	       authentication.Authenticator() == CompiledAuthenticator::NONE &&
	       authentication.Placement() == CompiledCredentialPlacement::NONE && authentication.Destination() == nullptr &&
	       mapping.ColumnName() == "visibility" && mapping.Operator() == CompiledPredicateOperator::EQUALS &&
	       mapping.Literal() == CompiledPredicateLiteral::VARCHAR_PRIVATE &&
	       mapping.OperationName() == operation.name &&
	       mapping.InputPlacement() == CompiledPredicateInputPlacement::REST_QUERY_PARAMETER &&
	       (mapping.RemoteInputName() == "visibility" || mapping.RemoteInputName() == "repository_visibility") &&
	       mapping.EncodedRemoteValue() == "private" && mapping.Accuracy() == CompiledPredicateAccuracy::EXACT &&
	       mapping.BaseDomain() == CompiledPredicateBaseDomain::CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES &&
	       mapping.OccurrencePreservation() ==
	           CompiledPredicateOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES &&
	       mapping.EncodingCapability() == CompiledPredicateEncodingCapability::SINGLE_POSITIVE_REST_QUERY_INPUT &&
	       HasControlledExactSelector(operation, mapping);
}

} // namespace

const char *PredicateProofIdentityName(CompiledPredicateProofIdentity value) {
	switch (value) {
	case CompiledPredicateProofIdentity::GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY:
		return "github_rest_2022_11_28_repository_visibility";
	case CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY:
		return "controlled_exact_duplicate_repository_visibility";
	}
	throw std::invalid_argument("compiled predicate mapping contains an unknown proof identity");
}

const char *PredicateBaseDomainName(CompiledPredicateBaseDomain value) {
	switch (value) {
	case CompiledPredicateBaseDomain::GITHUB_AUTHENTICATED_REPOSITORY_OCCURRENCES:
		return "github_authenticated_repository_occurrences";
	case CompiledPredicateBaseDomain::CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES:
		return "controlled_duplicate_repository_occurrences";
	}
	throw std::invalid_argument("compiled predicate mapping contains an unknown base-domain identity");
}

const char *PredicateOccurrencePreservationName(CompiledPredicateOccurrencePreservation value) {
	switch (value) {
	case CompiledPredicateOccurrencePreservation::PRESERVES_ALL_MATCHING_BASE_OCCURRENCES:
		return "all_matching_base_occurrences";
	case CompiledPredicateOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES:
		return "exact_matching_base_occurrences";
	}
	throw std::invalid_argument("compiled predicate mapping contains an unknown occurrence guarantee");
}

const char *PredicateEncodingCapabilityName(CompiledPredicateEncodingCapability value) {
	switch (value) {
	case CompiledPredicateEncodingCapability::SINGLE_POSITIVE_REST_QUERY_INPUT:
		return "single_positive_rest_query_input";
	}
	throw std::invalid_argument("compiled predicate mapping contains an unknown encoding capability");
}

void ValidatePredicateProofProfile(const std::string &relation_name, const CompiledOperation &operation,
                                   const CompiledAuthenticationPolicy &authentication,
                                   const CompiledPredicateMapping &mapping) {
	switch (mapping.ProofIdentity()) {
	case CompiledPredicateProofIdentity::GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY:
		if (HasInstalledGithubProfile(relation_name, operation, authentication, mapping)) {
			return;
		}
		break;
	case CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY:
		if (HasControlledExactProfile(relation_name, operation, authentication, mapping)) {
			return;
		}
		break;
	}
	throw std::invalid_argument("compiled predicate mapping does not match its accepted proof profile");
}

} // namespace internal
} // namespace duckdb_api
