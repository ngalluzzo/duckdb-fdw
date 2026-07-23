#include "query/packages/support/package_query_test_support.hpp"

#include "catalog_generation_coordinator.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb_api/duckdb_secret.hpp"
#include "duckdb_api/execution.hpp"
#include "duckdb_api/scan_planner.hpp"
#include "package_lifecycle_sentry.hpp"
#include "connector/support/package_generation_test_fixtures.hpp"
#include "connector/support/package_compiler_test_fixtures.hpp"
#include "support/require.hpp"

#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace duckdb_api_test {
namespace {

duckdb_api::ValueKind ValueKindFor(duckdb_api::PlannedColumnScalarKind kind) {
	switch (kind) {
	case duckdb_api::PlannedColumnScalarKind::BIGINT:
		return duckdb_api::ValueKind::BIGINT;
	case duckdb_api::PlannedColumnScalarKind::VARCHAR:
		return duckdb_api::ValueKind::VARCHAR;
	case duckdb_api::PlannedColumnScalarKind::BOOLEAN:
		return duckdb_api::ValueKind::BOOLEAN;
	case duckdb_api::PlannedColumnScalarKind::DOUBLE:
		return duckdb_api::ValueKind::DOUBLE;
	}
	throw std::logic_error("Query package test executor received an unsupported planned scalar type");
}

duckdb_api::TypedScalarValue MarkerElement(duckdb_api::ValueKind kind, const std::string &marker,
                                           const std::string &column) {
	switch (kind) {
	case duckdb_api::ValueKind::BIGINT:
		return duckdb_api::TypedScalarValue::BigInt(marker == "old" ? 1 : 2);
	case duckdb_api::ValueKind::VARCHAR:
		return duckdb_api::TypedScalarValue::Varchar(marker + ":" + column);
	case duckdb_api::ValueKind::BOOLEAN:
		return duckdb_api::TypedScalarValue::Boolean(marker != "old");
	case duckdb_api::ValueKind::DOUBLE:
		return duckdb_api::TypedScalarValue::Double(marker == "old" ? 1.5 : 2.5);
	}
	throw std::logic_error("Query package test executor received an unsupported ARRAY element type");
}

class PackagePlanningService final : public duckdb_api::QueryScanPlanningService {
public:
	PackagePlanningService(duckdb_api::CompiledGenerationHandle handle_p,
	                       std::shared_ptr<const duckdb_api::CompiledConnector> connector_p,
	                       std::shared_ptr<const duckdb_api::CompiledConnector> selective_connector_p,
	                       std::shared_ptr<PackageQueryProbe> probe_p)
	    : handle(std::move(handle_p)), connector(std::move(connector_p)),
	      selective_connector(std::move(selective_connector_p)), probe(std::move(probe_p)) {
	}

	duckdb_api::ScanPlan BuildPlan(const duckdb_api::CompiledGenerationHandle &candidate_handle,
	                               const duckdb_api::ScanRequest &request) const override {
		if (!candidate_handle.IsSameGeneration(handle)) {
			throw std::logic_error("Query package test planner received the wrong immutable generation");
		}
		probe->plans.fetch_add(1, std::memory_order_relaxed);
		const auto &selected =
		    request.capabilities.selective_predicate && selective_connector ? selective_connector : connector;
		if (!selected) {
			throw std::logic_error("Query registration-only fixture has no Semantics planning provider");
		}
		return duckdb_api::BuildConservativeScanPlan(*selected, request);
	}

private:
	const duckdb_api::CompiledGenerationHandle handle;
	const std::shared_ptr<const duckdb_api::CompiledConnector> connector;
	const std::shared_ptr<const duckdb_api::CompiledConnector> selective_connector;
	const std::shared_ptr<PackageQueryProbe> probe;
};

class PackageGenerationOwner final : public duckdb_api::QueryGenerationOwner {
public:
	PackageGenerationOwner(std::shared_ptr<const duckdb_api::CompiledQueryRegistrationView> registration_p,
	                       std::shared_ptr<PackageQueryProbe> probe_p)
	    : registration(std::move(registration_p)), probe(std::move(probe_p)) {
	}

