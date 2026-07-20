#include "query/rfc0012/coordinator_trial_support.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/duck_schema_entry.hpp"
#include "duckdb/catalog/catalog_entry/table_function_catalog_entry.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/catalog/catalog_set.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/client_context_state.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/parsed_data/drop_info.hpp"
#include "duckdb/planner/extension_callback.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace duckdb_api_test {
namespace rfc0012 {

struct RegistryGeneration final {
	RegistryGeneration(std::uint64_t id_p, std::vector<std::string> relations_p,
	                   std::shared_ptr<CoordinatorTrialControl> control_p)
	    : id(id_p), relations(std::move(relations_p)), control(std::move(control_p)) {
		control->GenerationCreated();
	}

	~RegistryGeneration() {
		control->GenerationDestroyed();
	}

	std::uint64_t id;
	std::vector<std::string> relations;
	std::shared_ptr<CoordinatorTrialControl> control;
};

namespace {

const char *const PUBLISH_FUNCTION = "rfc0012_publish_generation";
const char *const INVENTORY_FUNCTION = "rfc0012_generation_inventory";
const char *const PUBLICATION_STATE_KEY = "duckdb_api_rfc0012_publication_state";

enum class FunctionRole : std::uint8_t {
	PUBLISH,
	INVENTORY,
	RELATION,
};

struct GenerationFunctionInfo final : public duckdb::TableFunctionInfo {
	GenerationFunctionInfo(std::shared_ptr<const RegistryGeneration> generation_p,
	                       std::shared_ptr<CatalogGenerationCoordinator> coordinator_p, FunctionRole role_p,
	                       std::string relation_p = std::string())
	    : generation(std::move(generation_p)), coordinator(std::move(coordinator_p)), role(role_p),
	      relation(std::move(relation_p)) {
	}

	std::shared_ptr<const RegistryGeneration> generation;
	std::shared_ptr<CatalogGenerationCoordinator> coordinator;
	FunctionRole role;
	std::string relation;
};

struct RelationBindData final : public duckdb::TableFunctionData {
	RelationBindData(std::shared_ptr<const RegistryGeneration> generation_p, std::string relation_p,
	                 duckdb::Value input_p)
	    : generation(std::move(generation_p)), relation(std::move(relation_p)), input(std::move(input_p)) {
	}

	duckdb::unique_ptr<duckdb::FunctionData> Copy() const override {
		return duckdb::make_uniq<RelationBindData>(generation, relation, input);
	}

	bool Equals(const duckdb::FunctionData &other_p) const override {
		auto &other = other_p.Cast<RelationBindData>();
		return generation == other.generation && relation == other.relation && input == other.input;
	}

	std::shared_ptr<const RegistryGeneration> generation;
	std::string relation;
	duckdb::Value input;
};

struct InventoryBindData final : public duckdb::TableFunctionData {
	explicit InventoryBindData(std::shared_ptr<const RegistryGeneration> generation_p)
	    : generation(std::move(generation_p)) {
	}

	duckdb::unique_ptr<duckdb::FunctionData> Copy() const override {
		return duckdb::make_uniq<InventoryBindData>(generation);
	}

	bool Equals(const duckdb::FunctionData &other_p) const override {
		return generation == other_p.Cast<InventoryBindData>().generation;
	}

	std::shared_ptr<const RegistryGeneration> generation;
};

struct PublishBindData final : public duckdb::TableFunctionData {
	PublishBindData(std::shared_ptr<const RegistryGeneration> base_p,
	                std::shared_ptr<CatalogGenerationCoordinator> coordinator_p, std::uint64_t requested_p,
	                std::int64_t fail_after_p, std::int64_t interrupt_after_p)
	    : base(std::move(base_p)), coordinator(std::move(coordinator_p)), requested(requested_p),
	      fail_after(fail_after_p), interrupt_after(interrupt_after_p) {
	}

	duckdb::unique_ptr<duckdb::FunctionData> Copy() const override {
		return duckdb::make_uniq<PublishBindData>(base, coordinator, requested, fail_after, interrupt_after);
	}

	bool Equals(const duckdb::FunctionData &other_p) const override {
		auto &other = other_p.Cast<PublishBindData>();
		return base == other.base && coordinator == other.coordinator && requested == other.requested &&
		       fail_after == other.fail_after && interrupt_after == other.interrupt_after;
	}

