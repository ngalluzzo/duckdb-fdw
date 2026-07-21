#pragma once

#include "duckdb_api/scan_plan.hpp"

#include <string>

namespace duckdb_api_test {

// Closed invalid package-generator plans supplied by Semantics to Runtime.
// Every case changes one recipe field or one correlation between that recipe
// and executable GraphQL operation facts. Runtime consumers receive only the
// resulting immutable ScanPlan and never construct private planned values.
enum class PackageGraphqlRuntimeRecipeCounterexample {
	MISSING_RECIPE,
	NATIVE_IDENTITY_WITH_RECIPE,
	UNKNOWN_RECIPE_IDENTITY,
	OTHER_RECIPE_OPERATION_NAME,
	OTHER_PAGE_VARIABLE_NAME,
	UNKNOWN_PAGE_VARIABLE_TYPE,
	UNKNOWN_PAGE_VARIABLE_ROLE,
	OTHER_PAGE_ARGUMENT_NAME,
	OTHER_CURSOR_VARIABLE_NAME,
	UNKNOWN_CURSOR_VARIABLE_TYPE,
	UNKNOWN_CURSOR_VARIABLE_ROLE,
	OTHER_CURSOR_ARGUMENT_NAME,
	OTHER_ROOT_PATH,
	MISSING_FIXED_ARGUMENT,
	OTHER_FIXED_ARGUMENT_NAME,
	UNKNOWN_FIXED_ARGUMENT_LITERAL_KIND,
	OTHER_FIXED_ARGUMENT_LITERAL_VALUE,
	OTHER_LIST_LITERAL_ITEM,
	OTHER_OBJECT_LITERAL_FIELD_NAME,
	OTHER_OBJECT_LITERAL_FIELD_VALUE,
	OTHER_NODES_FIELD,
	MISSING_SELECTION,
	OTHER_SELECTION_COLUMN,
	OTHER_SELECTION_PATH,
	OTHER_PAGE_INFO_FIELD,
	OTHER_HAS_NEXT_PAGE_FIELD,
	OTHER_END_CURSOR_FIELD,
	COHERENT_OTHER_DOCUMENT,
	OTHER_OPERATION_PAGE_VARIABLE,
	OTHER_OPERATION_CURSOR_VARIABLE,
	OTHER_OPERATION_NODES_PATH,
	OTHER_OPERATION_PAGE_INFO_PATH,
	OTHER_OPERATION_HAS_NEXT_PATH,
	OTHER_OPERATION_END_CURSOR_PATH,
	OTHER_OPERATION_RESULT_COLUMN_NAME,
	OTHER_OPERATION_RESULT_COLUMN_PATH,
	OTHER_CURSOR_PAGE_VARIABLE_CORRELATION,
	OTHER_CURSOR_VARIABLE_CORRELATION,
	COUNT
};

// Resolver-equivalent numeric origins supplied as coherent immutable plans.
// Runtime must reject these before authorization or transport even though the
// operation, credential destination, and network capability agree exactly.
enum class PackageHttpNumericOriginCounterexample {
	LOOPBACK_TWO_COMPONENT_DECIMAL,
	LOOPBACK_SINGLE_DECIMAL,
	LOOPBACK_SINGLE_HEX,
	LOOPBACK_SINGLE_HEX_UPPERCASE,
	LOOPBACK_TWO_COMPONENT_HEX_UPPERCASE,
	PUBLIC_SINGLE_DECIMAL,
	PUBLIC_SINGLE_HEX,
	PUBLIC_MAX_HEX_UPPERCASE,
	PUBLIC_FOUR_COMPONENT_OCTAL,
	COUNT
};

// Produces the real repository GitHub package's GraphQL plan through one exact
// compiled generation and its matching opaque handle. The bounded API exposes
// only Semantics' immutable ScanPlan; Runtime consumers do not compile or
// import Connector/compiler/planner implementation details.
duckdb_api::ScanPlan BuildRepositoryGithubPackageGraphqlPlan(const std::string &absolute_repository_root,
                                                             const std::string &logical_secret_name);

// Produces the real non-GitHub package's anonymous :8443 GraphQL candidate and
// REST fallback through the same bounded Semantics planning service. Runtime
// receives only immutable plans and never compiles package source or imports a
// Connector/private construction surface.
duckdb_api::ScanPlan BuildNonGithubPackageGraphqlPlan(const std::string &absolute_repository_root);
duckdb_api::ScanPlan BuildNonGithubPackageRestPlan(const std::string &absolute_repository_root);

// Compiles the real authenticated repository package, then returns one closed
// coherent invalid-origin variation through Semantics' fixture service.
duckdb_api::ScanPlan
BuildRepositoryPackageGraphqlNumericOriginCounterexample(const std::string &absolute_repository_root,
                                                         const std::string &logical_secret_name,
                                                         PackageHttpNumericOriginCounterexample counterexample);
duckdb_api::ScanPlan
BuildRepositoryPackageRestNumericOriginCounterexample(const std::string &absolute_repository_root,
                                                      const std::string &logical_secret_name,
                                                      PackageHttpNumericOriginCounterexample counterexample);

duckdb_api::ScanPlan
BuildPackageGraphqlRuntimeRecipeCounterexample(const std::string &absolute_repository_root,
                                               const std::string &logical_secret_name,
                                               PackageGraphqlRuntimeRecipeCounterexample counterexample);

} // namespace duckdb_api_test
