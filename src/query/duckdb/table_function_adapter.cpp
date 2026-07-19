#include "duckdb_api_extension.hpp"

#include "complex_filter_adapter.hpp"
#include "table_function_bind_data.hpp"
#include "table_function_plan_state.hpp"

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

// Call-scoped DuckDB coupling. Runtime receives only this non-owning control
// interface and cannot retain ClientContext or throw a DuckDB exception.
class DuckdbExecutionControl : public duckdb_api::ExecutionControl {
public:
	explicit DuckdbExecutionControl(ClientContext &context_p) : context(context_p) {
	}

	bool IsCancellationRequested() const noexcept override {
		try {
			return context.IsInterrupted();
		} catch (...) {
			return true;
		}
	}

private:
	ClientContext &context;
};

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

const char *PredicateName(duckdb_api::PlannedPredicate predicate) {
	switch (predicate) {
	case duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN:
		return "unrestricted";
	case duckdb_api::PlannedPredicate::VISIBILITY_EQUALS_PRIVATE:
		return "visibility_equals_private";
	case duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER:
		return "complete_duckdb_filter";
	}
	throw InternalException("duckdb_api scan plan contains an unknown predicate state");
}

const char *AccuracyName(duckdb_api::RemotePredicateAccuracy accuracy) {
	switch (accuracy) {
	case duckdb_api::RemotePredicateAccuracy::UNSUPPORTED:
		return "unsupported";
	case duckdb_api::RemotePredicateAccuracy::SUPERSET:
		return "superset";
	}
	throw InternalException("duckdb_api scan plan contains an unknown remote accuracy");
}

const char *ResidualOwnerName(duckdb_api::RelationalOwner owner) {
	switch (owner) {
	case duckdb_api::RelationalOwner::DUCKDB:
		return "duckdb";
	}
	throw InternalException("duckdb_api scan plan contains an unknown residual owner");
}

InsertionOrderPreservingMap<string> DuckdbApiToString(TableFunctionToStringInput &input) {
	if (!input.bind_data) {
		throw InternalException("duckdb_api explanation is missing bind data");
	}
	const auto &bind_data = input.bind_data->Cast<DuckdbApiBindData>();
	const auto &plan = bind_data.plan_state.SelectedPlan();
	InsertionOrderPreservingMap<string> result;
	// These closed plan facts are safe explanation only. Query neither parses
	// this rendering nor exposes request, credential, row, or received-URL data.
	result["Relation"] = plan.RelationName();
	result["Remote Predicate"] = PredicateName(plan.RemotePredicate());
	result["Remote Accuracy"] = AccuracyName(plan.RemoteAccuracy());
	result["Residual Predicate"] = PredicateName(plan.ResidualPredicate());
	result["Residual Owner"] = ResidualOwnerName(plan.ResidualOwner());
	result["Classification"] = plan.ClassificationReason();
	return result;
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
	std::size_t recognized = 0;
	std::size_t present = 0;
	for (const auto &filter : filters) {
		if (!filter) {
			continue;
		}
		present++;
		if (duckdb_api_query_internal::IsVisibilityEqualsPrivate(get, *filter)) {
			recognized++;
		}
	}
	if (recognized > 0) {
		candidate.requested_predicate = duckdb_api::RequestedPredicate::VisibilityEqualsPrivate();
		candidate.retained_predicate_scope = recognized == 1 && present == 1
		                                         ? duckdb_api::RetainedPredicateScope::REQUESTED_PREDICATE
		                                         : duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER;
	} else if (present > 0) {
		candidate.retained_predicate_scope = duckdb_api::RetainedPredicateScope::COMPLETE_DUCKDB_FILTER;
	}

	// The filter vector is intentionally untouched. DuckDB regenerates every
	// expression as its own LogicalFilter because generic filter pushdown stays
	// disabled. Build the complete replacement before changing selected state.
	try {
		auto &function_info = get.function.function_info->Cast<DuckdbApiFunctionInfo>();
		auto selected_plan = duckdb_api::BuildConservativeScanPlan(function_info.connector, candidate);
		bind_data.plan_state.ReplaceSelectedPlan(std::move(selected_plan));
	} catch (const std::exception &) {
		throw InvalidInputException("[duckdb_api][planning] selective predicate planning failed safely");
	} catch (...) {
		throw InvalidInputException("[duckdb_api][planning] selective predicate planning failed safely");
	}
}