	bool SupportStatementCache() const override {
		// A prepared management statement must rebind to the currently visible
		// generation before every EXECUTE.
		return false;
	}

	std::shared_ptr<const RegistryGeneration> base;
	std::shared_ptr<CatalogGenerationCoordinator> coordinator;
	std::uint64_t requested;
	std::int64_t fail_after;
	std::int64_t interrupt_after;
};

struct SingleRowState final : public duckdb::GlobalTableFunctionState {
	SingleRowState() : emitted(false), completed(false), changed(false), generation(0), row(0) {
	}

	bool emitted;
	bool completed;
	bool changed;
	std::uint64_t generation;
	duckdb::idx_t row;
};

class PublicationContextState final : public duckdb::ClientContextState {
public:
	PublicationContextState() : query_id_(duckdb::DConstants::INVALID_INDEX), bind_count_(0) {
	}

	void RecordBind(duckdb::ClientContext &context) {
		const auto query_id = context.transaction.GetActiveQuery();
		if (query_id_ != query_id) {
			query_id_ = query_id;
			bind_count_ = 0;
		}
		bind_count_++;
		if (bind_count_ > 1) {
			throw duckdb::BinderException("RFC 0012 trial permits one lifecycle invocation per statement");
		}
	}

	void Hold(const std::shared_ptr<CatalogGenerationCoordinator> &coordinator,
	          std::unique_ptr<std::unique_lock<std::timed_mutex>> guard) {
		if (guard_) {
			throw duckdb::InternalException("RFC 0012 publication guard was already held");
		}
		coordinator_ = coordinator;
		guard_ = std::move(guard);
	}

	void TransactionCommit(duckdb::MetaTransaction &, duckdb::ClientContext &) override {
		guard_.reset();
		coordinator_.reset();
	}

