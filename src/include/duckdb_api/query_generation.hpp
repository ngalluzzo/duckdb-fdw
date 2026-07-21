#pragma once

#include "duckdb_api/compiled_package_generation.hpp"
#include "duckdb_api/execution.hpp"
#include "duckdb_api/scan_request.hpp"

#include <exception>
#include <memory>
#include <string>

namespace duckdb_api {

// Redacted structured failure produced by lead composition while staging a
// package. Source is package-relative and detail is bounded safe prose; Query
// maps this once to DuckDB without seeing an absolute root, source scalar,
// request data, or credential material.
class QueryStagingError : public std::exception {
public:
	QueryStagingError(std::string code, std::string phase, std::string source, std::string field,
	                  std::string safe_detail);

	const char *what() const noexcept override;
	const std::string &Code() const noexcept;
	const std::string &Phase() const noexcept;
	const std::string &Source() const noexcept;
	const std::string &Field() const noexcept;
	const std::string &SafeDetail() const noexcept;

private:
	std::string code;
	std::string phase;
	std::string source;
	std::string field;
	std::string safe_detail;
	std::string message;
};

// Relational Semantics' bounded service as consumed by generated Query
// functions. The opaque generation handle is lifetime/provenance only; the
// provider remains solely responsible for finding and interpreting semantic
// compiled facts. Implementations must be deterministic and perform no I/O.
class QueryScanPlanningService {
public:
	virtual ~QueryScanPlanningService() noexcept;
	virtual ScanPlan BuildPlan(const CompiledGenerationHandle &generation, const ScanRequest &request) const = 0;
};

// Opaque ownership retained by DuckDB catalog, bind, prepared-plan, and scan
// state. Remote Runtime supplies the concrete owner. Query may copy and retain
// it, but has no inspection, mutation, commit, or lookup API.
class QueryGenerationOwner {
public:
	virtual ~QueryGenerationOwner() noexcept;
};

// Opaque publication capability supplied by lead composition for a changed
// Runtime candidate. Query retains it across DuckDB catalog mutation and calls
// exactly one terminal method from the corresponding transaction callback.
// Commit must be an infallible pointer-swap style publication; Discard is
// idempotent. Concrete destruction must contain an uncommitted candidate as a
// discard so exceptions before transaction ownership cannot leak staged state.
class QueryPublicationLease {
public:
	virtual ~QueryPublicationLease() noexcept;
	virtual void Commit() noexcept = 0;
	virtual void Discard() noexcept = 0;
};

// Complete immutable value needed to publish and execute one package
// generation. Lead-owned composition assembles the three provider services;
// Query validates and consumes their narrow interfaces without parsing source,
// choosing operations, or inspecting Runtime registry state.
class QueryPublishedGeneration {
public:
	QueryPublishedGeneration(CompiledQueryRegistrationView registration,
	                         std::shared_ptr<const QueryScanPlanningService> planning,
	                         std::shared_ptr<const ScanExecutor> executor,
	                         std::shared_ptr<const QueryGenerationOwner> owner);

	QueryPublishedGeneration(const QueryPublishedGeneration &) = default;
	QueryPublishedGeneration(QueryPublishedGeneration &&) = default;
	QueryPublishedGeneration &operator=(const QueryPublishedGeneration &) = delete;
	QueryPublishedGeneration &operator=(QueryPublishedGeneration &&) = delete;

	const CompiledQueryRegistrationView &Registration() const noexcept;
	const std::shared_ptr<const QueryScanPlanningService> &Planning() const noexcept;
	const std::shared_ptr<const ScanExecutor> &Executor() const noexcept;
	const std::shared_ptr<const QueryGenerationOwner> &Owner() const noexcept;

private:
	CompiledQueryRegistrationView registration;
	std::shared_ptr<const QueryScanPlanningService> planning;
	std::shared_ptr<const ScanExecutor> executor;
	std::shared_ptr<const QueryGenerationOwner> owner;
};

// Result of local compilation and Runtime staging. `changed` describes the
// candidate relative to the active generation supplied for reload. The staged
// value is not separately published: catalog entries retain it only if the
// surrounding DuckDB catalog transaction commits.
class QueryStagedGeneration {
public:
	QueryStagedGeneration(std::shared_ptr<const QueryPublishedGeneration> generation, bool changed,
	                      std::unique_ptr<QueryPublicationLease> publication_lease = nullptr);
	QueryStagedGeneration(QueryStagedGeneration &&) noexcept = default;
	QueryStagedGeneration &operator=(QueryStagedGeneration &&) noexcept = default;
	QueryStagedGeneration(const QueryStagedGeneration &) = delete;
	QueryStagedGeneration &operator=(const QueryStagedGeneration &) = delete;

	const std::shared_ptr<const QueryPublishedGeneration> &Generation() const noexcept;
	bool Changed() const noexcept;
	const QueryPublicationLease *PublicationLease() const noexcept;
	std::unique_ptr<QueryPublicationLease> TakePublicationLease() noexcept;

private:
	std::shared_ptr<const QueryPublishedGeneration> generation;
	bool changed;
	std::unique_ptr<QueryPublicationLease> publication_lease;
};

// Lead-composition port used only by management execution. Connector owns
// source custody/compilation and Runtime owns staging; the composed service
// performs those provider calls and returns one complete immutable candidate.
// Calls must honor the call-scoped cancellation view, perform no network or
// credential work, and leave no published state when they throw.
class QueryPackageStagingService {
public:
	virtual ~QueryPackageStagingService() noexcept;

	virtual QueryStagedGeneration StageLoad(const std::string &absolute_root, ExecutionControl &control) const = 0;
	virtual QueryStagedGeneration StageReload(const std::string &connector,
	                                          const std::shared_ptr<const QueryPublishedGeneration> &active,
	                                          ExecutionControl &control) const = 0;

	// DatabaseInstance teardown calls this only after Query has rejected new
	// catalog publication. Implementations close their provider-side admission
	// service without invalidating immutable generations, plans, executors, or
	// streams already retained by DuckDB state. Close is idempotent and must not
	// throw; dynamic extension unload remains unsupported.
	virtual void Close() const noexcept = 0;
};

} // namespace duckdb_api
