#include "duckdb_api/scan_plan.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace duckdb_api {

namespace {

const char *UrlSchemeName(CompiledUrlScheme scheme) {
	switch (scheme) {
	case CompiledUrlScheme::HTTP:
		return "http";
	case CompiledUrlScheme::HTTPS:
		return "https";
	}
	throw std::logic_error("compiled connector contains an unknown URL scheme");
}

PlannedUrlScheme PlanUrlScheme(CompiledUrlScheme scheme) {
	switch (scheme) {
	case CompiledUrlScheme::HTTP:
		return PlannedUrlScheme::HTTP;
	case CompiledUrlScheme::HTTPS:
		return PlannedUrlScheme::HTTPS;
	}
	throw std::logic_error("compiled connector contains an unknown URL scheme");
}

PlannedProtocol PlanProtocol(CompiledProtocol protocol) {
	switch (protocol) {
	case CompiledProtocol::REST:
		return PlannedProtocol::REST;
	}
	throw std::logic_error("compiled relation contains an unsupported protocol");
}

PlannedHttpMethod PlanMethod(CompiledHttpMethod method) {
	switch (method) {
	case CompiledHttpMethod::GET:
		return PlannedHttpMethod::GET;
	}
	throw std::logic_error("compiled relation contains an unsupported HTTP method");
}

PlannedReplaySafety PlanReplaySafety(CompiledReplaySafety replay_safety) {
	switch (replay_safety) {
	case CompiledReplaySafety::SAFE:
		return PlannedReplaySafety::SAFE;
	}
	throw std::logic_error("compiled relation contains an unsupported replay-safety declaration");
}

PlannedCardinality PlanCardinality(CompiledOperationCardinality cardinality) {
	switch (cardinality) {
	case CompiledOperationCardinality::ZERO_TO_MANY:
		return PlannedCardinality::ZERO_TO_MANY;
	case CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS:
		return PlannedCardinality::EXACTLY_ONE_ON_SUCCESS;
	}
	throw std::logic_error("compiled relation contains an unsupported source cardinality");
}

PlannedResponseSource PlanResponseSource(CompiledResponseSource source) {
	switch (source) {
	case CompiledResponseSource::JSON_PATH_MANY:
		return PlannedResponseSource::JSON_PATH_MANY;
	case CompiledResponseSource::ROOT_OBJECT:
		return PlannedResponseSource::ROOT_OBJECT;
	}
	throw std::logic_error("compiled relation contains an unsupported response source");
}

BaseDomain PlanBaseDomain(CompiledResponseSource source) {
	switch (source) {
	case CompiledResponseSource::JSON_PATH_MANY:
		return BaseDomain::JSON_PATH_RECORDS;
	case CompiledResponseSource::ROOT_OBJECT:
		return BaseDomain::SUCCESSFUL_ROOT_OBJECT;
	}
	throw std::logic_error("compiled relation contains an unsupported response source");
}

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
	if (operation.response_source == CompiledResponseSource::JSON_PATH_MANY) {
		if (operation.cardinality != CompiledOperationCardinality::ZERO_TO_MANY ||
		    operation.records_extractor.empty() || operation.records_extractor == "$") {
			throw std::logic_error("JSON-path response contains contradictory source cardinality or extraction");
		}
		return;
	}
	if (operation.response_source == CompiledResponseSource::ROOT_OBJECT) {
		if (operation.cardinality != CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS ||
		    operation.records_extractor != "$" || ceilings.max_records != 1) {
			throw std::logic_error("root-object response contains contradictory cardinality, extraction, or budget");
		}
		return;
	}
	throw std::logic_error("selected relation contains an unsupported response source");
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

void ValidateOperation(const CompiledRelation &relation, const CompiledNetworkPolicy &network_policy) {
	const auto &operation = relation.Operation();
	const auto &ceilings = relation.ResourceCeilings();
	if (operation.name.empty() || !operation.fallback || operation.retry_enabled || operation.pagination_enabled ||
	    operation.request.path.empty() || operation.request.path.front() != '/' || ceilings.max_records == 0 ||
	    ceilings.max_extracted_string_bytes == 0) {
		throw std::logic_error("selected relation contains an unsupported base operation or resource declaration");
	}
	(void)PlanProtocol(operation.protocol);
	(void)PlanMethod(operation.method);
	(void)PlanReplaySafety(operation.replay_safety);
	(void)PlanCardinality(operation.cardinality);
	(void)PlanResponseSource(operation.response_source);
	ValidateSourceShape(operation, ceilings);
	ValidateExecutableOrigin(operation.request.origin, network_policy);
}

void ValidateRequest(const CompiledConnector &connector, const CompiledRelation &relation, const ScanRequest &request) {
	if (request.connector_name != connector.ConnectorName() || request.relation_name != relation.Name() ||
	    !request.explicit_inputs.empty() || request.projected_columns != ProjectedColumnNames(relation.Columns()) ||
	    request.predicate != "TRUE" || !request.orderings.empty() || request.has_limit || request.has_offset ||
	    !request.capabilities.HasConservativeRelationalProfile()) {
		throw std::logic_error("planner received a non-conservative scan request for the selected relation");
	}
}

void ValidateAuthentication(const CompiledRelation &relation, const ScanRequest &request) {
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
	    authentication.Destination() == nullptr ||
	    !OriginsEqual(*authentication.Destination(), relation.Operation().request.origin)) {
		throw std::logic_error("authenticated relation contains a contradictory authentication policy");
	}
	if (!request.secret_reference.IsPresent()) {
		throw std::logic_error("authenticated relation is missing its logical secret reference");
	}
	if (!request.capabilities.secret_manager) {
		throw std::logic_error("authenticated relation requires unavailable Secret Manager capability");
	}
}

} // namespace