	void TransactionRollback(duckdb::MetaTransaction &, duckdb::ClientContext &) override {
		guard_.reset();
		coordinator_.reset();
	}

private:
	duckdb::idx_t query_id_;
	duckdb::idx_t bind_count_;
	std::unique_ptr<std::unique_lock<std::timed_mutex>> guard_;
	std::shared_ptr<CatalogGenerationCoordinator> coordinator_;
};

std::vector<std::string> RelationsFor(std::uint64_t generation) {
	if (generation == 1) {
		return {"rfc0012_relation_a", "rfc0012_relation_b", "rfc0012_relation_old"};
	}
	return {"rfc0012_relation_a", "rfc0012_relation_b", "rfc0012_relation_c"};
}

duckdb::unique_ptr<duckdb::GlobalTableFunctionState> InitSingleRow(duckdb::ClientContext &,
                                                                   duckdb::TableFunctionInitInput &) {
	return duckdb::make_uniq<SingleRowState>();
}

duckdb::unique_ptr<duckdb::FunctionData> BindRelation(duckdb::ClientContext &, duckdb::TableFunctionBindInput &input,
                                                      duckdb::vector<duckdb::LogicalType> &return_types,
                                                      duckdb::vector<duckdb::string> &names) {
	if (!input.info) {
		throw duckdb::InternalException("RFC 0012 relation is missing immutable generation information");
	}
	auto &info = input.info->Cast<GenerationFunctionInfo>();
	if (info.role != FunctionRole::RELATION || !info.generation) {
		throw duckdb::InternalException("RFC 0012 relation has contradictory generation information");
	}
	return_types.push_back(duckdb::LogicalType::UBIGINT);
	return_types.push_back(duckdb::LogicalType::VARCHAR);
	return_types.push_back(duckdb::LogicalType::BIGINT);
	names.push_back("generation");
	names.push_back("relation");
	names.push_back("input");
	auto value = duckdb::Value(duckdb::LogicalType::BIGINT);
	auto value_entry = input.named_parameters.find("input");
	if (value_entry != input.named_parameters.end()) {
		value = value_entry->second;
	}
	return duckdb::make_uniq<RelationBindData>(info.generation, info.relation, std::move(value));
}

void ScanRelation(duckdb::ClientContext &, duckdb::TableFunctionInput &input, duckdb::DataChunk &output) {
	auto &state = input.global_state->Cast<SingleRowState>();
	if (state.emitted) {
		return;
	}
	auto &data = input.bind_data->Cast<RelationBindData>();
	data.generation->control->PauseRelationScanIfRequested();
	output.SetCardinality(1);
	output.SetValue(0, 0, duckdb::Value::UBIGINT(data.generation->id));
	output.SetValue(1, 0, data.relation);
	output.SetValue(2, 0, data.input);
	state.emitted = true;
}

duckdb::unique_ptr<duckdb::FunctionData> BindInventory(duckdb::ClientContext &, duckdb::TableFunctionBindInput &input,
                                                       duckdb::vector<duckdb::LogicalType> &return_types,
                                                       duckdb::vector<duckdb::string> &names) {
	if (!input.info) {
		throw duckdb::InternalException("RFC 0012 inventory is missing immutable generation information");
	}
	auto &info = input.info->Cast<GenerationFunctionInfo>();
	if (info.role != FunctionRole::INVENTORY || !info.generation) {
		throw duckdb::InternalException("RFC 0012 inventory has contradictory generation information");
	}
	return_types.push_back(duckdb::LogicalType::UBIGINT);
	return_types.push_back(duckdb::LogicalType::VARCHAR);
	names.push_back("generation");
	names.push_back("relation");
	return duckdb::make_uniq<InventoryBindData>(info.generation);
}

void ScanInventory(duckdb::ClientContext &, duckdb::TableFunctionInput &input, duckdb::DataChunk &output) {
	auto &state = input.global_state->Cast<SingleRowState>();
	auto &data = input.bind_data->Cast<InventoryBindData>();
	duckdb::idx_t count = 0;
	while (state.row < data.generation->relations.size() && count < STANDARD_VECTOR_SIZE) {
		output.SetValue(0, count, duckdb::Value::UBIGINT(data.generation->id));
		output.SetValue(1, count, data.generation->relations[state.row]);
		state.row++;
		count++;
	}
	output.SetCardinality(count);
}

duckdb::TableFunction BuildRelationFunction(const std::string &name,
                                            const std::shared_ptr<const RegistryGeneration> &generation,
                                            const std::shared_ptr<CatalogGenerationCoordinator> &coordinator) {
	duckdb::TableFunction function(name, {}, ScanRelation, BindRelation, InitSingleRow);
	function.named_parameters["input"] = duckdb::LogicalType::BIGINT;
	function.function_info =
	    duckdb::make_shared_ptr<GenerationFunctionInfo>(generation, coordinator, FunctionRole::RELATION, name);
	return function;
}

duckdb::TableFunction BuildInventoryFunction(const std::shared_ptr<const RegistryGeneration> &generation,
                                             const std::shared_ptr<CatalogGenerationCoordinator> &coordinator) {
	duckdb::TableFunction function(INVENTORY_FUNCTION, {}, ScanInventory, BindInventory, InitSingleRow);
	function.function_info =
	    duckdb::make_shared_ptr<GenerationFunctionInfo>(generation, coordinator, FunctionRole::INVENTORY);
	return function;
}

} // namespace

class CatalogGenerationCoordinator final : public std::enable_shared_from_this<CatalogGenerationCoordinator> {
public:
	explicit CatalogGenerationCoordinator(std::shared_ptr<CoordinatorTrialControl> control_p)
	    : control_(std::move(control_p)) {
	}

