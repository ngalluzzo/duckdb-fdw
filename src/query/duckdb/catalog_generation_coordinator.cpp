#include "catalog_generation_coordinator.hpp"

#include "generated_relation_adapter.hpp"
#include "package_catalog_snapshot.hpp"
#include "package_introspection_functions.hpp"
#include "package_management_functions.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/duck_schema_entry.hpp"
#include "duckdb/catalog/catalog_entry/table_function_catalog_entry.hpp"
#include "duckdb/catalog/catalog_set.hpp"
#include "duckdb/catalog/catalog_transaction.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/client_context_state.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/parsed_data/drop_info.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace duckdb {
namespace duckdb_api_query_internal {
namespace {

const char *const PUBLICATION_STATE_KEY = "duckdb_api_package_publication_state";

struct OwnedFunctionExpectation final {
	std::string name;
	PackageCatalogFunctionKind kind;
	std::shared_ptr<const duckdb_api::QueryPublishedGeneration> generation;
	const duckdb_api::CompiledRegistrationRelation *relation;
};

class PackagePublicationContextState final : public ClientContextState {
public:
	~PackagePublicationContextState() override {
		Discard();
	}

	void RecordBind(ClientContext &context) {
		const auto active_query = context.transaction.GetActiveQuery();
		if (query_id != active_query) {
			query_id = active_query;
			bind_count = 0;
		}
		bind_count++;
		if (bind_count > 1) {
			throw BinderException("[duckdb_api][bind] one load or reload invocation is permitted per statement");
		}
	}

	void Hold(std::shared_ptr<CatalogGenerationCoordinator> coordinator_p,
	          std::unique_ptr<std::unique_lock<std::timed_mutex>> guard_p,
	          std::unique_ptr<duckdb_api::QueryPublicationLease> lease_p) {
		if (!lease_p) {
			throw InternalException("duckdb_api publication requires a staged lease");
		}
		if (guard || lease) {
			throw InternalException("duckdb_api publication guard is already held by this context");
		}
		coordinator = std::move(coordinator_p);
		guard = std::move(guard_p);
		lease = std::move(lease_p);
	}

	void TransactionCommit(MetaTransaction &, ClientContext &) override {
		if (lease) {
			lease->Commit();
			lease.reset();
		}
		guard.reset();
		coordinator.reset();
	}

	void TransactionRollback(MetaTransaction &, ClientContext &) override {
		Discard();
	}

private:
	void Discard() noexcept {
		if (lease) {
			lease->Discard();
			lease.reset();
		}
		guard.reset();
		coordinator.reset();
	}

