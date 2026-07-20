#pragma once

#include "duckdb/function/table_function.hpp"
#include "duckdb_api/execution.hpp"
#include "duckdb_api/scan_plan.hpp"

#include <memory>

namespace duckdb {
class ClientContext;

namespace duckdb_api_query_internal {

// Call-scoped adapter for DuckDB interruption. Provider services may inspect
// it only during the call and cannot retain ClientContext or throw DuckDB
// exceptions through the interface.
class DuckdbExecutionControl final : public duckdb_api::ExecutionControl {
public:
	explicit DuckdbExecutionControl(duckdb::ClientContext &context);
	bool IsCancellationRequested() const noexcept override;

private:
	duckdb::ClientContext &context;
};

// Shared execution half of native-dispatcher and generated-relation table
// functions. Bind/planning remains function-specific; once an immutable plan
// exists, both paths use this single Secret Manager, stream, cancellation,
// error, and vector-output boundary.
duckdb::unique_ptr<duckdb::GlobalTableFunctionState>
InitializeRelationExecution(duckdb::ClientContext &context, const duckdb_api::ScanPlan &plan,
                            const std::shared_ptr<const duckdb_api::ScanExecutor> &executor);

void ScanRelationExecution(duckdb::ClientContext &context, duckdb::TableFunctionInput &input,
                           duckdb::DataChunk &output);

} // namespace duckdb_api_query_internal
} // namespace duckdb