	bool Publish(duckdb::ClientContext &context, const std::shared_ptr<const RegistryGeneration> &base,
	             std::uint64_t requested, std::int64_t fail_after, std::int64_t interrupt_after) {
		if (closing_.load(std::memory_order_acquire)) {
			throw duckdb::InvalidInputException("RFC 0012 publication coordinator is closing");
		}
		if (requested == base->id) {
			return false;
		}
		auto state = context.registered_state->GetOrCreate<PublicationContextState>(PUBLICATION_STATE_KEY);
		state->Hold(shared_from_this(), AcquirePublication(context));

		auto candidate = std::make_shared<const RegistryGeneration>(requested, RelationsFor(requested), control_);
		control_->ObserveCandidate(candidate);
		PreflightUserMacros(context, candidate->relations);
		auto transaction = duckdb::CatalogTransaction::GetSystemCatalogTransaction(context);
		auto &catalog = duckdb::Catalog::GetSystemCatalog(context);
		auto &schema = catalog.GetSchema(transaction, DEFAULT_SCHEMA);
		auto &catalog_set =
		    schema.Cast<duckdb::DuckSchemaEntry>().GetCatalogSet(duckdb::CatalogType::TABLE_FUNCTION_ENTRY);

		RequireOwned(catalog_set, transaction, PUBLISH_FUNCTION, base);
		RequireOwned(catalog_set, transaction, INVENTORY_FUNCTION, base);
		for (const auto &name : base->relations) {
			RequireOwned(catalog_set, transaction, name, base);
		}
		for (const auto &name : candidate->relations) {
			if (std::find(base->relations.begin(), base->relations.end(), name) == base->relations.end()) {
				RequireVacant(catalog_set, transaction, name);
			}
		}
		control_->PauseAfterPreflightIfRequested();

		std::int64_t mutations = 0;
		for (const auto &name : base->relations) {
			if (std::find(candidate->relations.begin(), candidate->relations.end(), name) ==
			    candidate->relations.end()) {
				DropOwned(context, schema, catalog_set, transaction, name, base);
				CheckInjectedStop(++mutations, fail_after, interrupt_after);
			}
		}
		for (const auto &name : candidate->relations) {
			const auto replaces =
			    std::find(base->relations.begin(), base->relations.end(), name) != base->relations.end();
			if (replaces) {
				DropOwned(context, schema, catalog_set, transaction, name, base);
				CheckInjectedStop(++mutations, fail_after, interrupt_after);
			}
			Create(catalog, transaction, BuildRelationFunction(name, candidate, shared_from_this()));
			CheckInjectedStop(++mutations, fail_after, interrupt_after);
		}
		DropOwned(context, schema, catalog_set, transaction, INVENTORY_FUNCTION, base);
		CheckInjectedStop(++mutations, fail_after, interrupt_after);
		Create(catalog, transaction, BuildInventoryFunction(candidate, shared_from_this()));
		CheckInjectedStop(++mutations, fail_after, interrupt_after);
		DropOwned(context, schema, catalog_set, transaction, PUBLISH_FUNCTION, base);
		CheckInjectedStop(++mutations, fail_after, interrupt_after);
		Create(catalog, transaction, BuildPublishFunction(candidate));
		CheckInjectedStop(++mutations, fail_after, interrupt_after);
		return true;
	}

	duckdb::TableFunction BuildPublishFunction(const std::shared_ptr<const RegistryGeneration> &generation);

	void BeginClose() {
		closing_.store(true, std::memory_order_release);
	}

private:
	std::unique_ptr<std::unique_lock<std::timed_mutex>> AcquirePublication(duckdb::ClientContext &context) {
		auto guard = std::unique_ptr<std::unique_lock<std::timed_mutex>>(
		    new std::unique_lock<std::timed_mutex>(publication_mutex_, std::defer_lock));
		if (guard->try_lock()) {
			if (closing_.load(std::memory_order_acquire)) {
				throw duckdb::InvalidInputException("RFC 0012 publication coordinator is closing");
			}
			return guard;
		}
		control_->WaitingPublicationStarted();
		try {
			while (!guard->try_lock_for(std::chrono::milliseconds(10))) {
				if (context.IsInterrupted()) {
					throw duckdb::InterruptException();
				}
				if (closing_.load(std::memory_order_acquire)) {
					throw duckdb::InvalidInputException("RFC 0012 publication coordinator is closing");
				}
			}
		} catch (...) {
			control_->WaitingPublicationEnded();
			throw;
		}
		control_->WaitingPublicationEnded();
		if (closing_.load(std::memory_order_acquire)) {
			throw duckdb::InvalidInputException("RFC 0012 publication coordinator is closing");
		}
		return guard;
	}

	void CheckInjectedStop(std::int64_t mutations, std::int64_t fail_after, std::int64_t interrupt_after) {
		control_->PauseAfterMutationIfRequested(static_cast<std::uint64_t>(mutations));
		if (fail_after >= 0 && mutations == fail_after) {
			throw duckdb::InvalidInputException("RFC 0012 injected late catalog failure");
		}
		if (interrupt_after >= 0 && mutations == interrupt_after) {
			throw duckdb::InterruptException();
		}
	}