	idx_t query_id = DConstants::INVALID_INDEX;
	idx_t bind_count = 0;
	std::shared_ptr<CatalogGenerationCoordinator> coordinator;
	std::unique_ptr<std::unique_lock<std::timed_mutex>> guard;
	std::unique_ptr<duckdb_api::QueryPublicationLease> lease;
};

std::vector<OwnedFunctionExpectation> ExpectedFunctions(const std::shared_ptr<const PackageCatalogSnapshot> &snapshot) {
	std::vector<OwnedFunctionExpectation> result = {
	    {"duckdb_api_load_connector", PackageCatalogFunctionKind::LOAD, nullptr, nullptr},
	    {"duckdb_api_reload_connector", PackageCatalogFunctionKind::RELOAD, nullptr, nullptr},
	    {"duckdb_api_loaded_connectors", PackageCatalogFunctionKind::LOADED_CONNECTORS, nullptr, nullptr},
	    {"duckdb_api_loaded_relations", PackageCatalogFunctionKind::LOADED_RELATIONS, nullptr, nullptr},
	    {"duckdb_api_relation_arguments", PackageCatalogFunctionKind::RELATION_ARGUMENTS, nullptr, nullptr},
	};
	for (const auto &generation : snapshot->Generations()) {
		const auto &registration = generation->Registration();
		for (const auto &relation : registration.Relations()) {
			result.push_back({GeneratedRelationName(registration.Identity(), relation),
			                  PackageCatalogFunctionKind::GENERATED_RELATION, generation, &relation});
		}
	}
	return result;
}

CatalogEntry *RequireOwned(CatalogSet &set, CatalogTransaction transaction, const OwnedFunctionExpectation &expected,
                           const CatalogGenerationCoordinator *coordinator,
                           const std::shared_ptr<const PackageCatalogSnapshot> &snapshot) {
	auto entry = set.GetEntry(transaction, expected.name);
	if (!entry || entry->type != CatalogType::TABLE_FUNCTION_ENTRY) {
		throw CatalogException("[duckdb_api][publication] missing owned function %s", expected.name);
	}
	auto &functions = entry->Cast<TableFunctionCatalogEntry>().functions.functions;
	if (functions.size() != 1 || !functions[0].function_info) {
		throw CatalogException("[duckdb_api][publication] function %s has an unrelated overload owner", expected.name);
	}
	auto info = dynamic_cast<PackageCatalogFunctionInfo *>(functions[0].function_info.get());
	const auto snapshot_matches =
	    info && (expected.kind == PackageCatalogFunctionKind::GENERATED_RELATION || info->snapshot == snapshot);
	if (!info || info->coordinator.get() != coordinator || !snapshot_matches || info->kind != expected.kind ||
	    info->generation != expected.generation || info->relation != expected.relation) {
		throw CatalogException("[duckdb_api][publication] function %s has an unrelated catalog owner", expected.name);
	}
	return entry.get();
}

void RequireVacant(CatalogSet &set, CatalogTransaction transaction, const std::string &name) {
	if (set.GetEntry(transaction, name)) {
		throw CatalogException("[duckdb_api][publication] generated function %s conflicts with a table function", name);
	}
}

void PreflightMacros(ClientContext &context, const duckdb_api::QueryPublishedGeneration &candidate) {
	const auto &registration = candidate.Registration();
	for (const auto &relation : registration.Relations()) {
		const auto name = GeneratedRelationName(registration.Identity(), relation);
		EntryLookupInfo lookup(CatalogType::TABLE_MACRO_ENTRY, name);
		auto macro = Catalog::GetEntry(context, INVALID_CATALOG, DEFAULT_SCHEMA, lookup, OnEntryNotFound::RETURN_NULL);
		if (macro && macro->type == CatalogType::TABLE_MACRO_ENTRY) {
			throw CatalogException("[duckdb_api][publication] generated function %s conflicts with a table macro",
			                       name);
		}
	}
}

void DropOwned(ClientContext &context, SchemaCatalogEntry &schema, CatalogSet &set, CatalogTransaction transaction,
               const OwnedFunctionExpectation &expected, const CatalogGenerationCoordinator *coordinator,
               const std::shared_ptr<const PackageCatalogSnapshot> &snapshot) {
	auto entry = RequireOwned(set, transaction, expected, coordinator, snapshot);
	DropInfo drop;
	drop.type = CatalogType::TABLE_FUNCTION_ENTRY;
	drop.schema = DEFAULT_SCHEMA;
	drop.name = expected.name;
	drop.allow_drop_internal = true;
	schema.DropEntry(context, drop);
	if (!entry->HasParent()) {
		throw InternalException("duckdb_api conditional catalog drop did not preserve an MVCC parent");
	}
	auto &parent = entry->Parent();
	if (!parent.deleted || parent.timestamp != transaction.transaction_id) {
		throw CatalogException("[duckdb_api][publication] function %s changed owner during publication", expected.name);
	}
}

void Create(Catalog &catalog, CatalogTransaction transaction, TableFunction function) {
	CreateTableFunctionInfo info(std::move(function));
	info.on_conflict = OnCreateConflict::ERROR_ON_CONFLICT;
	catalog.CreateFunction(transaction, info);
}

std::unique_ptr<std::unique_lock<std::timed_mutex>> AcquirePublication(ClientContext &context, std::timed_mutex &mutex,
                                                                       const std::atomic<bool> &closing) {
	if (closing.load(std::memory_order_acquire)) {
		throw InvalidInputException("[duckdb_api][publication] package coordinator is closing");
	}
	auto guard = std::unique_ptr<std::unique_lock<std::timed_mutex>>(
	    new std::unique_lock<std::timed_mutex>(mutex, std::defer_lock));
	while (!guard->try_lock_for(std::chrono::milliseconds(10))) {
		if (context.IsInterrupted()) {
			throw InterruptException();
		}
		if (closing.load(std::memory_order_acquire)) {
			throw InvalidInputException("[duckdb_api][publication] package coordinator is closing");
		}
	}
	if (closing.load(std::memory_order_acquire)) {
		throw InvalidInputException("[duckdb_api][publication] package coordinator is closing");
	}
	return guard;
}

} // namespace

CatalogGenerationCoordinator::CatalogGenerationCoordinator(
    std::shared_ptr<const duckdb_api::QueryPackageStagingService> staging_p)
    : staging(std::move(staging_p)) {
	if (!staging) {
		throw std::invalid_argument("Query catalog coordinator requires a staging service");
	}
}

void CatalogGenerationCoordinator::RecordManagementBind(ClientContext &context) {
	auto state = context.registered_state->GetOrCreate<PackagePublicationContextState>(PUBLICATION_STATE_KEY);
	state->RecordBind(context);
}

void CatalogGenerationCoordinator::Publish(ClientContext &context,
                                           const std::shared_ptr<const PackageCatalogSnapshot> &base,
                                           duckdb_api::QueryStagedGeneration &staged, PackagePublicationIntent intent) {
	if (!base) {
		throw InternalException("duckdb_api publication is missing its base catalog snapshot");
	}
	auto guard = AcquirePublication(context, publication_mutex, closing);

	const auto &candidate = staged.Generation();
	std::shared_ptr<const PackageCatalogSnapshot> target;
	try {
		target = intent == PackagePublicationIntent::LOAD ? PackageCatalogSnapshot::Load(base, candidate)
		                                                  : PackageCatalogSnapshot::Reload(base, candidate);
	} catch (const std::invalid_argument &error) {
		throw InvalidInputException("[duckdb_api][publication] %s", error.what());
	}

	auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);
	auto &catalog = Catalog::GetSystemCatalog(context);
	auto &schema = catalog.GetSchema(transaction, DEFAULT_SCHEMA);
	auto &set = schema.Cast<DuckSchemaEntry>().GetCatalogSet(CatalogType::TABLE_FUNCTION_ENTRY);
	const auto old_functions = ExpectedFunctions(base);
	for (const auto &expected : old_functions) {
		(void)RequireOwned(set, transaction, expected, this, base);
	}

