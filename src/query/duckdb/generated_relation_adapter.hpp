#pragma once

#include "duckdb/function/table_function.hpp"

#include <memory>

namespace duckdb_api {
class QueryPublishedGeneration;
class CompiledRegistrationRelation;
class ScanPlan;
} // namespace duckdb_api

namespace duckdb {
namespace duckdb_api_query_internal {

class CatalogGenerationCoordinator;

// Correlates every same-generation planning result with the immutable schema
// DuckDB registered. The adapter calls this after initial planning and every
// selective replan before the selected plan can become observable.
void ValidateGeneratedRelationSchema(const duckdb_api::CompiledRegistrationRelation &relation,
                                     const duckdb_api::ScanPlan &plan);

// Builds one generated relation function from structural registration facts.
// The function captures the relation descriptor and immutable generation
// directly; its SQL name is never parsed for connector, input, operation, or
// protocol meaning.
TableFunction
BuildGeneratedRelationFunction(const std::shared_ptr<CatalogGenerationCoordinator> &coordinator,
                               const std::shared_ptr<const duckdb_api::QueryPublishedGeneration> &generation,
                               const duckdb_api::CompiledRegistrationRelation &relation);

} // namespace duckdb_api_query_internal
} // namespace duckdb