	static void PreflightUserMacros(duckdb::ClientContext &context, const std::vector<std::string> &names) {
		for (const auto &name : names) {
			duckdb::EntryLookupInfo lookup(duckdb::CatalogType::TABLE_MACRO_ENTRY, name);
			auto macro = duckdb::Catalog::GetEntry(context, INVALID_CATALOG, DEFAULT_SCHEMA, lookup,
			                                       duckdb::OnEntryNotFound::RETURN_NULL);
			if (macro && macro->type == duckdb::CatalogType::TABLE_MACRO_ENTRY) {
				throw duckdb::CatalogException("RFC 0012 generated name conflicts with table macro %s", name);
			}
		}
	}

	duckdb::CatalogEntry *RequireOwned(duckdb::CatalogSet &set, duckdb::CatalogTransaction transaction,
	                                   const std::string &name,
	                                   const std::shared_ptr<const RegistryGeneration> &generation) const {
		auto entry = set.GetEntry(transaction, name);
		if (!entry || entry->type != duckdb::CatalogType::TABLE_FUNCTION_ENTRY) {
			throw duckdb::CatalogException("RFC 0012 missing owned table function %s", name);
		}
		auto &functions = entry->Cast<duckdb::TableFunctionCatalogEntry>().functions.functions;
		if (functions.size() != 1 || !functions[0].function_info) {
			throw duckdb::CatalogException("RFC 0012 table function %s has an unrelated overload owner", name);
		}
		auto info = dynamic_cast<GenerationFunctionInfo *>(functions[0].function_info.get());
		if (!info || info->coordinator.get() != this || info->generation != generation) {
			throw duckdb::CatalogException("RFC 0012 table function %s has an unrelated generation owner", name);
		}
		return entry.get();
	}

	static void RequireVacant(duckdb::CatalogSet &set, duckdb::CatalogTransaction transaction,
	                          const std::string &name) {
		if (set.GetEntry(transaction, name)) {
			throw duckdb::CatalogException("RFC 0012 generated name conflicts with table function %s", name);
		}
	}

	void DropOwned(duckdb::ClientContext &context, duckdb::SchemaCatalogEntry &schema, duckdb::CatalogSet &set,
	               duckdb::CatalogTransaction transaction, const std::string &name,
	               const std::shared_ptr<const RegistryGeneration> &generation) const {
		auto expected = RequireOwned(set, transaction, name, generation);
		duckdb::DropInfo drop;
		drop.type = duckdb::CatalogType::TABLE_FUNCTION_ENTRY;
		drop.schema = DEFAULT_SCHEMA;
		drop.name = name;
		drop.allow_drop_internal = true;
		schema.DropEntry(context, drop);
		if (!expected->HasParent()) {
			throw duckdb::InternalException("RFC 0012 conditional drop did not create a catalog parent");
		}
		auto &parent = expected->Parent();
		if (!parent.deleted || parent.timestamp != transaction.transaction_id) {
			throw duckdb::CatalogException("RFC 0012 table function %s changed owner during publication", name);
		}
	}

	static void Create(duckdb::Catalog &catalog, duckdb::CatalogTransaction transaction,
	                   duckdb::TableFunction function) {
		duckdb::CreateTableFunctionInfo info(std::move(function));
		info.on_conflict = duckdb::OnCreateConflict::ERROR_ON_CONFLICT;
		catalog.CreateFunction(transaction, info);
	}

	std::shared_ptr<CoordinatorTrialControl> control_;
	std::timed_mutex publication_mutex_;
	std::atomic<bool> closing_ {false};
};

// DuckDB 1.5.4 keeps DatabaseInstance alive through every Connection and
// active query. Consequently the database-owned callback can only be
// destroyed after publication work is quiescent. Its destructor is the real
// pinned lifecycle hook that closes the coordinator before releasing its
// final DatabaseInstance-owned reference.
class CoordinatorDatabaseLifecycle final : public duckdb::ExtensionCallback {
public:
	CoordinatorDatabaseLifecycle(std::shared_ptr<CatalogGenerationCoordinator> coordinator_p,
	                             std::shared_ptr<CoordinatorTrialControl> control_p)
	    : coordinator_(std::move(coordinator_p)), control_(std::move(control_p)) {
	}