	const auto &connector = candidate->Registration().Identity().ConnectorId();
	auto active = base->Find(connector);
	if (intent == PackagePublicationIntent::LOAD && active) {
		throw InvalidInputException("[duckdb_api][publication] connector is already active");
	}
	if (intent == PackagePublicationIntent::RELOAD && !active) {
		throw InvalidInputException("[duckdb_api][publication] connector is not active");
	}
	if (!staged.Changed()) {
		if (intent != PackagePublicationIntent::RELOAD || !active ||
		    !candidate->Registration().GenerationHandle().IsSameGeneration(active->Registration().GenerationHandle())) {
			throw InvalidInputException("[duckdb_api][publication] staging returned an invalid unchanged generation");
		}
		return;
	}
	if (!staged.PublicationLease()) {
		throw InvalidInputException("[duckdb_api][publication] changed generation has no publication lease");
	}

	PreflightMacros(context, *candidate);
	std::vector<std::string> replaceable_names;
	if (active) {
		for (const auto &relation : active->Registration().Relations()) {
			replaceable_names.push_back(GeneratedRelationName(active->Registration().Identity(), relation));
		}
	}
	for (const auto &relation : candidate->Registration().Relations()) {
		const auto name = GeneratedRelationName(candidate->Registration().Identity(), relation);
		if (std::find(replaceable_names.begin(), replaceable_names.end(), name) == replaceable_names.end()) {
			RequireVacant(set, transaction, name);
		}
	}
	auto context_state = context.registered_state->GetOrCreate<PackagePublicationContextState>(PUBLICATION_STATE_KEY);
	context_state->Hold(shared_from_this(), std::move(guard), staged.TakePublicationLease());

	if (active) {
		for (const auto &relation : active->Registration().Relations()) {
			const OwnedFunctionExpectation expected {GeneratedRelationName(active->Registration().Identity(), relation),
			                                         PackageCatalogFunctionKind::GENERATED_RELATION, active, &relation};
			DropOwned(context, schema, set, transaction, expected, this, base);
		}
	}
	for (const auto &relation : candidate->Registration().Relations()) {
		Create(catalog, transaction, BuildGeneratedRelationFunction(shared_from_this(), target, candidate, relation));
	}

	for (const auto &expected : old_functions) {
		if (expected.kind == PackageCatalogFunctionKind::GENERATED_RELATION) {
			if (!active || expected.generation != active) {
				continue;
			}
			// Active connector functions were dropped above.
			continue;
		}
		DropOwned(context, schema, set, transaction, expected, this, base);
	}
	Create(catalog, transaction, BuildLoadConnectorFunction(shared_from_this(), target));
	Create(catalog, transaction, BuildReloadConnectorFunction(shared_from_this(), target));
	Create(catalog, transaction, BuildLoadedConnectorsFunction(shared_from_this(), target));
	Create(catalog, transaction, BuildLoadedRelationsFunction(shared_from_this(), target));
	Create(catalog, transaction, BuildRelationArgumentsFunction(shared_from_this(), target));
}

const std::shared_ptr<const duckdb_api::QueryPackageStagingService> &
CatalogGenerationCoordinator::Staging() const noexcept {
	return staging;
}

void CatalogGenerationCoordinator::BeginClose() noexcept {
	closing.store(true, std::memory_order_release);
}

bool CatalogGenerationCoordinator::IsClosing() const noexcept {
	return closing.load(std::memory_order_acquire);
}

} // namespace duckdb_api_query_internal
} // namespace duckdb
