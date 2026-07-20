#include "generated_relation_adapter.hpp"

#include "complex_filter_adapter.hpp"
#include "package_catalog_snapshot.hpp"
#include "relation_execution.hpp"
#include "scan_plan_explanation.hpp"
#include "table_function_bind_data.hpp"
#include "typed_value_adapter.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb_api/query_generation.hpp"
#include "duckdb_api/scan_planner.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace duckdb {
namespace duckdb_api_query_internal {
namespace {

LogicalType RegistrationLogicalType(duckdb_api::CompiledScalarType type) {
	switch (type) {
	case duckdb_api::CompiledScalarType::BOOLEAN:
		return LogicalType::BOOLEAN;
	case duckdb_api::CompiledScalarType::BIGINT:
		return LogicalType::BIGINT;
	case duckdb_api::CompiledScalarType::VARCHAR:
		return LogicalType::VARCHAR;
	}
	throw InternalException("generated relation contains an unsupported structural type");
}

duckdb_api::ExplicitInput ExplicitInputValue(const duckdb_api::CompiledRelationInput &descriptor, const Value &value) {
	if (value.IsNull()) {
		switch (descriptor.Type()) {
		case duckdb_api::CompiledScalarType::BOOLEAN:
			return duckdb_api::ExplicitInput::Null(descriptor.Name(), duckdb_api::ExplicitInputValueKind::BOOLEAN);
		case duckdb_api::CompiledScalarType::BIGINT:
			return duckdb_api::ExplicitInput::Null(descriptor.Name(), duckdb_api::ExplicitInputValueKind::BIGINT);
		case duckdb_api::CompiledScalarType::VARCHAR:
			return duckdb_api::ExplicitInput::Null(descriptor.Name(), duckdb_api::ExplicitInputValueKind::VARCHAR);
		}
	}
	switch (descriptor.Type()) {
	case duckdb_api::CompiledScalarType::BOOLEAN:
		return duckdb_api::ExplicitInput::Boolean(descriptor.Name(), BooleanValue::Get(value));
	case duckdb_api::CompiledScalarType::BIGINT:
		return duckdb_api::ExplicitInput::BigInt(descriptor.Name(), BigIntValue::Get(value));
	case duckdb_api::CompiledScalarType::VARCHAR:
		return duckdb_api::ExplicitInput::Varchar(descriptor.Name(), StringValue::Get(value));
	}
	throw InternalException("generated relation contains an unsupported input type");
}

duckdb_api::LogicalSecretReference BindGeneratedSecret(TableFunctionBindInput &input,
                                                       duckdb_api::CompiledRegistrationAuthentication authentication,
                                                       const std::string &connector, const std::string &relation) {
	const auto entry = input.named_parameters.find("secret");
	if (authentication == duckdb_api::CompiledRegistrationAuthentication::ANONYMOUS) {
		if (entry != input.named_parameters.end()) {
			throw BinderException("[duckdb_api][bind] connector=%s relation=%s: named argument secret is not accepted",
			                      connector, relation);
		}
		return duckdb_api::LogicalSecretReference();
	}
	if (authentication != duckdb_api::CompiledRegistrationAuthentication::LOGICAL_SECRET_REQUIRED) {
		throw InternalException("generated relation has an unsupported authentication shape");
	}
	if (entry == input.named_parameters.end()) {
		throw BinderException("[duckdb_api][bind] connector=%s relation=%s: required named argument secret is missing",
		                      connector, relation);
	}
	if (entry->second.IsNull()) {
		throw BinderException(
		    "[duckdb_api][bind] connector=%s relation=%s: named argument secret must not be NULL or empty", connector,
		    relation);
	}
	const auto secret = StringValue::Get(entry->second);
	if (secret.empty()) {
		throw BinderException(
		    "[duckdb_api][bind] connector=%s relation=%s: named argument secret must not be NULL or empty", connector,
		    relation);
	}
	return duckdb_api::LogicalSecretReference::Named(secret);
}

InsertionOrderPreservingMap<string> GeneratedRelationToString(TableFunctionToStringInput &input) {
	if (!input.bind_data) {
		throw InternalException("generated relation explanation is missing bind data");
	}
	const auto &bind_data = input.bind_data->Cast<DuckdbApiBindData>();
	return ExplainSelectedScan(bind_data.plan_state.SelectedRequest(), bind_data.plan_state.SelectedPlan());
}

void GeneratedRelationPushdown(ClientContext &, LogicalGet &get, FunctionData *function_data,
                               vector<unique_ptr<Expression>> &filters) {
	if (!function_data || !get.function.function_info) {
		throw InternalException("generated relation filter callback is missing immutable bind information");
	}
	auto &bind_data = function_data->Cast<DuckdbApiBindData>();
	auto &function_info = get.function.function_info->Cast<PackageCatalogFunctionInfo>();
	if (function_info.kind != PackageCatalogFunctionKind::GENERATED_RELATION || !function_info.generation ||
	    !function_info.relation) {
		throw InternalException("generated relation filter callback has contradictory catalog ownership");
	}
	auto candidate = bind_data.plan_state.BaselineRequest();
	candidate.capabilities.selective_predicate = true;
	candidate.capabilities.retains_predicate = true;
	const auto translated = TranslateComplexFilters(get, filters);
	candidate.requested_predicate = translated.candidate;
	candidate.retained_predicate_scope = translated.retained_scope;
	try {
		auto selected = function_info.generation->Planning()->BuildPlan(
		    function_info.generation->Registration().GenerationHandle(), candidate);
		bind_data.plan_state.ReplaceSelected(std::move(candidate), std::move(selected));
	} catch (const duckdb_api::PlanningError &error) {
		throw InvalidInputException("[duckdb_api][planning] %s", error.what());
	} catch (const std::exception &) {
		throw InvalidInputException("[duckdb_api][planning] selective predicate planning failed safely");
	} catch (...) {
		throw InvalidInputException("[duckdb_api][planning] selective predicate planning failed safely");
	}
}

unique_ptr<FunctionData> BindGeneratedRelation(ClientContext &, TableFunctionBindInput &input,
                                               vector<LogicalType> &return_types, vector<string> &names) {
	if (!input.info) {
		throw InternalException("generated relation is missing immutable catalog information");
	}
	auto &info = input.info->Cast<PackageCatalogFunctionInfo>();
	if (info.kind != PackageCatalogFunctionKind::GENERATED_RELATION || !info.generation || !info.relation) {
		throw InternalException("generated relation has contradictory catalog information");
	}
	const auto &registration = info.generation->Registration();
	const auto &identity = registration.Identity();
	const auto &relation = *info.relation;
	std::vector<duckdb_api::ExplicitInput> explicit_values;
	explicit_values.reserve(relation.Inputs().size());
	for (const auto &descriptor : relation.Inputs()) {
		const auto entry = input.named_parameters.find(descriptor.Name());
		if (entry != input.named_parameters.end()) {
			explicit_values.push_back(ExplicitInputValue(descriptor, entry->second));
		}
	}
	auto secret = BindGeneratedSecret(input, relation.Authentication(), identity.ConnectorId(), relation.Name());
	auto request = duckdb_api::BuildPackageScanRequest(
	    identity, relation, duckdb_api::ExplicitInputs(std::move(explicit_values)), std::move(secret));
	try {
		auto plan = info.generation->Planning()->BuildPlan(registration.GenerationHandle(), request);
		for (const auto &column : plan.OutputColumns()) {
			names.push_back(column.name);
			return_types.push_back(PlannedLogicalType(column));
		}
		return make_uniq<DuckdbApiBindData>(std::move(request), std::move(plan), info.generation->Executor(),
		                                    info.generation);
	} catch (const duckdb_api::PlanningError &error) {
		throw BinderException("[duckdb_api][planning] connector=%s relation=%s: %s", identity.ConnectorId(),
		                      relation.Name(), error.what());
	} catch (const std::exception &) {
		throw BinderException("[duckdb_api][planning] connector=%s relation=%s: planning failed safely",
		                      identity.ConnectorId(), relation.Name());
	} catch (...) {
		throw BinderException("[duckdb_api][planning] connector=%s relation=%s: planning failed safely",
		                      identity.ConnectorId(), relation.Name());
	}
}

unique_ptr<GlobalTableFunctionState> InitGeneratedRelation(ClientContext &context, TableFunctionInitInput &input) {
	auto &data = input.bind_data->Cast<DuckdbApiBindData>();
	return InitializeRelationExecution(context, data.plan_state.SelectedPlan(), data.executor);
}

void ScanGeneratedRelation(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	ScanRelationExecution(context, input, output);
}

} // namespace

TableFunction
BuildGeneratedRelationFunction(const std::shared_ptr<CatalogGenerationCoordinator> &coordinator,
                               const std::shared_ptr<const PackageCatalogSnapshot> &snapshot,
                               const std::shared_ptr<const duckdb_api::QueryPublishedGeneration> &generation,
                               const duckdb_api::CompiledRegistrationRelation &relation) {
	const auto &registration = generation->Registration();
	TableFunction function(GeneratedRelationName(registration.Identity(), relation), {}, ScanGeneratedRelation,
	                       BindGeneratedRelation, InitGeneratedRelation);
	for (const auto &input : relation.Inputs()) {
		function.named_parameters[input.Name()] = RegistrationLogicalType(input.Type());
	}
	if (relation.Authentication() == duckdb_api::CompiledRegistrationAuthentication::LOGICAL_SECRET_REQUIRED) {
		function.named_parameters["secret"] = LogicalType::VARCHAR;
	} else if (relation.Authentication() != duckdb_api::CompiledRegistrationAuthentication::ANONYMOUS) {
		throw std::invalid_argument("generated relation has an unsupported authentication shape");
	}
	function.projection_pushdown = false;
	function.filter_pushdown = false;
	function.filter_prune = false;
	function.pushdown_complex_filter = GeneratedRelationPushdown;
	function.to_string = GeneratedRelationToString;
	function.function_info = make_shared_ptr<PackageCatalogFunctionInfo>(
	    coordinator, snapshot, PackageCatalogFunctionKind::GENERATED_RELATION, generation, &relation);
	return function;
}

} // namespace duckdb_api_query_internal
} // namespace duckdb