	~CoordinatorDatabaseLifecycle() override {
		coordinator_->BeginClose();
		control_->ObserveDatabaseShutdown();
	}

private:
	std::shared_ptr<CatalogGenerationCoordinator> coordinator_;
	std::shared_ptr<CoordinatorTrialControl> control_;
};

namespace {

duckdb::unique_ptr<duckdb::FunctionData> BindPublish(duckdb::ClientContext &context,
                                                     duckdb::TableFunctionBindInput &input,
                                                     duckdb::vector<duckdb::LogicalType> &return_types,
                                                     duckdb::vector<duckdb::string> &names) {
	if (!context.transaction.IsAutoCommit()) {
		throw duckdb::BinderException("RFC 0012 lifecycle functions require autocommit");
	}
	if (!input.info) {
		throw duckdb::InternalException("RFC 0012 publisher is missing immutable generation information");
	}
	auto &info = input.info->Cast<GenerationFunctionInfo>();
	if (info.role != FunctionRole::PUBLISH || !info.generation || !info.coordinator) {
		throw duckdb::InternalException("RFC 0012 publisher has contradictory generation information");
	}
	auto generation_entry = input.named_parameters.find("generation");
	if (generation_entry == input.named_parameters.end() || generation_entry->second.IsNull()) {
		throw duckdb::BinderException("RFC 0012 publisher requires generation UBIGINT");
	}
	std::int64_t fail_after = -1;
	auto failure_entry = input.named_parameters.find("fail_after");
	if (failure_entry != input.named_parameters.end()) {
		if (failure_entry->second.IsNull()) {
			throw duckdb::BinderException("RFC 0012 fail_after cannot be NULL");
		}
		fail_after = failure_entry->second.GetValue<std::int64_t>();
	}
	std::int64_t interrupt_after = -1;
	auto interrupt_entry = input.named_parameters.find("interrupt_after");
	if (interrupt_entry != input.named_parameters.end()) {
		if (interrupt_entry->second.IsNull()) {
			throw duckdb::BinderException("RFC 0012 interrupt_after cannot be NULL");
		}
		interrupt_after = interrupt_entry->second.GetValue<std::int64_t>();
	}
	auto state = context.registered_state->GetOrCreate<PublicationContextState>(PUBLICATION_STATE_KEY);
	state->RecordBind(context);
	return_types.push_back(duckdb::LogicalType::UBIGINT);
	return_types.push_back(duckdb::LogicalType::BOOLEAN);
	names.push_back("generation");
	names.push_back("changed");
	return duckdb::make_uniq<PublishBindData>(info.generation, info.coordinator,
	                                          generation_entry->second.GetValue<std::uint64_t>(), fail_after,
	                                          interrupt_after);
}

void ScanPublish(duckdb::ClientContext &context, duckdb::TableFunctionInput &input, duckdb::DataChunk &output) {
	auto &state = input.global_state->Cast<SingleRowState>();
	if (state.emitted) {
		return;
	}
	auto &data = input.bind_data->Cast<PublishBindData>();
	if (!state.completed) {
		state.changed =
		    data.coordinator->Publish(context, data.base, data.requested, data.fail_after, data.interrupt_after);
		state.generation = data.requested;
		state.completed = true;
	}
	output.SetCardinality(1);
	output.SetValue(0, 0, duckdb::Value::UBIGINT(state.generation));
	output.SetValue(1, 0, duckdb::Value::BOOLEAN(state.changed));
	state.emitted = true;
}

} // namespace

duckdb::TableFunction
CatalogGenerationCoordinator::BuildPublishFunction(const std::shared_ptr<const RegistryGeneration> &generation) {
	duckdb::TableFunction function(PUBLISH_FUNCTION, {}, ScanPublish, BindPublish, InitSingleRow);
	function.named_parameters["generation"] = duckdb::LogicalType::UBIGINT;
	function.named_parameters["fail_after"] = duckdb::LogicalType::BIGINT;
	function.named_parameters["interrupt_after"] = duckdb::LogicalType::BIGINT;
	function.function_info =
	    duckdb::make_shared_ptr<GenerationFunctionInfo>(generation, shared_from_this(), FunctionRole::PUBLISH);
	return function;
}

namespace {

duckdb::unique_ptr<duckdb::FunctionData> BindUnrelated(duckdb::ClientContext &, duckdb::TableFunctionBindInput &,
                                                       duckdb::vector<duckdb::LogicalType> &return_types,
                                                       duckdb::vector<duckdb::string> &names) {
	return_types.push_back(duckdb::LogicalType::UBIGINT);
	names.push_back("unrelated");
	class UnrelatedBindData final : public duckdb::TableFunctionData {
	public:
		duckdb::unique_ptr<duckdb::FunctionData> Copy() const override {
			return duckdb::make_uniq<UnrelatedBindData>();
		}

		bool Equals(const duckdb::FunctionData &other) const override {
			return dynamic_cast<const UnrelatedBindData *>(&other) != nullptr;
		}
	};
	return duckdb::make_uniq<UnrelatedBindData>();
}

void ScanUnrelated(duckdb::ClientContext &, duckdb::TableFunctionInput &input, duckdb::DataChunk &output) {
	auto &state = input.global_state->Cast<SingleRowState>();
	if (state.emitted) {
		return;
	}
	output.SetCardinality(1);
	output.SetValue(0, 0, duckdb::Value::UBIGINT(999));
	state.emitted = true;
}

} // namespace

CoordinatorTrialControl::CoordinatorTrialControl()
    : pause_requested_(false), pause_after_mutation_(0), pause_reached_(false), resume_requested_(false),
      live_generations_(0), waiting_publications_(0), database_shutdown_observed_(false), scan_pause_requested_(false),
      scan_pause_reached_(false), scan_resume_requested_(false) {
}

void CoordinatorTrialControl::PauseNextPublicationAfterPreflight() {
	std::lock_guard<std::mutex> guard(pause_mutex_);
	pause_requested_ = true;
	pause_after_mutation_ = 0;
	pause_reached_ = false;
	resume_requested_ = false;
}

void CoordinatorTrialControl::PauseNextPublicationAfterMutation(std::uint64_t mutation) {
	if (mutation == 0) {
		throw std::invalid_argument("publication mutation pause must be positive");
	}
	std::lock_guard<std::mutex> guard(pause_mutex_);
	pause_requested_ = true;
	pause_after_mutation_ = mutation;
	pause_reached_ = false;
	resume_requested_ = false;
}

bool CoordinatorTrialControl::WaitForPublicationPause(std::chrono::milliseconds timeout) {
	std::unique_lock<std::mutex> guard(pause_mutex_);
	return pause_condition_.wait_for(guard, timeout, [&]() { return pause_reached_; });
}

void CoordinatorTrialControl::ResumePublication() {
	std::lock_guard<std::mutex> guard(pause_mutex_);
	resume_requested_ = true;
	pause_condition_.notify_all();
}

void CoordinatorTrialControl::PauseAfterPreflightIfRequested() {
	std::unique_lock<std::mutex> guard(pause_mutex_);
	if (!pause_requested_ || pause_after_mutation_ != 0) {
		return;
	}
	pause_reached_ = true;
	pause_condition_.notify_all();
	pause_condition_.wait(guard, [&]() { return resume_requested_; });
	pause_requested_ = false;
	pause_after_mutation_ = 0;
	pause_reached_ = false;
	resume_requested_ = false;
}

void CoordinatorTrialControl::PauseAfterMutationIfRequested(std::uint64_t mutation) {
	std::unique_lock<std::mutex> guard(pause_mutex_);
	if (!pause_requested_ || pause_after_mutation_ != mutation) {
		return;
	}
	pause_reached_ = true;
	pause_condition_.notify_all();
	pause_condition_.wait(guard, [&]() { return resume_requested_; });
	pause_requested_ = false;
	pause_after_mutation_ = 0;
	pause_reached_ = false;
	resume_requested_ = false;
}

void CoordinatorTrialControl::ObserveCandidate(const std::shared_ptr<const RegistryGeneration> &candidate) {
	std::lock_guard<std::mutex> guard(candidate_mutex_);
	last_candidate_ = candidate;
}

bool CoordinatorTrialControl::LastCandidateReleased() const {
	std::lock_guard<std::mutex> guard(candidate_mutex_);
	return last_candidate_.expired();
}

bool CoordinatorTrialControl::DatabaseShutdownObserved() const {
	return database_shutdown_observed_.load(std::memory_order_acquire);
}

std::uint64_t CoordinatorTrialControl::LiveGenerationCount() const {
	return live_generations_.load(std::memory_order_relaxed);
}

std::uint64_t CoordinatorTrialControl::WaitingPublicationCount() const {
	return waiting_publications_.load(std::memory_order_relaxed);
}

void CoordinatorTrialControl::GenerationCreated() {
	live_generations_.fetch_add(1, std::memory_order_relaxed);
}

void CoordinatorTrialControl::GenerationDestroyed() {
	live_generations_.fetch_sub(1, std::memory_order_relaxed);
}

void CoordinatorTrialControl::WaitingPublicationStarted() {
	waiting_publications_.fetch_add(1, std::memory_order_relaxed);
}

void CoordinatorTrialControl::WaitingPublicationEnded() {
	waiting_publications_.fetch_sub(1, std::memory_order_relaxed);
}

void CoordinatorTrialControl::AttachCoordinator(const std::shared_ptr<CatalogGenerationCoordinator> &coordinator) {
	std::lock_guard<std::mutex> guard(candidate_mutex_);
	coordinator_ = coordinator;
}

void CoordinatorTrialControl::ObserveDatabaseShutdown() {
	database_shutdown_observed_.store(true, std::memory_order_release);
}

void CoordinatorTrialControl::BeginShutdown() {
	std::shared_ptr<CatalogGenerationCoordinator> coordinator;
	{
		std::lock_guard<std::mutex> guard(candidate_mutex_);
		coordinator = coordinator_.lock();
	}
	if (coordinator) {
		coordinator->BeginClose();
	}
}

void CoordinatorTrialControl::PauseNextRelationScan() {
	std::lock_guard<std::mutex> guard(scan_mutex_);
	scan_pause_requested_ = true;
	scan_pause_reached_ = false;
	scan_resume_requested_ = false;
}

bool CoordinatorTrialControl::WaitForRelationScanPause(std::chrono::milliseconds timeout) {
	std::unique_lock<std::mutex> guard(scan_mutex_);
	return scan_condition_.wait_for(guard, timeout, [&]() { return scan_pause_reached_; });
}

void CoordinatorTrialControl::ResumeRelationScan() {
	std::lock_guard<std::mutex> guard(scan_mutex_);
	scan_resume_requested_ = true;
	scan_condition_.notify_all();
}

void CoordinatorTrialControl::PauseRelationScanIfRequested() {
	std::unique_lock<std::mutex> guard(scan_mutex_);
	if (!scan_pause_requested_ || scan_pause_reached_) {
		return;
	}
	scan_pause_reached_ = true;
	scan_condition_.notify_all();
	scan_condition_.wait(guard, [&]() { return scan_resume_requested_; });
	scan_pause_requested_ = false;
	scan_pause_reached_ = false;
	scan_resume_requested_ = false;
}

std::shared_ptr<CoordinatorTrialControl> RegisterNativeCoordinatorTrial(duckdb::DuckDB &database) {
	auto control = std::make_shared<CoordinatorTrialControl>();
	auto coordinator = std::make_shared<CatalogGenerationCoordinator>(control);
	control->AttachCoordinator(coordinator);
	auto empty = std::make_shared<const RegistryGeneration>(0, std::vector<std::string>(), control);
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_rfc0012_coordinator_trial");
	loader.RegisterFunction(coordinator->BuildPublishFunction(empty));
	loader.RegisterFunction(BuildInventoryFunction(empty, coordinator));
	duckdb::ExtensionCallback::Register(database.instance->config,
	                                    duckdb::make_shared_ptr<CoordinatorDatabaseLifecycle>(coordinator, control));
	return control;
}

void RegisterUnrelatedSystemFunction(duckdb::DuckDB &database, const std::string &name) {
	duckdb::TableFunction function(name, {}, ScanUnrelated, BindUnrelated, InitSingleRow);
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_rfc0012_unrelated_trial");
	loader.RegisterFunction(std::move(function));
}

void RegisterUnrelatedSystemOverload(duckdb::DuckDB &database, const std::string &name) {
	duckdb::TableFunction function(name, {duckdb::LogicalType::BIGINT}, ScanUnrelated, BindUnrelated, InitSingleRow);
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_rfc0012_unrelated_overload_trial");
	loader.RegisterFunction(std::move(function));
}

} // namespace rfc0012
} // namespace duckdb_api_test
