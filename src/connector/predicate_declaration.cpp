#include "duckdb_api/internal/connector/predicate_declaration.hpp"

#include <ostream>
#include <stdexcept>
#include <utility>

namespace duckdb_api {

namespace {

bool IsAsciiLower(char value) {
	return value >= 'a' && value <= 'z';
}

bool IsAsciiDigit(char value) {
	return value >= '0' && value <= '9';
}

bool IsIdentifier(const std::string &value) {
	if (value.empty() || !IsAsciiLower(value.front())) {
		return false;
	}
	for (const auto character : value) {
		if (!IsAsciiLower(character) && !IsAsciiDigit(character) && character != '_') {
			return false;
		}
	}
	return true;
}

bool IsSafeEncodedScalar(const std::string &value) {
	return !value.empty() && value.find_first_of("&=?#\r\n") == std::string::npos;
}

const char *OperatorName(CompiledPredicateOperator value) {
	switch (value) {
	case CompiledPredicateOperator::EQUALS:
		return "equals";
	}
	throw std::invalid_argument("compiled predicate mapping contains an unknown operator");
}

const char *LiteralName(CompiledPredicateLiteral value) {
	switch (value) {
	case CompiledPredicateLiteral::VARCHAR_PRIVATE:
		return "varchar:private";
	}
	throw std::invalid_argument("compiled predicate mapping contains an unknown typed literal");
}

const char *PlacementName(CompiledPredicateInputPlacement value) {
	switch (value) {
	case CompiledPredicateInputPlacement::REST_QUERY_PARAMETER:
		return "rest_query";
	}
	throw std::invalid_argument("compiled predicate mapping contains an unknown input placement");
}

const char *AccuracyName(CompiledPredicateAccuracy value) {
	switch (value) {
	case CompiledPredicateAccuracy::EXACT:
		return "exact";
	case CompiledPredicateAccuracy::SUPERSET:
		return "superset";
	}
	throw std::invalid_argument("compiled predicate mapping contains an unknown accuracy");
}

const char *EvidenceName(CompiledPredicateEvidence value) {
	switch (value) {
	case CompiledPredicateEvidence::GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY:
		return "github_rest_2022_11_28_repository_visibility";
	}
	throw std::invalid_argument("compiled predicate mapping contains an unknown evidence identity");
}

const CompiledColumn *FindColumn(const std::vector<CompiledColumn> &columns, const std::string &name) {
	for (const auto &column : columns) {
		if (column.name == name) {
			return &column;
		}
	}
	return nullptr;
}

bool HasFixedQueryField(const CompiledOperation &operation, const std::string &name) {
	for (const auto &parameter : operation.request.query_parameters) {
		if (parameter.name == name) {
			return true;
		}
	}
	return false;
}

bool HasCanonicalGithubOrigin(const CompiledRestOrigin &origin) {
	return origin.scheme == CompiledUrlScheme::HTTPS && origin.host.Value() == "api.github.com" && origin.port == 443;
}

bool HasCanonicalRepositoryQuery(const CompiledOperation &operation) {
	const auto &query = operation.request.query_parameters;
	return query.size() == 2 && query[0].name == "per_page" && query[0].encoded_value == "100" &&
	       query[1].name == "page" && query[1].encoded_value == "1";
}

bool HasCanonicalGithubHeaders(const CompiledOperation &operation) {
	const auto &headers = operation.request.headers;
	return headers.size() == 3 && headers[0].name == "Accept" && headers[0].value == "application/vnd.github+json" &&
	       headers[1].name == "User-Agent" && headers[1].value == "duckdb-api/0.6.0" &&
	       headers[2].name == "X-GitHub-Api-Version" && headers[2].value == "2022-11-28";
}

bool HasCanonicalRepositoryPagination(const CompiledOperation &operation) {
	const auto &pagination = operation.pagination;
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

bool HasAcceptedRepositoryOperationProfile(const std::string &relation_name, const CompiledOperation &operation,
                                           const CompiledAuthenticationPolicy &authentication) {
	return relation_name == "authenticated_repositories" && operation.name == "github_authenticated_repositories" &&
	       operation.fallback && operation.cardinality == CompiledOperationCardinality::ZERO_TO_MANY &&
	       operation.protocol == CompiledProtocol::REST && operation.method == CompiledHttpMethod::GET &&
	       operation.replay_safety == CompiledReplaySafety::SAFE && !operation.retry_enabled &&
	       HasCanonicalGithubOrigin(operation.request.origin) && operation.request.path == "/user/repos" &&
	       HasCanonicalRepositoryQuery(operation) && HasCanonicalGithubHeaders(operation) &&
	       operation.response_source == CompiledResponseSource::ROOT_ARRAY && operation.records_extractor == "$" &&
	       HasCanonicalRepositoryPagination(operation) && HasCanonicalRepositoryAuthentication(authentication);
}

bool SamePredicateShape(const CompiledPredicateMapping &left, const CompiledPredicateMapping &right) {
	return left.ColumnName() == right.ColumnName() && left.Operator() == right.Operator() &&
	       left.Literal() == right.Literal();
}

void ValidateAcceptedEvidenceProfile(const std::string &relation_name, const CompiledOperation &operation,
                                     const CompiledAuthenticationPolicy &authentication,
                                     const CompiledPredicateMapping &mapping) {
	(void)EvidenceName(mapping.Evidence());
	if (mapping.Evidence() != CompiledPredicateEvidence::GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY ||
	    mapping.ColumnName() != "visibility" || mapping.Operator() != CompiledPredicateOperator::EQUALS ||
	    mapping.Literal() != CompiledPredicateLiteral::VARCHAR_PRIVATE ||
	    mapping.OperationName() != "github_authenticated_repositories" ||
	    mapping.InputPlacement() != CompiledPredicateInputPlacement::REST_QUERY_PARAMETER ||
	    mapping.RemoteInputName() != "visibility" || mapping.EncodedRemoteValue() != "private" ||
	    mapping.Accuracy() != CompiledPredicateAccuracy::SUPERSET ||
	    !HasAcceptedRepositoryOperationProfile(relation_name, operation, authentication)) {
		throw std::invalid_argument("compiled predicate mapping does not match its accepted evidence profile");
	}
}

} // namespace

CompiledPredicateMapping::CompiledPredicateMapping(
    std::string column_name_p, CompiledPredicateOperator predicate_operator_p, CompiledPredicateLiteral literal_p,
    std::string operation_name_p, CompiledPredicateInputPlacement input_placement_p, std::string remote_input_name_p,
    std::string encoded_remote_value_p, CompiledPredicateAccuracy accuracy_p, CompiledPredicateEvidence evidence_p)
    : column_name(std::move(column_name_p)), predicate_operator(predicate_operator_p), literal(literal_p),
      operation_name(std::move(operation_name_p)), input_placement(input_placement_p),
      remote_input_name(std::move(remote_input_name_p)), encoded_remote_value(std::move(encoded_remote_value_p)),
      accuracy(accuracy_p), evidence(evidence_p) {
	if (!IsIdentifier(column_name) || !IsIdentifier(operation_name) || !IsIdentifier(remote_input_name) ||
	    !IsSafeEncodedScalar(encoded_remote_value)) {
		throw std::invalid_argument("compiled predicate mapping contains an invalid identifier or encoded value");
	}
	(void)OperatorName(predicate_operator);
	(void)LiteralName(literal);
	(void)PlacementName(input_placement);
	(void)AccuracyName(accuracy);
	(void)EvidenceName(evidence);
}

const std::string &CompiledPredicateMapping::ColumnName() const {
	return column_name;
}

CompiledPredicateOperator CompiledPredicateMapping::Operator() const {
	return predicate_operator;
}

CompiledPredicateLiteral CompiledPredicateMapping::Literal() const {
	return literal;
}

const std::string &CompiledPredicateMapping::OperationName() const {
	return operation_name;
}

CompiledPredicateInputPlacement CompiledPredicateMapping::InputPlacement() const {
	return input_placement;
}

const std::string &CompiledPredicateMapping::RemoteInputName() const {
	return remote_input_name;
}

const std::string &CompiledPredicateMapping::EncodedRemoteValue() const {
	return encoded_remote_value;
}

CompiledPredicateAccuracy CompiledPredicateMapping::Accuracy() const {
	return accuracy;
}

CompiledPredicateEvidence CompiledPredicateMapping::Evidence() const {
	return evidence;
}

namespace internal {

void ValidatePredicateMappings(const std::string &relation_name, const std::vector<CompiledColumn> &columns,
                               const CompiledOperation &operation, const CompiledAuthenticationPolicy &authentication,
                               const std::vector<CompiledPredicateMapping> &mappings) {
	for (std::size_t index = 0; index < mappings.size(); index++) {
		const auto &mapping = mappings[index];
		ValidateAcceptedEvidenceProfile(relation_name, operation, authentication, mapping);
		const auto *column = FindColumn(columns, mapping.ColumnName());
		if (column == nullptr || column->nullable || column->logical_type != "VARCHAR" ||
		    column->extractor != "$.visibility") {
			throw std::invalid_argument("compiled predicate mapping references an absent or incompatible column");
		}
		if (mapping.OperationName() != operation.name) {
			throw std::invalid_argument("compiled predicate mapping references a different operation");
		}
		const bool collides_with_pagination =
		    operation.pagination.Strategy() == CompiledPaginationStrategy::LINK_HEADER &&
		    (mapping.RemoteInputName() == operation.pagination.PageSizeParameter() ||
		     mapping.RemoteInputName() == operation.pagination.PageNumberParameter());
		if (HasFixedQueryField(operation, mapping.RemoteInputName()) || collides_with_pagination) {
			throw std::invalid_argument("compiled predicate mapping conflicts with a fixed or pagination query field");
		}
		if (mapping.RemoteInputName() == "visibility" && HasFixedQueryField(operation, "type")) {
			throw std::invalid_argument("compiled visibility mapping conflicts with a fixed legacy type field");
		}
		for (std::size_t other = index + 1; other < mappings.size(); other++) {
			if (SamePredicateShape(mapping, mappings[other])) {
				throw std::invalid_argument("compiled relation contains a duplicate predicate mapping");
			}
			if (mapping.RemoteInputName() == mappings[other].RemoteInputName()) {
				throw std::invalid_argument("compiled relation contains ambiguous predicate input bindings");
			}
		}
	}
}

void AppendPredicateMappings(std::ostream &result, const std::vector<CompiledPredicateMapping> &mappings) {
	for (std::size_t index = 0; index < mappings.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		const auto &mapping = mappings[index];
		result << "{column:" << mapping.ColumnName() << ",operator:" << OperatorName(mapping.Operator())
		       << ",literal:" << LiteralName(mapping.Literal()) << ",operation:" << mapping.OperationName()
		       << ",input:" << PlacementName(mapping.InputPlacement()) << ':' << mapping.RemoteInputName() << '='
		       << mapping.EncodedRemoteValue() << ",accuracy:" << AccuracyName(mapping.Accuracy())
		       << ",evidence:" << EvidenceName(mapping.Evidence()) << '}';
	}
}

} // namespace internal
} // namespace duckdb_api
