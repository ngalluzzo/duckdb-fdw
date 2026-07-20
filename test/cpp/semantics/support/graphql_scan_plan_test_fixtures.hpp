#pragma once

#include "duckdb_api/scan_plan.hpp"

#include <string>

namespace duckdb_api_test {

// Closed valid local-residual profiles supplied by Relational Semantics to
// focused Runtime admission tests. These cases differ only in the conservative
// predicate classification produced from Query's retained local filter. Every
// profile keeps remote TRUE, grants no conditional input, leaves every
// relational operator with DuckDB, and grants no remote or Runtime delegation.
// Runtime may admit these complete profiles but must not infer the local
// expression from the enum, explanation, or COMPLETE_DUCKDB_FILTER marker.
enum class GraphqlLocalResidualProfile {
	UNRESTRICTED,
	MAPPING_UNAVAILABLE,
	STRUCTURE_UNSUPPORTED,
	CAPABILITY_UNAVAILABLE,
	COUNT
};

// Closed positive-admission probes supplied to Runtime. Each probe changes one
// non-authority plan fact from the unrestricted profile while preserving the
// complete executable remote, ownership, and delegation envelope. These
// deliberately isolated values are admission probes, not coherent planner
// outcomes or expression fixtures. COUNT is an iteration sentinel.
enum class GraphqlRuntimeNonAuthorityVariation {
	OTHER_RESIDUAL_PREDICATE,
	OTHER_PREDICATE_CATEGORY,
	OTHER_PREDICATE_REASON,
	COUNT
};

// Closed Semantics provider API for focused Runtime admission consumers. Each
// counterexample isolates one executable authority fact that Runtime must
// enforce before authorization or transport. Most change one structured field;
// CHANGED_DOCUMENT_WITH_RECOMPUTED_DIGEST changes the dependent document and
// digest together so self-consistency cannot replace reviewed canonical-profile
// membership. Provenance prose and valid logical-secret names are intentionally
// outside this rejection surface. Factories expose immutable plans, never
// private builders, Connector catalogs, ScanRequest values, Runtime types, or
// credential bytes. COUNT is an iteration sentinel, not a plan candidate.
enum class GraphqlRuntimeAdmissionCounterexample {
	UNKNOWN_PROTOCOL,
	GRAPHQL_CONTRADICTORY_PROTOCOL_PAYLOADS,
	GRAPHQL_MISSING_ACTIVE_PROTOCOL_PAYLOAD,
	GRAPHQL_WRONG_PROTOCOL_PAYLOAD,
	REST_CONTRADICTORY_PROTOCOL_PAYLOADS,
	REST_MISSING_ACTIVE_PROTOCOL_PAYLOAD,
	REST_WRONG_PROTOCOL_PAYLOAD,
	OTHER_OPERATION_NAME,
	OTHER_OPERATION_CARDINALITY,
	UNKNOWN_REPLAY_SAFETY,
	UNKNOWN_OPERATION_KIND,
	OTHER_DOCUMENT_IDENTITY,
	OTHER_DOCUMENT,
	CHANGED_DOCUMENT_WITH_RECOMPUTED_DIGEST,
	UNKNOWN_DIGEST_ALGORITHM,
	OTHER_DOCUMENT_DIGEST,
	HTTP_OPERATION_ORIGIN,
	OTHER_OPERATION_HOST,
	OTHER_OPERATION_PORT,
	OTHER_OPERATION_PATH,
	OTHER_HEADER_NAME,
	OTHER_HEADER_VALUE,
	MISSING_OPERATION_HEADER,
	REORDERED_OPERATION_HEADERS,
	EXTRA_OPERATION_HEADER,
	OTHER_VARIABLE_NAME,
	UNKNOWN_VARIABLE_TYPE,
	UNKNOWN_VARIABLE_SOURCE,
	OTHER_VARIABLE_INTEGER_VALUE,
	MISSING_OPERATION_VARIABLE,
	REORDERED_OPERATION_VARIABLES,
	EXTRA_OPERATION_VARIABLE,
	OTHER_RESULT_NAME,
	UNKNOWN_RESULT_SCALAR,
	OTHER_RESULT_NULLABILITY,
	OTHER_RESULT_PATH,
	MISSING_OPERATION_RESULT_COLUMN,
	REORDERED_OPERATION_RESULT_COLUMNS,
	EXTRA_OPERATION_RESULT_COLUMN,
	OTHER_RESPONSE_NODES_PATH,
	OTHER_RESPONSE_ERRORS_PATH,
	OTHER_RESPONSE_PAGE_INFO_PATH,
	UNKNOWN_PARTIAL_DATA_POLICY,
	UNKNOWN_OPERATION_CURSOR_DIRECTION,
	UNKNOWN_OPERATION_CURSOR_DEPENDENCY,
	UNKNOWN_OPERATION_CURSOR_CONSISTENCY,
	OPERATION_CURSOR_SUPPORTS_TOTAL,
	OPERATION_CURSOR_SUPPORTS_RESUME,
	OTHER_OPERATION_CURSOR_CONCURRENCY,
	OTHER_OPERATION_PAGE_SIZE_VARIABLE,
	OTHER_OPERATION_CURSOR_PAGE_SIZE,
	OTHER_OPERATION_CURSOR_VARIABLE,
	OTHER_OPERATION_HAS_NEXT_PAGE_PATH,
	OTHER_OPERATION_END_CURSOR_PATH,
	OTHER_OPERATION_MAX_PAGES,
	OTHER_MAX_DOCUMENT_BYTES,
	OTHER_OPERATION_PAGE_BODY_BUDGET,
	OTHER_OPERATION_SCAN_BODY_BUDGET,
	OTHER_OUTPUT_NAME,
	OTHER_OUTPUT_LOGICAL_TYPE,
	OTHER_OUTPUT_NULLABILITY,
	OTHER_OUTPUT_EXTRACTOR,
	MISSING_OUTPUT_COLUMN,
	REORDERED_OUTPUT_COLUMNS,
	EXTRA_OUTPUT_COLUMN,
	OTHER_DOMAIN,
	UNKNOWN_DOMAIN,
	OTHER_REMOTE_PREDICATE,
	OTHER_REMOTE_ACCURACY,
	OTHER_RESIDUAL_OWNER,
	OTHER_CONDITIONAL_INPUT,
	OTHER_FILTER_OWNER,
	OTHER_PROJECTION_OWNER,
	OTHER_ORDERING_OWNER,
	OTHER_LIMIT_OWNER,
	OTHER_OFFSET_OWNER,
	OTHER_REMOTE_ORDERING,
	OTHER_RUNTIME_ORDERING,
	OTHER_REMOTE_LIMIT,
	OTHER_REMOTE_OFFSET,
	OTHER_RUNTIME_LIMIT,
	OTHER_RUNTIME_OFFSET,
	PROVIDERS_ENABLED,
	RETRY_ENABLED,
	CACHE_ENABLED,
	AUTHENTICATION_DISABLED,
	SECRET_REFERENCE_ABSENT,
	OTHER_AUTH_REQUIREMENT,
	OTHER_LOGICAL_CREDENTIAL,
	OTHER_AUTHENTICATOR,
	OTHER_CREDENTIAL_PLACEMENT,
	AUTH_DESTINATION_ABSENT,
	HTTP_AUTH_DESTINATION,
	OTHER_AUTH_DESTINATION_HOST,
	OTHER_AUTH_DESTINATION_PORT,
	OTHER_NETWORK_SCHEME,
	OTHER_NETWORK_HOST,
	EXTRA_NETWORK_SCHEME,
	EXTRA_NETWORK_HOST,
	REDIRECTS_ENABLED,
	PRIVATE_ADDRESSES_ENABLED,
	LINK_LOCAL_ADDRESSES_ENABLED,
	LOOPBACK_ADDRESSES_ENABLED,
	OTHER_PAGINATION_STRATEGY,
	UNKNOWN_CURSOR_DIRECTION,
	UNKNOWN_CURSOR_DEPENDENCY,
	UNKNOWN_CURSOR_CONSISTENCY,
	CURSOR_SUPPORTS_TOTAL,
	CURSOR_SUPPORTS_RESUME,
	OTHER_CURSOR_CONCURRENCY,
	OTHER_CURSOR_PAGE_SIZE_VARIABLE,
	OTHER_CURSOR_PAGE_SIZE,
	OTHER_CURSOR_VARIABLE,
	OTHER_CURSOR_HAS_NEXT_PAGE_PATH,
	OTHER_CURSOR_END_CURSOR_PATH,
	OTHER_CURSOR_MAX_PAGES,
	OTHER_PAGE_REQUEST_ATTEMPTS,
	OTHER_PAGE_RESPONSE_BYTES,
	OTHER_PAGE_HEADER_BYTES,
	OTHER_PAGE_DECOMPRESSED_BYTES,
	OTHER_PAGE_DECODED_RECORDS,
	OTHER_PAGE_EXTRACTED_STRING_BYTES,
	OTHER_PAGE_JSON_NESTING,
	OTHER_PAGE_DECODED_MEMORY_BYTES,
	OTHER_PAGE_BATCH_ROWS,
	OTHER_PAGE_WALL_MILLISECONDS,
	OTHER_PAGE_CONCURRENCY,
	OTHER_PAGE_SERIALIZED_BODY_BYTES,
	OTHER_SCAN_REQUEST_ATTEMPTS,
	OTHER_SCAN_PAGES,
	OTHER_SCAN_RESPONSE_BYTES,
	OTHER_SCAN_HEADER_BYTES,
	OTHER_SCAN_DECOMPRESSED_BYTES,
	OTHER_SCAN_DECODED_RECORDS,
	OTHER_SCAN_EXTRACTED_STRING_BYTES,
	OTHER_SCAN_JSON_NESTING,
	OTHER_SCAN_DECODED_MEMORY_BYTES,
	OTHER_SCAN_BATCH_ROWS,
	OTHER_SCAN_WALL_MILLISECONDS,
	OTHER_SCAN_CONCURRENCY,
	OTHER_SCAN_SERIALIZED_BODY_BYTES,
	COUNT
};

duckdb_api::ScanPlan BuildValidGraphqlScanPlanFixture(const std::string &exact_logical_secret_name);
duckdb_api::ScanPlan BuildValidGraphqlScanPlanFixture(const std::string &exact_logical_secret_name,
                                                      GraphqlLocalResidualProfile profile);

// Canonical native GraphQL plan with the same operation, schema, cursor,
// resource, network, and relational authority as the authenticated fixture,
// but no credential requirement or logical-secret selector. Runtime consumes
// this immutable value to exercise anonymous admission without receiving
// Semantics construction authority.
duckdb_api::ScanPlan BuildValidAnonymousGraphqlScanPlanFixture();

// Coherent positive plan with package-like connector, relation, operation, and
// source-provenance labels. Its package document identity and generic Relay
// connection-node domain deliberately differ from the native 0.7 bridge; all
// other executable GraphQL, network, credential, cursor, resource, and
// relational authority is identical. Runtime consumers use this bounded value
// to prove that admission does not reconstruct authority from source identity
// or names. The caller-selected logical secret reference is preserved exactly.
duckdb_api::ScanPlan BuildDistinctGraphqlProvenanceScanPlanFixture(const std::string &exact_logical_secret_name);

duckdb_api::ScanPlan BuildGraphqlRuntimeNonAuthorityVariation(const std::string &exact_logical_secret_name,
                                                              GraphqlRuntimeNonAuthorityVariation variation);
duckdb_api::ScanPlan BuildGraphqlRuntimeAdmissionCounterexample(const std::string &exact_logical_secret_name,
                                                                GraphqlRuntimeAdmissionCounterexample counterexample);

} // namespace duckdb_api_test
