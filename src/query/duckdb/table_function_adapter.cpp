#include "duckdb_api_extension.hpp"

#include "complex_filter_adapter.hpp"
#include "relation_execution.hpp"
#include "scan_plan_explanation.hpp"
#include "table_function_bind_data.hpp"
#include "table_function_plan_state.hpp"
#include "typed_value_adapter.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb_api/duckdb_secret.hpp"
#include "duckdb_api/scan_planner.hpp"
#include "duckdb_api/scan_request.hpp"

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace duckdb {
namespace {

using duckdb_api_query_internal::DuckdbApiBindData;

// Registration state retains only immutable provider APIs. Product and private
// controlled composition both enter through this boundary.
struct DuckdbApiFunctionInfo : public TableFunctionInfo {
	DuckdbApiFunctionInfo(duckdb_api::CompiledConnector connector_p,
	                      std::shared_ptr<const duckdb_api::ScanExecutor> executor_p)
	    : connector(std::move(connector_p)), executor(std::move(executor_p)) {
	}

	const duckdb_api::CompiledConnector connector;
	const std::shared_ptr<const duckdb_api::ScanExecutor> executor;
};

InsertionOrderPreservingMap<string> DuckdbApiToString(TableFunctionToStringInput &input) {
	if (!input.bind_data) {
		throw InternalException("duckdb_api explanation is missing bind data");
	}
	const auto &bind_data = input.bind_data->Cast<DuckdbApiBindData>();
	return duckdb_api_query_internal::ExplainSelectedScan(bind_data.plan_state.SelectedRequest(),
	                                                      bind_data.plan_state.SelectedPlan());
}

void DuckdbApiPushdownComplexFilter(ClientContext &, LogicalGet &get, FunctionData *function_data,
                                    vector<unique_ptr<Expression>> &filters) {
	if (!function_data || !get.function.function_info) {
		throw InternalException("duckdb_api complex-filter callback is missing immutable bind information");
	}
	auto &bind_data = function_data->Cast<DuckdbApiBindData>();
	// DuckDB may re-optimize an execution-specific copy after replacing a
	// prepared parameter with a typed constant. Rebuild from the retained
	// baseline on every callback so each execution selects or falls back from
	// its own structured expression without inheriting a prior value's plan.
	auto candidate = bind_data.plan_state.BaselineRequest();
	candidate.capabilities.selective_predicate = true;
	candidate.capabilities.retains_predicate = true;
	const auto translated = duckdb_api_query_internal::TranslateComplexFilters(get, filters);
	candidate.requested_predicate = translated.candidate;
	candidate.retained_predicate_scope = translated.retained_scope;

	// The filter vector is intentionally untouched. DuckDB regenerates every
	// expression as its own LogicalFilter because generic filter pushdown stays
	// disabled. Build the complete replacement before changing selected state.
	try {
		auto &function_info = get.function.function_info->Cast<DuckdbApiFunctionInfo>();
		auto selected_plan = duckdb_api::BuildConservativeScanPlan(function_info.connector, candidate);
		bind_data.plan_state.ReplaceSelected(std::move(candidate), std::move(selected_plan));
	} catch (const duckdb_api::PlanningError &error) {
		throw InvalidInputException("[duckdb_api][planning] %s", error.what());
	} catch (const std::exception &) {
		throw InvalidInputException("[duckdb_api][planning] selective predicate planning failed safely");
	} catch (...) {
		throw InvalidInputException("[duckdb_api][planning] selective predicate planning failed safely");
	}
}

std::string RequiredNamedString(TableFunctionBindInput &input, const std::string &name) {
	const auto entry = input.named_parameters.find(name);
	if (entry == input.named_parameters.end() || entry->second.IsNull()) {
		throw BinderException("[duckdb_api][bind] required named argument %s is missing", name);
	}
	const auto value = StringValue::Get(entry->second);
	if (value.empty()) {
		throw BinderException("[duckdb_api][bind] required named argument %s must not be empty", name);
	}
	return value;
}

duckdb_api::LogicalSecretReference BindSecretReference(TableFunctionBindInput &input,
                                                       duckdb_api::CompiledCredentialRequirement requirement,
                                                       const std::string &connector_name,
                                                       const std::string &relation_name) {
	const auto entry = input.named_parameters.find("secret");
	if (entry == input.named_parameters.end()) {
		if (requirement == duckdb_api::CompiledCredentialRequirement::REQUIRED) {
			throw BinderException(
			    "[duckdb_api][bind] connector=%s relation=%s: required named argument secret is missing",
			    connector_name, relation_name);
		}
		return duckdb_api::LogicalSecretReference();
	}
	if (entry->second.IsNull()) {
		throw BinderException(
		    "[duckdb_api][bind] connector=%s relation=%s: named argument secret must not be NULL or empty",
		    connector_name, relation_name);
	}
	const auto logical_name = StringValue::Get(entry->second);
	if (logical_name.empty()) {
		throw BinderException(
		    "[duckdb_api][bind] connector=%s relation=%s: named argument secret must not be NULL or empty",
		    connector_name, relation_name);
	}
	if (requirement == duckdb_api::CompiledCredentialRequirement::NONE) {
		throw BinderException("[duckdb_api][bind] connector=%s relation=%s: named argument secret is not accepted",
		                      connector_name, relation_name);
	}
	if (requirement != duckdb_api::CompiledCredentialRequirement::REQUIRED) {
		throw InternalException("duckdb_api relation has an unsupported credential requirement");
	}
	return duckdb_api::LogicalSecretReference::Named(logical_name);
}

unique_ptr<FunctionData> DuckdbApiBind(ClientContext &, TableFunctionBindInput &input,
                                       vector<LogicalType> &return_types, vector<string> &names) {
	const auto connector_name = RequiredNamedString(input, "connector");
	const auto relation_name = RequiredNamedString(input, "relation");
	if (!input.info) {
		throw InternalException("duckdb_api table function is missing immutable function information");
	}
	auto &function_info = input.info->Cast<DuckdbApiFunctionInfo>();
	if (connector_name != function_info.connector.ConnectorName()) {
		throw BinderException("[duckdb_api][bind] unknown connector identifier");
	}
	const auto *relation = function_info.connector.FindRelation(relation_name);
	if (!relation) {
		throw BinderException("[duckdb_api][bind] connector=%s: unknown relation identifier", connector_name);
	}
	if (!function_info.executor) {
		throw InternalException("duckdb_api table function is missing its scan executor");
	}

	// Bind performs deterministic metadata planning only. Executor open and all
	// network authority remain deferred until DuckdbApiInit.
	auto secret_reference =
	    BindSecretReference(input, relation->Authentication().Requirement(), connector_name, relation_name);
	auto request =
	    duckdb_api::BuildConservativeScanRequest(function_info.connector, relation_name, std::move(secret_reference));
	auto plan = duckdb_api::BuildConservativeScanPlan(function_info.connector, request);

	for (const auto &column : plan.OutputColumns()) {
		names.push_back(column.name);
		return_types.push_back(duckdb_api_query_internal::PlannedLogicalType(column));
	}
	return make_uniq<DuckdbApiBindData>(std::move(request), std::move(plan), function_info.executor);
}

unique_ptr<GlobalTableFunctionState> DuckdbApiInit(ClientContext &context, TableFunctionInitInput &input) {
	auto &bind_data = input.bind_data->Cast<DuckdbApiBindData>();
	return duckdb_api_query_internal::InitializeRelationExecution(context, bind_data.plan_state.SelectedPlan(),
	                                                              bind_data.executor);
}

void DuckdbApiScan(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	duckdb_api_query_internal::ScanRelationExecution(context, input, output);
}

} // namespace

void RegisterDuckdbApi(ExtensionLoader &loader, duckdb_api::CompiledConnector connector,
                       std::shared_ptr<const duckdb_api::ScanExecutor> executor) {
	if (!executor) {
		throw InternalException("duckdb_api registration requires a scan executor");
	}
	// DuckDB 1.5.4 cannot atomically register a secret type, provider, and
	// table function. Publish the scan only after the credential boundary is
	// complete; a failure may leave an orphan type/provider, but never a scan
	// function that cannot resolve its declared secret surface.
	RegisterDuckdbApiSecrets(loader);
	TableFunction scan("duckdb_api_scan", {}, DuckdbApiScan, DuckdbApiBind, DuckdbApiInit);
	scan.named_parameters["connector"] = LogicalType::VARCHAR;
	scan.named_parameters["relation"] = LogicalType::VARCHAR;
	scan.named_parameters["secret"] = LogicalType::VARCHAR;
	scan.projection_pushdown = false;
	scan.filter_pushdown = false;
	scan.filter_prune = false;
	scan.pushdown_complex_filter = DuckdbApiPushdownComplexFilter;
	scan.to_string = DuckdbApiToString;
	scan.function_info = make_shared_ptr<DuckdbApiFunctionInfo>(std::move(connector), std::move(executor));
	loader.RegisterFunction(std::move(scan));
}

} // namespace duckdb