ScanPlan BuildConservativeScanPlan(const CompiledConnector &connector, const ScanRequest &request) {
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
		throw std::logic_error("planner could not select the exact requested relation");
	}

	ValidateSchema(*relation);
	ValidateOperation(*relation, connector.NetworkPolicy());
	ValidateRequest(connector, *relation, request);
	ValidateAuthentication(*relation, request);

	ScanPlan result;
	result.connector_name = connector.ConnectorName();
	result.connector_version = connector.Version();
	result.relation_name = relation->Name();
	result.source_snapshot = relation->Snapshot();
	result.domain = PlanBaseDomain(relation->Operation().response_source);

	const auto &operation = relation->Operation();
	result.operation.operation_name = operation.name;
	result.operation.protocol = PlanProtocol(operation.protocol);
	result.operation.method = PlanMethod(operation.method);
	result.operation.cardinality = PlanCardinality(operation.cardinality);
	result.operation.replay_safety = PlanReplaySafety(operation.replay_safety);
	result.operation.origin = {PlanUrlScheme(operation.request.origin.scheme), operation.request.origin.host.Value(),
	                           operation.request.origin.port};
	result.operation.path = operation.request.path;
	for (const auto &query : operation.request.query_parameters) {
		result.operation.query_parameters.push_back({query.name, query.encoded_value});
	}
	for (const auto &header : operation.request.headers) {
		result.operation.headers.push_back({header.name, header.value});
	}
	result.operation.response_source = PlanResponseSource(operation.response_source);
	result.operation.records_extractor = operation.records_extractor;

	for (const auto &column : relation->Columns()) {
		result.output_columns.push_back({column.name, column.logical_type, column.nullable, column.extractor});
	}
	result.remote_predicate = PlannedPredicate::TRUE_FOR_BASE_DOMAIN;
	result.residual_predicate = PlannedPredicate::TRUE_FOR_BASE_DOMAIN;
	result.residual_owner = RelationalOwner::DUCKDB;
	result.ownership = {RelationalOwner::DUCKDB, RelationalOwner::DUCKDB, RelationalOwner::DUCKDB,
	                    RelationalOwner::DUCKDB};
	result.remote_ordering = RelationalDelegation::NONE;
	result.runtime_ordering = RelationalDelegation::NONE;
	result.remote_limit = RelationalDelegation::NONE;
	result.remote_offset = RelationalDelegation::NONE;
	result.runtime_limit = RelationalDelegation::NONE;
	result.runtime_offset = RelationalDelegation::NONE;
	result.pagination = FeatureState::DISABLED;
	result.providers = FeatureState::DISABLED;
	result.retry = FeatureState::DISABLED;
	result.cache = FeatureState::DISABLED;
	result.secret_reference = request.secret_reference;
	const auto &authentication = relation->Authentication();
	if (authentication.Requirement() == CompiledCredentialRequirement::NONE) {
		result.authentication = FeatureState::DISABLED;
	} else {
		result.authentication = FeatureState::ENABLED;
		result.authentication_obligation.requirement = PlannedCredentialRequirement::REQUIRED;
		result.authentication_obligation.logical_credential = authentication.LogicalCredential();
		result.authentication_obligation.authenticator = PlannedAuthenticator::BEARER;
		result.authentication_obligation.placement = PlannedCredentialPlacement::AUTHORIZATION_HEADER;
		result.authentication_obligation.has_destination = true;
		const auto &destination = *authentication.Destination();
		result.authentication_obligation.destination = {PlanUrlScheme(destination.scheme), destination.host.Value(),
		                                                destination.port};
	}

	// The selected plan narrows catalog-wide network policy to the one operation
	// origin. It never grants another relation's host or scheme to this scan.
	result.network = {{UrlSchemeName(operation.request.origin.scheme)},
	                  {operation.request.origin.host.Value()},
	                  false,
	                  false,
	                  false,
	                  connector.NetworkPolicy().loopback_addresses_enabled};
	const auto &ceilings = relation->ResourceCeilings();
	result.budgets = {HOST_MAX_REQUEST_ATTEMPTS,
	                  std::min(connector.NetworkPolicy().max_response_bytes, HOST_MAX_RESPONSE_BYTES),
	                  HOST_MAX_HEADER_BYTES,
	                  HOST_MAX_DECOMPRESSED_BYTES,
	                  std::min(ceilings.max_records, HOST_MAX_DECODED_RECORDS),
	                  std::min(ceilings.max_extracted_string_bytes, HOST_MAX_EXTRACTED_STRING_BYTES),
	                  HOST_MAX_JSON_NESTING,
	                  HOST_MAX_DECODED_MEMORY_BYTES,
	                  OUTPUT_BATCH_ROWS,
	                  MAX_EXECUTION_MILLISECONDS,
	                  HOST_MAX_CONCURRENCY};
	if (!result.budgets.IsWithinLiveRestBounds()) {
		throw std::logic_error("selected relation produced an invalid effective resource envelope");
	}

	if (result.domain == BaseDomain::SUCCESSFUL_ROOT_OBJECT) {
		result.classification_reason =
		    "successful root object defines exactly one base row; source cardinality is not a limit; DuckDB retains "
		    "all relational operators";
	} else {
		result.classification_reason =
		    "selected JSON-path records define the complete base domain; DuckDB retains all relational operators";
	}
	return result;
}

} // namespace duckdb_api
