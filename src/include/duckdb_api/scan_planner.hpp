#pragma once

#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_plan.hpp"
#include "duckdb_api/scan_request.hpp"

#include <stdexcept>
#include <string>

namespace duckdb_api {

enum class PlanningErrorCode { INVALID_CONTRACT, OPERATION_SELECTION_FAILED };

// Deterministic, credential-safe semantic failure. A PlanningError produces no
// partial ScanPlan and grants no Runtime authority. It derives from logic_error
// so existing private pre-1.0 consumers that treated invalid planning as a
// contract defect remain source-compatible while adopting the structured code.
class PlanningError : public std::logic_error {
public:
	PlanningError(PlanningErrorCode code, std::string safe_message);

	PlanningErrorCode Code() const noexcept;

private:
	PlanningErrorCode code;
};

// Relational Semantics' construction facade. It deterministically selects and
// plans exactly the relation named by request, consumes only immutable
// Connector and Query team APIs, and performs no DuckDB callback, secret
// lookup, network/filesystem/environment access, runtime construction, or
// other I/O. Failure is all-or-nothing and returns PlanningError. Runtime
// consumers depend only on scan_plan.hpp.
ScanPlan BuildConservativeScanPlan(const CompiledConnector &connector, const ScanRequest &request);

} // namespace duckdb_api