	~PackageGenerationOwner() noexcept override {
		probe->generation_owners_destroyed.fetch_add(1, std::memory_order_relaxed);
	}

private:
	const std::shared_ptr<const duckdb_api::CompiledQueryRegistrationView> registration;
	const std::shared_ptr<PackageQueryProbe> probe;
};

// Runtime owns this concrete capability in production. The test lease proves
// that Query publishes a changed candidate only from DuckDB's commit callback
// and contains every rejected or rolled-back candidate as an exact-once
// discard.
class PackagePublicationLease final : public duckdb_api::QueryPublicationLease {
public:
	explicit PackagePublicationLease(std::shared_ptr<PackageQueryProbe> probe_p)
	    : probe(std::move(probe_p)), terminal(false) {
	}

	~PackagePublicationLease() noexcept override {
		Discard();
	}

	void Commit() noexcept override {
		if (!terminal.exchange(true, std::memory_order_acq_rel)) {
			probe->publication_commits.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void Discard() noexcept override {
		if (!terminal.exchange(true, std::memory_order_acq_rel)) {
			probe->publication_discards.fetch_add(1, std::memory_order_relaxed);
		}
	}

private:
	const std::shared_ptr<PackageQueryProbe> probe;
	std::atomic<bool> terminal;
};

class PackageRowStream final : public duckdb_api::BatchStream {
public:
	PackageRowStream(duckdb_api::ScanPlan plan_p, std::string marker_p, std::shared_ptr<PackageQueryProbe> probe_p)
	    : plan(std::move(plan_p)), marker(std::move(marker_p)), probe(std::move(probe_p)), emitted(false),
	      closed(false) {
	}

	~PackageRowStream() noexcept override {
		Close();
	}

	bool Next(duckdb_api::ExecutionControl &control, duckdb_api::TypedBatch &batch) override {
		batch.Clear();
		if (control.IsCancellationRequested()) {
			throw duckdb_api::ExecutionCancelled();
		}
		if (emitted || closed) {
			return false;
		}
		duckdb_api::TypedRow row;
		for (const auto &column : plan.OutputColumns()) {
			const auto kind = ValueKindFor(column.ElementKind());
			if (column.shape == duckdb_api::PlannedColumnShape::ARRAY) {
				batch.column_types.push_back(duckdb_api::OutputValueType::Array(kind, column.element_nullable));
				std::vector<duckdb_api::TypedScalarValue> elements;
				elements.push_back(MarkerElement(kind, marker, column.name));
				row.values.push_back(duckdb_api::TypedValue::Array(kind, column.element_nullable, std::move(elements)));
				continue;
			}
			batch.column_types.push_back(kind);
			switch (kind) {
			case duckdb_api::ValueKind::BIGINT:
				row.values.push_back(duckdb_api::TypedValue::BigInt(marker == "old" ? 1 : 2));
				break;
			case duckdb_api::ValueKind::VARCHAR:
				row.values.push_back(duckdb_api::TypedValue::Varchar(marker + ":" + column.name));
				break;
			case duckdb_api::ValueKind::BOOLEAN:
				row.values.push_back(duckdb_api::TypedValue::Boolean(marker != "old"));
				break;
			case duckdb_api::ValueKind::DOUBLE:
				row.values.push_back(duckdb_api::TypedValue::Double(marker == "old" ? 1.5 : 2.5));
				break;
			}
		}
		batch.rows.push_back(std::move(row));
		emitted = true;
		probe->rows.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	void Cancel() noexcept override {
	}

	void Close() noexcept override {
		if (!closed) {
			closed = true;
			probe->streams_closed.fetch_add(1, std::memory_order_relaxed);
		}
	}

private:
	const duckdb_api::ScanPlan plan;
	const std::string marker;
	const std::shared_ptr<PackageQueryProbe> probe;
	bool emitted;
	bool closed;
};

class PackageExecutor final : public duckdb_api::ScanExecutor {
public:
	PackageExecutor(std::string marker_p, std::shared_ptr<PackageQueryProbe> probe_p)
	    : marker(std::move(marker_p)), probe(std::move(probe_p)) {
	}

	std::unique_ptr<duckdb_api::BatchStream> Open(const duckdb_api::ScanPlan &,
	                                              duckdb_api::ExecutionControl &) const override {
		throw std::logic_error("Query package test executor received the legacy authorization path");
	}

protected:
	std::unique_ptr<duckdb_api::BatchStream>
	OpenAuthorizationEnvelope(const duckdb_api::ScanPlan &plan, duckdb_api::ScanAuthorization authorization,
	                          duckdb_api::ExecutionControl &control) const override {
		if (control.IsCancellationRequested()) {
			throw duckdb_api::ExecutionCancelled();
		}
		const auto alternative = AlternativeOf(authorization);
		// ResolveDuckdbApiSecret supplies the kind-neutral CREDENTIAL
		// alternative for every authenticated relation, not BEARER/
		// GITHUB_USER_BEARER specifically, so any non-anonymous alternative
		// is valid here.
		if ((plan.Authentication() == duckdb_api::FeatureState::ENABLED) !=
		    (alternative != AuthorizationAlternative::ANONYMOUS)) {
			throw std::logic_error("Query package test executor received the wrong authorization alternative");
		}
		probe->streams_opened.fetch_add(1, std::memory_order_relaxed);
		return std::unique_ptr<duckdb_api::BatchStream>(new PackageRowStream(plan, marker, probe));
	}

private:
	const std::string marker;
	const std::shared_ptr<PackageQueryProbe> probe;
};

} // namespace

PackageQueryProbe::PackageQueryProbe()
    : load_stages(0), reload_stages(0), plans(0), streams_opened(0), streams_closed(0), rows(0),
      generation_owners_destroyed(0), publication_commits(0), publication_discards(0), closes(0),
      query_was_closing_at_close(false) {
}

void PackageQueryStagingService::Close() const noexcept {
	probe->closes.fetch_add(1, std::memory_order_relaxed);
	auto coordinator = close_observer.lock();
	probe->query_was_closing_at_close.store(coordinator && coordinator->IsClosing(), std::memory_order_release);
}

PackageQueryStagingService::PackageQueryStagingService(
    duckdb_api::CompiledQueryRegistrationView initial_registration_p, duckdb_api::CompiledConnector initial_connector_p,
    duckdb_api::CompiledQueryRegistrationView replacement_registration_p,
    duckdb_api::CompiledConnector replacement_connector_p, std::string accepted_root_p,
    std::shared_ptr<PackageQueryProbe> probe_p)
    : initial_registration(new duckdb_api::CompiledQueryRegistrationView(std::move(initial_registration_p))),
      initial_connector(new duckdb_api::CompiledConnector(std::move(initial_connector_p))),
      initial_selective_connector(),
      replacement_registration(new duckdb_api::CompiledQueryRegistrationView(std::move(replacement_registration_p))),
      replacement_connector(new duckdb_api::CompiledConnector(std::move(replacement_connector_p))),
      replacement_selective_connector(), accepted_root(std::move(accepted_root_p)), probe(std::move(probe_p)),
      reload_changed(false) {
	if (accepted_root.empty() || accepted_root[0] != '/' || !probe) {
		throw std::invalid_argument("Query package test staging requires an absolute root and probe");
	}
}

PackageQueryStagingService::PackageQueryStagingService(
    duckdb_api::CompiledQueryRegistrationView initial_registration_p, duckdb_api::CompiledConnector initial_connector_p,
    duckdb_api::CompiledConnector initial_selective_connector_p,
    duckdb_api::CompiledQueryRegistrationView replacement_registration_p,
    duckdb_api::CompiledConnector replacement_connector_p,
    duckdb_api::CompiledConnector replacement_selective_connector_p, std::string accepted_root_p,
    std::shared_ptr<PackageQueryProbe> probe_p)
    : initial_registration(new duckdb_api::CompiledQueryRegistrationView(std::move(initial_registration_p))),
      initial_connector(new duckdb_api::CompiledConnector(std::move(initial_connector_p))),
      initial_selective_connector(new duckdb_api::CompiledConnector(std::move(initial_selective_connector_p))),
      replacement_registration(new duckdb_api::CompiledQueryRegistrationView(std::move(replacement_registration_p))),
      replacement_connector(new duckdb_api::CompiledConnector(std::move(replacement_connector_p))),
      replacement_selective_connector(new duckdb_api::CompiledConnector(std::move(replacement_selective_connector_p))),
      accepted_root(std::move(accepted_root_p)), probe(std::move(probe_p)), reload_changed(false) {
	if (accepted_root.empty() || accepted_root[0] != '/' || !probe) {
		throw std::invalid_argument("Query package test staging requires an absolute root and probe");
	}
}

PackageQueryStagingService::PackageQueryStagingService(
    duckdb_api::CompiledQueryRegistrationView initial_registration_p,
    duckdb_api::CompiledQueryRegistrationView replacement_registration_p, std::string accepted_root_p,
    std::shared_ptr<PackageQueryProbe> probe_p)
    : initial_registration(new duckdb_api::CompiledQueryRegistrationView(std::move(initial_registration_p))),
      initial_connector(), initial_selective_connector(),
      replacement_registration(new duckdb_api::CompiledQueryRegistrationView(std::move(replacement_registration_p))),
      replacement_connector(), replacement_selective_connector(), accepted_root(std::move(accepted_root_p)),
      probe(std::move(probe_p)), reload_changed(false) {
	if (accepted_root.empty() || accepted_root[0] != '/' || !probe) {
		throw std::invalid_argument("Query package test staging requires an absolute root and probe");
	}
}

std::shared_ptr<const duckdb_api::QueryPublishedGeneration> PackageQueryStagingService::BuildPublished(
    const std::shared_ptr<const duckdb_api::CompiledQueryRegistrationView> &registration,
    const std::shared_ptr<const duckdb_api::CompiledConnector> &connector,
    const std::shared_ptr<const duckdb_api::CompiledConnector> &selective_connector, const std::string &marker) const {
	auto published =
	    std::shared_ptr<const duckdb_api::QueryPublishedGeneration>(new duckdb_api::QueryPublishedGeneration(
	        *registration,
	        std::shared_ptr<const duckdb_api::QueryScanPlanningService>(
	            new PackagePlanningService(registration->GenerationHandle(), connector, selective_connector, probe)),
	        std::shared_ptr<const duckdb_api::ScanExecutor>(new PackageExecutor(marker, probe)),
	        std::shared_ptr<const duckdb_api::QueryGenerationOwner>(new PackageGenerationOwner(registration, probe))));
	{
		std::lock_guard<std::mutex> guard(candidate_mutex);
		last_candidate = published;
	}
	return published;
}

duckdb_api::QueryStagedGeneration PackageQueryStagingService::StageLoad(const std::string &absolute_root,
                                                                        duckdb_api::ExecutionControl &control) const {
	probe->load_stages.fetch_add(1, std::memory_order_relaxed);
	if (control.IsCancellationRequested()) {
		throw duckdb_api::ExecutionCancelled();
	}
	if (absolute_root != accepted_root) {
		throw duckdb_api::QueryStagingError("package_root", "compile", "connector.yaml", 1, 1, "$.package_root",
		                                    "package root is not the controlled fixture");
	}
	return duckdb_api::QueryStagedGeneration(
	    BuildPublished(initial_registration, initial_connector, initial_selective_connector, "old"), true,
	    std::unique_ptr<duckdb_api::QueryPublicationLease>(new PackagePublicationLease(probe)));
}

duckdb_api::QueryStagedGeneration
PackageQueryStagingService::StageReload(const std::string &connector,
                                        const std::shared_ptr<const duckdb_api::QueryPublishedGeneration> &active,
                                        duckdb_api::ExecutionControl &control) const {
	probe->reload_stages.fetch_add(1, std::memory_order_relaxed);
	if (control.IsCancellationRequested()) {
		throw duckdb_api::ExecutionCancelled();
	}
	if (!active || connector != active->Registration().Identity().ConnectorId()) {
		throw std::logic_error("Query package test reload received the wrong active generation");
	}
	if (!reload_changed.load(std::memory_order_acquire)) {
		std::lock_guard<std::mutex> guard(candidate_mutex);
		last_candidate = active;
		return duckdb_api::QueryStagedGeneration(active, false);
	}
	return duckdb_api::QueryStagedGeneration(
	    BuildPublished(replacement_registration, replacement_connector, replacement_selective_connector, "new"), true,
	    std::unique_ptr<duckdb_api::QueryPublicationLease>(new PackagePublicationLease(probe)));
}

void PackageQueryStagingService::SetReloadChanged(bool changed) noexcept {
	reload_changed.store(changed, std::memory_order_release);
}

void PackageQueryStagingService::ObserveQueryClose(
    std::weak_ptr<duckdb::duckdb_api_query_internal::CatalogGenerationCoordinator> coordinator) {
	close_observer = std::move(coordinator);
}

std::weak_ptr<const duckdb_api::QueryPublishedGeneration> PackageQueryStagingService::LastCandidate() const {
	std::lock_guard<std::mutex> guard(candidate_mutex);
	return last_candidate;
}

std::shared_ptr<PackageQueryStagingService>
BuildCompatibilityPackageQueryStaging(const std::string &absolute_root,
                                      const std::shared_ptr<PackageQueryProbe> &probe) {
	auto initial = BuildPackageCompatibilityFixture(PackageCompatibilityFixture::BASELINE, "1.2.3", 'a');
	auto replacement = BuildPackageCompatibilityFixture(PackageCompatibilityFixture::APPEND_RELATION, "1.3.0", 'b');
	return std::shared_ptr<PackageQueryStagingService>(
	    new PackageQueryStagingService(initial.QueryRegistration(), initial.Connector(),
	                                   replacement.QueryRegistration(), replacement.Connector(), absolute_root, probe));
}

std::shared_ptr<PackageQueryStagingService>
BuildGithubPackageQueryStaging(const std::string &absolute_repository_root,
                               const std::shared_ptr<PackageQueryProbe> &probe) {
	auto initial = CompileRepositoryGithubRegistrationFixture(absolute_repository_root);
	auto replacement = CompileRepositoryGithubRegistrationFixture(absolute_repository_root);
	// This Query-owned catalog oracle intentionally has no Semantics provider.
	// The lead-owned whole-graph target supplies same-generation planning; this
	// fixture fails if catalog-only assertions cross that unprovisioned port.
	return std::shared_ptr<PackageQueryStagingService>(new PackageQueryStagingService(
	    std::move(initial), std::move(replacement), absolute_repository_root + "/connectors/github", probe));
}

std::shared_ptr<PackageQueryStagingService>
BuildRickAndMortyPackageQueryStaging(const std::string &absolute_repository_root,
                                     const std::shared_ptr<PackageQueryProbe> &probe) {
	auto initial = CompileRepositoryRickAndMortyLocalPackageFixture(absolute_repository_root);
	auto replacement = CompileRepositoryRickAndMortyLocalPackageFixture(absolute_repository_root);
	// ARRAY bind evidence needs the deterministic Semantics provider so DuckDB
	// can compare the published registration schema with its same-generation
	// plan. The DESCRIBE remains Runtime-free.
	return std::shared_ptr<PackageQueryStagingService>(new PackageQueryStagingService(
	    initial.Generation().QueryRegistration(), initial.Generation().Connector(),
	    replacement.Generation().QueryRegistration(), replacement.Generation().Connector(),
	    absolute_repository_root + "/connectors/rickandmorty", probe));
}

std::shared_ptr<duckdb::duckdb_api_query_internal::CatalogGenerationCoordinator>
RegisterPackageQuerySurface(duckdb::DuckDB &database,
                            const std::shared_ptr<const duckdb_api::QueryPackageStagingService> &staging) {
	duckdb::ExtensionLoader loader(*database.instance, "duckdb_api_package_query_tests");
	duckdb::RegisterDuckdbApiSecrets(loader);
	return duckdb::duckdb_api_query_internal::RegisterPackageSurfaceInternal(loader, staging);
}

std::string PackageQueryError(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	Require(result->HasError(), "package query unexpectedly succeeded: " + sql);
	return result->GetError();
}

void RequirePackageQuerySuccess(duckdb::Connection &connection, const std::string &sql) {
	auto result = connection.Query(sql);
	if (result->HasError()) {
		throw std::runtime_error("package query failed: " + sql + ": " + result->GetError());
	}
}

} // namespace duckdb_api_test