// One DuckDB source task exclusively owns one mutable stream. Destruction is a
// non-throwing finalizer for success, failure, early close, and connection
// teardown; unfinished streams receive cancellation before close.
struct DuckdbApiGlobalState : public GlobalTableFunctionState {
	DuckdbApiGlobalState(std::unique_ptr<duckdb_api::BatchStream> stream_p,
	                     std::vector<duckdb_api::ValueKind> expected_kinds_p, uint64_t max_batch_rows_p,
	                     std::string connector_name_p, std::string relation_name_p)
	    : stream(std::move(stream_p)), expected_kinds(std::move(expected_kinds_p)), max_batch_rows(max_batch_rows_p),
	      connector_name(std::move(connector_name_p)), relation_name(std::move(relation_name_p)), finished(false) {
	}

	~DuckdbApiGlobalState() override {
		if (!stream) {
			return;
		}
		if (!finished) {
			stream->Cancel();
		}
		stream->Close();
	}

	idx_t MaxThreads() const override {
		return 1;
	}

	std::unique_ptr<duckdb_api::BatchStream> stream;
	const std::vector<duckdb_api::ValueKind> expected_kinds;
	const uint64_t max_batch_rows;
	const std::string connector_name;
	const std::string relation_name;
	bool finished;
};

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

const char *ErrorStageName(duckdb_api::ErrorStage stage) {
	switch (stage) {
	case duckdb_api::ErrorStage::TRANSPORT:
		return "transport";
	case duckdb_api::ErrorStage::HTTP_STATUS:
		return "http_status";
	case duckdb_api::ErrorStage::DECODE:
		return "decode";
	case duckdb_api::ErrorStage::SCHEMA:
		return "schema";
	case duckdb_api::ErrorStage::POLICY:
		return "policy";
	case duckdb_api::ErrorStage::RESOURCE:
		return "resource";
	case duckdb_api::ErrorStage::INTERNAL:
		return "internal";
	case duckdb_api::ErrorStage::AUTHENTICATION:
		return "authentication";
	case duckdb_api::ErrorStage::AUTHORIZATION:
		return "authorization";
	}
	return "internal";
}

[[noreturn]] void ThrowExecutionError(const duckdb_api::ExecutionError &error, const std::string &connector_name,
                                      const std::string &relation_name) {
	if (error.Stage() == duckdb_api::ErrorStage::INTERNAL) {
		throw InvalidInputException("[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure",
		                            connector_name, relation_name);
	}
	if (error.Field().empty()) {
		throw InvalidInputException("[duckdb_api][%s] connector=%s relation=%s: %s", ErrorStageName(error.Stage()),
		                            connector_name, relation_name, error.SafeMessage());
	}
	throw InvalidInputException("[duckdb_api][%s] connector=%s relation=%s field=%s: %s", ErrorStageName(error.Stage()),
	                            connector_name, relation_name, error.Field(), error.SafeMessage());
}

LogicalType PlannedLogicalType(const duckdb_api::PlannedColumn &column) {
	if (column.logical_type == "BIGINT") {
		return LogicalType::BIGINT;
	}
	if (column.logical_type == "VARCHAR") {
		return LogicalType::VARCHAR;
	}
	if (column.logical_type == "BOOLEAN") {
		return LogicalType::BOOLEAN;
	}
	throw InternalException("duckdb_api scan plan contains an unsupported logical type");
}

duckdb_api::ValueKind PlannedValueKind(const duckdb_api::PlannedColumn &column) {
	if (column.logical_type == "BIGINT") {
		return duckdb_api::ValueKind::BIGINT;
	}
	if (column.logical_type == "VARCHAR") {
		return duckdb_api::ValueKind::VARCHAR;
	}
	if (column.logical_type == "BOOLEAN") {
		return duckdb_api::ValueKind::BOOLEAN;
	}
	throw InternalException("duckdb_api scan plan contains an unsupported logical type");
}

std::vector<duckdb_api::ValueKind> PlannedValueKinds(const duckdb_api::ScanPlan &plan) {
	std::vector<duckdb_api::ValueKind> result;
	result.reserve(plan.OutputColumns().size());
	for (const auto &column : plan.OutputColumns()) {
		result.push_back(PlannedValueKind(column));
	}
	return result;
}

[[noreturn]] void ThrowCancellation(duckdb_api::BatchStream *stream) {
	if (stream) {
		stream->Cancel();
	}
	throw InterruptException();
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
		return_types.push_back(PlannedLogicalType(column));
	}
	return make_uniq<DuckdbApiBindData>(std::move(request), std::move(plan), function_info.executor);
}

