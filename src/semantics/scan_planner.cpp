#include "duckdb_api/scan_planner.hpp"

#include "scan_planner_internal.hpp"

#include <algorithm>
#include <stdexcept>

namespace duckdb_api {

class ScanPlanBuilder {
public:
	static ScanPlan Build(const CompiledConnector &connector, const ScanRequest &request);
};

ScanPlan ScanPlanBuilder::Build(const CompiledConnector &connector, const ScanRequest &request) {
	using namespace scan_planner_internal;

	const auto &relation = ValidateAndSelectRelation(connector, request);

	ScanPlan result;
	result.connector_name = connector.ConnectorName();
	result.connector_version = connector.Version();
	result.relation_name = relation.Name();
	result.source_snapshot = relation.Snapshot();
	result.domain = PlanBaseDomain(relation.Operation().response_source, relation.Operation().pagination.Strategy());

	const auto &operation = relation.Operation();
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

	for (const auto &column : relation.Columns()) {
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
	result.providers = FeatureState::DISABLED;
	result.retry = FeatureState::DISABLED;
	result.cache = FeatureState::DISABLED;
	result.secret_reference = request.secret_reference.IsPresent()
	                              ? PlannedSecretReference(request.secret_reference.Name())
	                              : PlannedSecretReference();
	const auto &authentication = relation.Authentication();
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
	const auto &ceilings = relation.ResourceCeilings();
	if (operation.pagination.Strategy() == CompiledPaginationStrategy::DISABLED) {
		const auto response_bytes =
		    ceilings.HasResponseByteNarrowing()
		        ? std::min(ceilings.MaxResponseBytesPerPage(), connector.NetworkPolicy().max_response_bytes)
		        : connector.NetworkPolicy().max_response_bytes;
		result.budgets = {HOST_MAX_REQUEST_ATTEMPTS,
		                  std::min(response_bytes, HOST_MAX_RESPONSE_BYTES),
		                  HOST_MAX_HEADER_BYTES,
		                  HOST_MAX_DECOMPRESSED_BYTES,
		                  std::min(ceilings.MaxRecordsPerPage(), HOST_MAX_DECODED_RECORDS),
		                  std::min(ceilings.MaxExtractedStringBytes(), HOST_MAX_EXTRACTED_STRING_BYTES),
		                  HOST_MAX_JSON_NESTING,
		                  HOST_MAX_DECODED_MEMORY_BYTES,
		                  OUTPUT_BATCH_ROWS,
		                  MAX_EXECUTION_MILLISECONDS,
		                  HOST_MAX_CONCURRENCY};
		if (!result.budgets.IsWithinLiveRestBounds()) {
			throw std::logic_error("selected relation produced an invalid single-response resource envelope");
		}
	} else {
		const auto &compiled_pagination = operation.pagination;
		result.pagination.strategy = PlannedPaginationStrategy::LINK_HEADER;
		result.pagination.dependency = PlanPageDependency(compiled_pagination.Dependency());
		result.pagination.consistency = PlanPageConsistency(compiled_pagination.Consistency());
		result.pagination.link_relation = PlanLinkRelation(compiled_pagination.LinkRelation());
		result.pagination.target_scope = PlanTargetScope(compiled_pagination.TargetScope());
		result.pagination.supports_total = compiled_pagination.SupportsTotal();
		result.pagination.supports_resume = compiled_pagination.SupportsResume();
		result.pagination.target = {{PlanUrlScheme(operation.request.origin.scheme),
		                             operation.request.origin.host.Value(), operation.request.origin.port},
		                            operation.request.path,
		                            compiled_pagination.PageSizeParameter(),
		                            compiled_pagination.PageSize(),
		                            compiled_pagination.PageNumberParameter(),
		                            compiled_pagination.FirstPage(),
		                            compiled_pagination.PageIncrement()};

		const auto page_response_bytes =
		    std::min(std::min(ceilings.MaxResponseBytesPerPage(), connector.NetworkPolicy().max_response_bytes),
		             PAGINATION_MAX_RESPONSE_BYTES_PER_PAGE);
		result.pagination.page_budgets = {
		    PAGINATION_MAX_REQUEST_ATTEMPTS_PER_PAGE,
		    page_response_bytes,
		    PAGINATION_MAX_HEADER_BYTES_PER_PAGE,
		    PAGINATION_MAX_DECOMPRESSED_BYTES_PER_PAGE,
		    std::min(ceilings.MaxRecordsPerPage(), PAGINATION_MAX_DECODED_RECORDS_PER_PAGE),
		    std::min(ceilings.MaxExtractedStringBytes(), PAGINATION_MAX_EXTRACTED_STRING_BYTES),
		    PAGINATION_MAX_JSON_NESTING,
		    PAGINATION_MAX_DECODED_MEMORY_BYTES,
		    PAGINATION_OUTPUT_BATCH_ROWS,
		    PAGINATION_MAX_EXECUTION_MILLISECONDS,
		    PAGINATION_MAX_CONCURRENCY};

		const auto max_pages = compiled_pagination.MaxPagesPerScan();
		result.pagination.scan_budgets = {
		    max_pages,
		    max_pages,
		    std::min(ceilings.MaxResponseBytesPerScan(), PAGINATION_MAX_RESPONSE_BYTES_PER_SCAN),
		    BoundedProduct(PAGINATION_MAX_HEADER_BYTES_PER_PAGE, max_pages, PAGINATION_MAX_HEADER_BYTES_PER_SCAN,
		                   "aggregate header-byte scope"),
		    BoundedProduct(PAGINATION_MAX_DECOMPRESSED_BYTES_PER_PAGE, max_pages,
		                   PAGINATION_MAX_DECOMPRESSED_BYTES_PER_SCAN, "aggregate decompressed-byte scope"),
		    std::min(ceilings.MaxRecordsPerScan(), PAGINATION_MAX_DECODED_RECORDS_PER_SCAN),
		    std::min(ceilings.MaxExtractedStringBytes(), PAGINATION_MAX_EXTRACTED_STRING_BYTES),
		    PAGINATION_MAX_JSON_NESTING,
		    PAGINATION_MAX_DECODED_MEMORY_BYTES,
		    PAGINATION_OUTPUT_BATCH_ROWS,
		    PAGINATION_MAX_EXECUTION_MILLISECONDS,
		    PAGINATION_MAX_CONCURRENCY};
		result.budgets = result.pagination.page_budgets;
		if (!result.pagination.PageBudgets().IsWithinPaginatedPageBounds() ||
		    !result.pagination.ScanBudgets().IsWithinPaginatedScanBounds() ||
		    result.pagination.ScanBudgets().response_bytes < result.pagination.PageBudgets().response_bytes ||
		    result.pagination.ScanBudgets().header_bytes < result.pagination.PageBudgets().header_bytes ||
		    result.pagination.ScanBudgets().decompressed_bytes < result.pagination.PageBudgets().decompressed_bytes ||
		    result.pagination.ScanBudgets().decoded_records < result.pagination.PageBudgets().decoded_records) {
			throw std::logic_error("selected relation produced an invalid paginated page or scan resource envelope");
		}
	}

	if (result.domain == BaseDomain::PAGINATED_JSON_PATH_RECORDS ||
	    result.domain == BaseDomain::PAGINATED_ROOT_ARRAY_RECORDS) {
		result.classification_reason =
		    "accepted sequential page records define one duplicate-preserving mutable base-domain bag; traversal "
		    "order is not DuckDB ordering; page and scan ceilings are not limits; DuckDB retains all relational "
		    "operators";
	} else if (result.domain == BaseDomain::SUCCESSFUL_ROOT_OBJECT) {
		result.classification_reason =
		    "successful root object defines exactly one base row; source cardinality is not a limit; DuckDB retains "
		    "all relational operators";
	} else {
		result.classification_reason =
		    "selected JSON-path records define the complete base domain; DuckDB retains all relational operators";
	}
	return result;
}

ScanPlan BuildConservativeScanPlan(const CompiledConnector &connector, const ScanRequest &request) {
	return ScanPlanBuilder::Build(connector, request);
}

} // namespace duckdb_api
