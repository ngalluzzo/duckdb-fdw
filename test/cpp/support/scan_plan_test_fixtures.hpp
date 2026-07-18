#pragma once

#include "duckdb_api/scan_plan.hpp"

#include <string>

namespace duckdb_api_test {

// Non-installable Relational Semantics provider test API. Runtime consumers
// may include this safe header to obtain closed valid and invalid plan values;
// they receive no construction authority, arbitrary field setter, credential
// value, Connector internals, DuckDB object, or Runtime implementation type.
// This test-only interface is neither a public ABI nor executable authority.

enum class OperationPlanCounterexample {
	OTHER_CONNECTOR_IDENTITY,
	OTHER_CONNECTOR_VERSION,
	OTHER_RELATION_IDENTITY,
	EMPTY_IDENTITY,
	OTHER_OPERATION_IDENTITY,
	UNKNOWN_METHOD,
	EMPTY_PATH,
	OTHER_PATH,
	INVALID_QUERY,
	EMPTY_FIXED_HEADER_VALUE,
	CASE_VARIANT_AUTHORIZATION_HEADER,
	DUPLICATE_AUTHORIZATION_HEADERS,
	HTTP_ORIGIN_SCHEME,
	OTHER_ORIGIN_HOST,
	OTHER_ORIGIN_PORT
};

enum class AuthenticatedPlanCounterexample {
	FEATURE_DISABLED,
	REQUIREMENT_NONE,
	EMPTY_LOGICAL_BINDING,
	AUTHENTICATOR_NONE,
	PLACEMENT_NONE,
	DESTINATION_ABSENT,
	HTTP_DESTINATION_SCHEME,
	OTHER_DESTINATION_HOST,
	OTHER_DESTINATION_PORT,
	MISSING_SECRET_REFERENCE
};

enum class AnonymousAuthPlanCounterexample {
	FEATURE_ENABLED,
	REQUIREMENT_REQUIRED,
	LOGICAL_BINDING_PRESENT,
	AUTHENTICATOR_BEARER,
	AUTHORIZATION_PLACEMENT,
	DESTINATION_PRESENT
};

enum class ResponsePlanCounterexample {
	JSON_PATH_RESPONSE_SOURCE,
	ZERO_TO_MANY_CARDINALITY,
	JSON_PATH_BASE_DOMAIN,
	EMPTY_RECORDS_EXTRACTOR,
	EMPTY_SCHEMA_NAME,
	UNSUPPORTED_SCHEMA_TYPE,
	FLIPPED_SCHEMA_NULLABILITY,
	EMPTY_SCHEMA_EXTRACTOR
};

enum class NetworkPlanCounterexample {
	EMPTY_SCHEMES,
	WIDENED_SCHEMES,
	EMPTY_HOSTS,
	WIDENED_HOSTS,
	REDIRECTS_ENABLED,
	PRIVATE_ADDRESSES_ENABLED,
	LINK_LOCAL_ADDRESSES_ENABLED,
	LOOPBACK_ADDRESSES_ENABLED
};

enum class FeaturePlanCounterexample { PAGINATION_ENABLED, PROVIDERS_ENABLED, RETRY_ENABLED, CACHE_ENABLED };

enum class ResourcePlanCounterexample {
	REQUEST_ATTEMPTS_ZERO,
	REQUEST_ATTEMPTS_WIDENED,
	RESPONSE_BYTES_ZERO,
	RESPONSE_BYTES_WIDENED,
	HEADER_BYTES_ZERO,
	HEADER_BYTES_WIDENED,
	DECOMPRESSED_BYTES_ZERO,
	DECOMPRESSED_BYTES_WIDENED,
	DECODED_RECORDS_ZERO,
	DECODED_RECORDS_WIDENED,
	EXTRACTED_STRING_BYTES_ZERO,
	EXTRACTED_STRING_BYTES_WIDENED,
	JSON_NESTING_ZERO,
	JSON_NESTING_WIDENED,
	DECODED_MEMORY_BYTES_ZERO,
	DECODED_MEMORY_BYTES_WIDENED,
	BATCH_ROWS_ZERO,
	BATCH_ROWS_WIDENED,
	WALL_MILLISECONDS_ZERO,
	WALL_MILLISECONDS_WIDENED,
	CONCURRENCY_ZERO,
	CONCURRENCY_WIDENED
};

duckdb_api::ScanPlan BuildValidAnonymousPlanFixture();
duckdb_api::ScanPlan BuildValidAuthenticatedPlanFixture(const std::string &exact_logical_secret_name);

duckdb_api::ScanPlan BuildOperationPlanCounterexample(const std::string &exact_logical_secret_name,
                                                      OperationPlanCounterexample counterexample);
duckdb_api::ScanPlan BuildAuthenticatedPlanCounterexample(const std::string &exact_logical_secret_name,
                                                          AuthenticatedPlanCounterexample counterexample);
duckdb_api::ScanPlan BuildAnonymousAuthPlanCounterexample(AnonymousAuthPlanCounterexample counterexample);
duckdb_api::ScanPlan BuildAnonymousSecretReferenceCounterexample(const std::string &exact_logical_secret_name);
duckdb_api::ScanPlan BuildResponsePlanCounterexample(const std::string &exact_logical_secret_name,
                                                     ResponsePlanCounterexample counterexample);
duckdb_api::ScanPlan BuildNetworkPlanCounterexample(const std::string &exact_logical_secret_name,
                                                    NetworkPlanCounterexample counterexample);
duckdb_api::ScanPlan BuildFeaturePlanCounterexample(const std::string &exact_logical_secret_name,
                                                    FeaturePlanCounterexample counterexample);
duckdb_api::ScanPlan BuildResourcePlanCounterexample(const std::string &exact_logical_secret_name,
                                                     ResourcePlanCounterexample counterexample);

} // namespace duckdb_api_test