std::unique_ptr<duckdb_api::BatchStream> OpenAuthorizedStream(const DuckdbApiBindData &bind_data,
                                                              ClientContext &context, DuckdbExecutionControl &control) {
	// Every global initialization resolves a fresh execution-scoped capability.
	// Query never reads its token alternative: it moves the closed authorization
	// directly into Runtime, which owns validation, use, and destruction.
	if (control.IsCancellationRequested()) {
		throw duckdb_api::ExecutionCancelled();
	}
	const auto &plan = bind_data.plan_state.SelectedPlan();
	if (plan.Authentication() == duckdb_api::FeatureState::ENABLED) {
		const auto &reference = plan.SecretReference();
		if (!reference.IsPresent()) {
			throw std::logic_error("authenticated scan plan has no logical secret reference");
		}
		auto authorization = ResolveDuckdbApiSecret(context, reference.Name());
		return bind_data.executor->OpenWithAuthorization(plan, std::move(authorization), control);
	}
	if (plan.Authentication() != duckdb_api::FeatureState::DISABLED) {
		throw std::logic_error("scan plan has an unknown authentication state");
	}
	return bind_data.executor->OpenWithAuthorization(plan, duckdb_api::ScanAuthorization::Anonymous(), control);
}

unique_ptr<GlobalTableFunctionState> DuckdbApiInit(ClientContext &context, TableFunctionInitInput &input) {
	auto &bind_data = input.bind_data->Cast<DuckdbApiBindData>();
	try {
		DuckdbExecutionControl control(context);
		auto stream = OpenAuthorizedStream(bind_data, context, control);
		const auto &plan = bind_data.plan_state.SelectedPlan();
		if (!stream) {
			throw std::logic_error("scan executor returned no stream");
		}
		return make_uniq<DuckdbApiGlobalState>(std::move(stream), PlannedValueKinds(plan), plan.Budgets().batch_rows,
		                                       plan.ConnectorName(), plan.RelationName());
	} catch (const duckdb_api::ExecutionCancelled &) {
		ThrowCancellation(nullptr);
	} catch (const duckdb_api::ExecutionError &error) {
		const auto &plan = bind_data.plan_state.SelectedPlan();
		ThrowExecutionError(error, plan.ConnectorName(), plan.RelationName());
	} catch (const std::exception &) {
		const auto &plan = bind_data.plan_state.SelectedPlan();
		throw InvalidInputException("[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure",
		                            plan.ConnectorName(), plan.RelationName());
	} catch (...) {
		const auto &plan = bind_data.plan_state.SelectedPlan();
		throw InvalidInputException("[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure",
		                            plan.ConnectorName(), plan.RelationName());
	}
}

Value DuckdbValue(const duckdb_api::TypedValue &value) {
	switch (value.kind) {
	case duckdb_api::ValueKind::BIGINT:
		return Value::BIGINT(value.bigint_value);
	case duckdb_api::ValueKind::VARCHAR:
		return Value(value.varchar_value);
	case duckdb_api::ValueKind::BOOLEAN:
		return Value::BOOLEAN(value.boolean_value);
	}
	throw InternalException("duckdb_api runtime returned an unknown typed value");
}

void DuckdbApiScan(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<DuckdbApiGlobalState>();
	duckdb_api::TypedBatch batch;
	try {
		DuckdbExecutionControl control(context);
		const auto produced = state.stream->Next(control, batch);
		if (!produced) {
			if (!batch.rows.empty()) {
				throw std::logic_error("batch stream returned rows with clean exhaustion");
			}
			state.finished = true;
			return;
		}
		// DuckDB treats a zero-cardinality table-function output as source
		// exhaustion and will not pull again. Runtime therefore owns crossing an
		// empty nonterminal source page inside the active Next call; a successful
		// empty batch is a provider-contract violation, not clean exhaustion.
		if (batch.rows.empty() || !batch.IsSchemaAligned() || batch.column_kinds != state.expected_kinds ||
		    batch.rows.size() > state.max_batch_rows || batch.rows.size() > STANDARD_VECTOR_SIZE) {
			throw std::logic_error("batch stream violated its bound schema or row ceiling");
		}
		for (idx_t row_index = 0; row_index < batch.rows.size(); row_index++) {
			if (control.IsCancellationRequested()) {
				throw duckdb_api::ExecutionCancelled();
			}
			for (idx_t column_index = 0; column_index < batch.rows[row_index].values.size(); column_index++) {
				output.SetValue(column_index, row_index, DuckdbValue(batch.rows[row_index].values[column_index]));
			}
		}
		output.SetCardinality(batch.rows.size());
	} catch (const duckdb_api::ExecutionCancelled &) {
		ThrowCancellation(state.stream.get());
	} catch (const InterruptException &) {
		ThrowCancellation(state.stream.get());
	} catch (const duckdb_api::ExecutionError &error) {
		ThrowExecutionError(error, state.connector_name, state.relation_name);
	} catch (const std::exception &) {
		state.stream->Cancel();
		throw InvalidInputException("[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure",
		                            state.connector_name, state.relation_name);
	} catch (...) {
		state.stream->Cancel();
		throw InvalidInputException("[duckdb_api][internal] connector=%s relation=%s: unexpected execution failure",
		                            state.connector_name, state.relation_name);
	}
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
