#include "duckdb_api/package_compatibility.hpp"

#include "duckdb_api/internal/connector/operation_selector_declaration.hpp"

#include <cstddef>
#include <utility>

namespace duckdb_api {

namespace {

bool SameOrigin(const CompiledHttpOrigin &left, const CompiledHttpOrigin &right) {
	return left.scheme == right.scheme && left.host.Value() == right.host.Value() && left.port == right.port;
}

bool SameQuery(const std::vector<CompiledQueryParameter> &left, const std::vector<CompiledQueryParameter> &right) {
	if (left.size() != right.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.size(); index++) {
		if (left[index].name != right[index].name || left[index].encoded_value != right[index].encoded_value ||
		    left[index].source != right[index].source || left[index].source_id != right[index].source_id ||
		    left[index].omit_when_unbound != right[index].omit_when_unbound ||
		    left[index].omit_when_null != right[index].omit_when_null) {
			return false;
		}
	}
	return true;
}

bool SameHeaders(const std::vector<CompiledHttpHeader> &left, const std::vector<CompiledHttpHeader> &right) {
	if (left.size() != right.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.size(); index++) {
		if (left[index].name != right[index].name || left[index].value != right[index].value) {
			return false;
		}
	}
	return true;
}

bool SamePath(const CompiledGraphqlResponsePath &left, const CompiledGraphqlResponsePath &right) {
	return left.segments == right.segments;
}

bool SamePagination(const CompiledPagination &left, const CompiledPagination &right) {
	if (left.Strategy() != right.Strategy() || left.SupportsTotal() != right.SupportsTotal() ||
	    left.SupportsResume() != right.SupportsResume()) {
		return false;
	}
	if (left.Strategy() == CompiledPaginationStrategy::DISABLED) {
		return true;
	}
	return left.Dependency() == right.Dependency() && left.Consistency() == right.Consistency() &&
	       left.LinkRelation() == right.LinkRelation() && left.TargetScope() == right.TargetScope() &&
	       left.PageSizeParameter() == right.PageSizeParameter() && left.PageSize() == right.PageSize() &&
	       left.PageNumberParameter() == right.PageNumberParameter() && left.FirstPage() == right.FirstPage() &&
	       left.PageIncrement() == right.PageIncrement() && left.MaxPagesPerScan() == right.MaxPagesPerScan();
}

bool SameRest(const CompiledRestOperation &left, const CompiledRestOperation &right) {
	return left.method == right.method && left.replay_safety == right.replay_safety &&
	       left.retry_enabled == right.retry_enabled && SamePagination(left.pagination, right.pagination) &&
	       SameOrigin(left.request.origin, right.request.origin) && left.request.path == right.request.path &&
	       SameQuery(left.request.query_parameters, right.request.query_parameters) &&
	       SameHeaders(left.request.headers, right.request.headers) && left.response_source == right.response_source &&
	       left.records_extractor == right.records_extractor;
}

bool SameGraphqlVariables(const std::vector<CompiledGraphqlVariable> &left,
                          const std::vector<CompiledGraphqlVariable> &right) {
	if (left.size() != right.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.size(); index++) {
		if (left[index].name != right[index].name || left[index].type != right[index].type ||
		    left[index].source != right[index].source || left[index].integer_value != right[index].integer_value) {
			return false;
		}
	}
	return true;
}

bool SameGraphqlColumns(const std::vector<CompiledGraphqlResultColumn> &left,
                        const std::vector<CompiledGraphqlResultColumn> &right) {
	if (left.size() != right.size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.size(); index++) {
		if (left[index].name != right[index].name || left[index].scalar_kind != right[index].scalar_kind ||
		    left[index].nullable != right[index].nullable ||
		    !SamePath(left[index].response_path, right[index].response_path)) {
			return false;
		}
	}
	return true;
}

bool SameGraphqlResponse(const CompiledGraphqlResponse &left, const CompiledGraphqlResponse &right) {
	return SamePath(left.nodes, right.nodes) && SamePath(left.errors, right.errors) &&
	       SamePath(left.page_info, right.page_info) && left.partial_data == right.partial_data;
}

bool SameGraphqlCursor(const CompiledGraphqlCursorPagination &left, const CompiledGraphqlCursorPagination &right) {
	return left.direction == right.direction && left.dependency == right.dependency &&
	       left.consistency == right.consistency && left.supports_total == right.supports_total &&
	       left.supports_resume == right.supports_resume && left.max_concurrent_pages == right.max_concurrent_pages &&
	       left.page_size_variable == right.page_size_variable && left.page_size == right.page_size &&
	       left.cursor_variable == right.cursor_variable && SamePath(left.has_next_page, right.has_next_page) &&
	       SamePath(left.end_cursor, right.end_cursor) && left.max_pages_per_scan == right.max_pages_per_scan;
}

bool SameGraphql(const CompiledGraphqlOperation &left, const CompiledGraphqlOperation &right) {
	return left.document_identity == right.document_identity && left.document == right.document &&
	       left.digest_algorithm == right.digest_algorithm && left.document_digest == right.document_digest &&
	       SameOrigin(left.endpoint_origin, right.endpoint_origin) && left.endpoint_path == right.endpoint_path &&
	       SameHeaders(left.headers, right.headers) && SameGraphqlVariables(left.variables, right.variables) &&
	       SameGraphqlColumns(left.result_columns, right.result_columns) &&
	       SameGraphqlResponse(left.response, right.response) && SameGraphqlCursor(left.cursor, right.cursor) &&
	       left.max_document_bytes == right.max_document_bytes &&
	       left.max_serialized_request_body_bytes_per_request == right.max_serialized_request_body_bytes_per_request &&
	       left.max_serialized_request_body_bytes_per_scan == right.max_serialized_request_body_bytes_per_scan &&
	       left.retry_enabled == right.retry_enabled && left.cache_enabled == right.cache_enabled &&
	       left.providers_enabled == right.providers_enabled;
}

bool SameOperation(const CompiledOperation &left, const CompiledOperation &right) {
	if (left.name != right.name || left.fallback != right.fallback || left.cardinality != right.cardinality ||
	    left.Protocol() != right.Protocol() ||
	    !internal::SameOperationSelectorStructure(left.selector, right.selector)) {
		return false;
	}
	switch (left.Protocol()) {
	case CompiledProtocol::REST:
		return SameRest(left.Rest(), right.Rest());
	case CompiledProtocol::GRAPHQL:
		return SameGraphql(left.Graphql(), right.Graphql());
	}
	return false;
}

bool SameScalar(const CompiledScalarValue &left, const CompiledScalarValue &right) {
	if (left.Type() != right.Type() || left.IsNull() != right.IsNull()) {
		return false;
	}
	if (left.IsNull()) {
		return true;
	}
	switch (left.Type()) {
	case CompiledScalarType::BOOLEAN:
		return left.Boolean() == right.Boolean();
	case CompiledScalarType::BIGINT:
		return left.Bigint() == right.Bigint();
	case CompiledScalarType::VARCHAR:
		return left.Varchar() == right.Varchar();
	}
	return false;
}

bool SameDefault(const CompiledInputDefault &left, const CompiledInputDefault &right) {
	return left.HasDefault() == right.HasDefault() && (!left.HasDefault() || SameScalar(left.Value(), right.Value()));
}

bool SameInput(const CompiledRelationInput &left, const CompiledRelationInput &right) {
	return left.Name() == right.Name() && left.Type() == right.Type() && left.Nullable() == right.Nullable() &&
	       SameDefault(left.Default(), right.Default());
}

bool SameColumn(const CompiledColumn &left, const CompiledColumn &right) {
	return left.name == right.name && left.ScalarType() == right.ScalarType() && left.nullable == right.nullable &&
	       left.extractor == right.extractor;
}

bool SamePredicate(const CompiledPredicateMapping &left, const CompiledPredicateMapping &right) {
	return left.ColumnName() == right.ColumnName() && left.Operator() == right.Operator() &&
	       left.Literal() == right.Literal() && SameScalar(left.TypedLiteral(), right.TypedLiteral()) &&
	       left.OperationName() == right.OperationName() && left.InputPlacement() == right.InputPlacement() &&
	       left.RemoteInputName() == right.RemoteInputName() &&
	       left.EncodedRemoteValue() == right.EncodedRemoteValue() && left.Accuracy() == right.Accuracy() &&
	       left.ProofIdentity() == right.ProofIdentity() && left.BaseDomain() == right.BaseDomain() &&
	       left.MatchingFixture() == right.MatchingFixture() &&
	       left.FalseOrNullFixture() == right.FalseOrNullFixture() &&
	       left.DuplicatesFixture() == right.DuplicatesFixture() &&
	       left.OccurrencePreservation() == right.OccurrencePreservation() &&
	       left.EncodingCapability() == right.EncodingCapability();
}

bool SameAuthentication(const CompiledAuthenticationPolicy &left, const CompiledAuthenticationPolicy &right) {
	if (left.Requirement() != right.Requirement() || left.LogicalCredential() != right.LogicalCredential() ||
	    left.Authenticator() != right.Authenticator() || left.Placement() != right.Placement()) {
		return false;
	}
	const auto *left_destination = left.Destination();
	const auto *right_destination = right.Destination();
	if ((left_destination == nullptr) != (right_destination == nullptr)) {
		return false;
	}
	return left_destination == nullptr || SameOrigin(*left_destination, *right_destination);
}

bool SameResources(const CompiledResourceCeilings &left, const CompiledResourceCeilings &right) {
	if (left.HasResponseByteNarrowing() != right.HasResponseByteNarrowing() ||
	    left.MaxRecordsPerPage() != right.MaxRecordsPerPage() ||
	    left.MaxRecordsPerScan() != right.MaxRecordsPerScan() ||
	    left.MaxExtractedStringBytes() != right.MaxExtractedStringBytes()) {
		return false;
	}
	return !left.HasResponseByteNarrowing() || (left.MaxResponseBytesPerPage() == right.MaxResponseBytesPerPage() &&
	                                            left.MaxResponseBytesPerScan() == right.MaxResponseBytesPerScan());
}

bool SameRelation(const CompiledRelation &left, const CompiledRelation &right) {
	if (left.Name() != right.Name() || left.Columns().size() != right.Columns().size() ||
	    left.Inputs().size() != right.Inputs().size() ||
	    left.PredicateMappings().size() != right.PredicateMappings().size() ||
	    left.Operations().size() != right.Operations().size() ||
	    !SameAuthentication(left.Authentication(), right.Authentication()) ||
	    !SameResources(left.ResourceCeilings(), right.ResourceCeilings())) {
		return false;
	}
	for (std::size_t index = 0; index < left.Columns().size(); index++) {
		if (!SameColumn(left.Columns()[index], right.Columns()[index])) {
			return false;
		}
	}
	for (std::size_t index = 0; index < left.Inputs().size(); index++) {
		if (!SameInput(left.Inputs()[index], right.Inputs()[index])) {
			return false;
		}
	}
	for (std::size_t index = 0; index < left.PredicateMappings().size(); index++) {
		if (!SamePredicate(left.PredicateMappings()[index], right.PredicateMappings()[index])) {
			return false;
		}
	}
	for (std::size_t index = 0; index < left.Operations().size(); index++) {
		if (!SameOperation(left.Operations()[index], right.Operations()[index])) {
			return false;
		}
	}
	return true;
}

bool SameNetworkPolicy(const CompiledNetworkPolicy &left, const CompiledNetworkPolicy &right) {
	return left.allowed_schemes == right.allowed_schemes && left.allowed_hosts == right.allowed_hosts &&
	       left.redirects_enabled == right.redirects_enabled &&
	       left.private_addresses_enabled == right.private_addresses_enabled &&
	       left.link_local_addresses_enabled == right.link_local_addresses_enabled &&
	       left.loopback_addresses_enabled == right.loopback_addresses_enabled &&
	       left.max_response_bytes == right.max_response_bytes;
}

bool SameDescriptor(const CompiledConnector &left, const CompiledConnector &right) {
	if (left.Origin() != right.Origin() || !SameNetworkPolicy(left.NetworkPolicy(), right.NetworkPolicy()) ||
	    left.Relations().size() != right.Relations().size()) {
		return false;
	}
	for (std::size_t index = 0; index < left.Relations().size(); index++) {
		if (!SameRelation(left.Relations()[index], right.Relations()[index])) {
			return false;
		}
	}
	return true;
}

bool IsAppendOnlyDescriptor(const CompiledConnector &active, const CompiledConnector &candidate) {
	if (active.Origin() != candidate.Origin() ||
	    !SameNetworkPolicy(active.NetworkPolicy(), candidate.NetworkPolicy()) ||
	    candidate.Relations().size() <= active.Relations().size()) {
		return false;
	}
	for (std::size_t index = 0; index < active.Relations().size(); index++) {
		if (!SameRelation(active.Relations()[index], candidate.Relations()[index])) {
			return false;
		}
	}
	return true;
}

bool IsSuccessful(PackageReloadClassification classification) {
	switch (classification) {
	case PackageReloadClassification::EXACT_NO_OP:
	case PackageReloadClassification::COMPATIBLE_PROVENANCE_PATCH:
	case PackageReloadClassification::COMPATIBLE_PROVENANCE_MINOR:
	case PackageReloadClassification::COMPATIBLE_APPEND_ONLY_MINOR:
		return true;
	case PackageReloadClassification::REJECTED_PACKAGE_IDENTITY:
	case PackageReloadClassification::INCOMPATIBLE_RELOAD:
		return false;
	}
	return false;
}

} // namespace

PackageReloadDecision::PackageReloadDecision(PackageReloadClassification classification_p, std::string connector_id_p)
    : classification(classification_p), connector_id(std::move(connector_id_p)) {
}

PackageReloadClassification PackageReloadDecision::Classification() const {
	return classification;
}

bool PackageReloadDecision::IsCompatible() const {
	return IsSuccessful(classification);
}

bool PackageReloadDecision::Changed() const {
	return classification == PackageReloadClassification::COMPATIBLE_PROVENANCE_PATCH ||
	       classification == PackageReloadClassification::COMPATIBLE_PROVENANCE_MINOR ||
	       classification == PackageReloadClassification::COMPATIBLE_APPEND_ONLY_MINOR;
}

bool PackageReloadDecision::HasDiagnostic() const {
	return !IsCompatible();
}

const char *PackageReloadDecision::DiagnosticCode() const {
	switch (classification) {
	case PackageReloadClassification::REJECTED_PACKAGE_IDENTITY:
		return "DUCKDB_API_PACKAGE_IDENTITY";
	case PackageReloadClassification::INCOMPATIBLE_RELOAD:
		return "DUCKDB_API_INCOMPATIBLE_RELOAD";
	case PackageReloadClassification::EXACT_NO_OP:
	case PackageReloadClassification::COMPATIBLE_PROVENANCE_PATCH:
	case PackageReloadClassification::COMPATIBLE_PROVENANCE_MINOR:
	case PackageReloadClassification::COMPATIBLE_APPEND_ONLY_MINOR:
		return "";
	}
	return "";
}

const char *PackageReloadDecision::DiagnosticPhase() const {
	return HasDiagnostic() ? "compatibility" : "";
}

const std::string &PackageReloadDecision::ConnectorId() const {
	return connector_id;
}

PackageReloadDecision ClassifyPackageReload(const CompiledPackageGeneration &active,
                                            const CompiledPackageGeneration &candidate) {
	const auto &active_identity = active.Identity();
	const auto &candidate_identity = candidate.Identity();
	if (active_identity.SpecIdentifier() != candidate_identity.SpecIdentifier() ||
	    active_identity.ConnectorId() != candidate_identity.ConnectorId()) {
		return PackageReloadDecision(PackageReloadClassification::INCOMPATIBLE_RELOAD,
		                             candidate_identity.ConnectorId());
	}

	const auto active_version = PackageSemVer::Parse(active_identity.PackageVersion());
	const auto candidate_version = PackageSemVer::Parse(candidate_identity.PackageVersion());
	const bool same_descriptor = SameDescriptor(active.Connector(), candidate.Connector());
	if (active_version.Compare(candidate_version) == 0 &&
	    active_identity.PackageDigest() == candidate_identity.PackageDigest() && same_descriptor) {
		return PackageReloadDecision(PackageReloadClassification::EXACT_NO_OP, active_identity.ConnectorId());
	}
	if (candidate_version.Compare(active_version) <= 0) {
		return PackageReloadDecision(PackageReloadClassification::REJECTED_PACKAGE_IDENTITY,
		                             active_identity.ConnectorId());
	}
	if (candidate_version.Major() != active_version.Major()) {
		return PackageReloadDecision(PackageReloadClassification::INCOMPATIBLE_RELOAD, active_identity.ConnectorId());
	}
	if (same_descriptor) {
		const auto classification = candidate_version.Minor() == active_version.Minor()
		                                ? PackageReloadClassification::COMPATIBLE_PROVENANCE_PATCH
		                                : PackageReloadClassification::COMPATIBLE_PROVENANCE_MINOR;
		return PackageReloadDecision(classification, active_identity.ConnectorId());
	}
	if (candidate_version.Minor() > active_version.Minor() &&
	    IsAppendOnlyDescriptor(active.Connector(), candidate.Connector())) {
		return PackageReloadDecision(PackageReloadClassification::COMPATIBLE_APPEND_ONLY_MINOR,
		                             active_identity.ConnectorId());
	}
	return PackageReloadDecision(PackageReloadClassification::INCOMPATIBLE_RELOAD, active_identity.ConnectorId());
}

} // namespace duckdb_api
