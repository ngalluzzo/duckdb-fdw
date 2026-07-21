#pragma once

#include "duckdb_api/authorization.hpp"
#include "duckdb_api/scan_plan.hpp"

#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace duckdb_api {

// Stable, redacted failure categories crossing from Remote Runtime into the
// DuckDB adapter. Raw response values, destinations, and dependency exceptions
// must never appear in field or safe_message.
enum class ErrorStage : uint8_t {
	TRANSPORT,
	HTTP_STATUS,
	DECODE,
	SCHEMA,
	POLICY,
	RESOURCE,
	INTERNAL,
	// Invalid or missing Runtime authorization and remote HTTP 401.
	AUTHENTICATION,
	// Remote HTTP 403 after successful authentication.
	AUTHORIZATION,
	// A syntactically valid remote protocol envelope reported an application
	// failure. Field and message remain structural and redacted: remote values,
	// paths containing values, documents, variables, and credentials must not
	// cross this boundary.
	REMOTE_PROTOCOL
};

class ExecutionError : public std::exception {
public:
	ExecutionError(ErrorStage stage, std::string field, std::string safe_message);

	const char *what() const noexcept override;
	ErrorStage Stage() const noexcept;
	const std::string &Field() const;
	const std::string &SafeMessage() const;

private:
	ErrorStage stage;
	std::string field;
	std::string safe_message;
};

// Protocol-neutral cancellation marker. The adapter translates it exactly
// once into the host engine's interruption type.
class ExecutionCancelled : public std::exception {
public:
	const char *what() const noexcept override;
};

// Non-owning, call-scoped cancellation view supplied by the host adapter.
// Runtime implementations must not retain this object beyond Open or Next.
class ExecutionControl {
public:
	virtual ~ExecutionControl() noexcept;
	virtual bool IsCancellationRequested() const noexcept = 0;
};

// DuckDB-free scalar kinds supported by the native preview runtime.
enum class ValueKind : uint8_t { BIGINT, VARCHAR, BOOLEAN };

// Protocol-neutral scalar handoff owned by Remote Runtime and consumed by
// Query Experience. Invalid values represent SQL NULL while retaining the
// planned scalar kind; consumers must use validity rather than inspecting a
// payload sentinel. Runtime may produce an invalid value only for a nullable
// planned column. Values own their payload and remain valid for the lifetime of
// the containing batch; the type carries no cancellation or close state.
struct TypedValue {
	// The default is a safe invalid VARCHAR. The four-argument constructor
	// preserves the former C++11 aggregate-construction surface for existing
	// non-null Runtime and fixture consumers.
	TypedValue();
	TypedValue(ValueKind kind, int64_t bigint_value, std::string varchar_value, bool boolean_value);

	static TypedValue BigInt(int64_t value);
	static TypedValue Varchar(std::string value);
	static TypedValue Boolean(bool value);
	static TypedValue Null(ValueKind kind);

	ValueKind kind;
	bool valid;
	int64_t bigint_value;
	std::string varchar_value;
	bool boolean_value;
};

struct TypedRow {
	std::vector<TypedValue> values;
};

// A batch declares the ordered scalar kinds matching ScanPlan::output_columns.
// Every row has exactly that arity and kind sequence. The adapter may translate
// these values into DuckDB vectors but runtime code owns strict conversion.
struct TypedBatch {
	std::vector<ValueKind> column_kinds;
	std::vector<TypedRow> rows;

	void Clear();
	bool IsSchemaAligned() const noexcept;
};

// One independently owned scan. Next replaces rows with at most the planned
// batch size. A true result always carries a schema-aligned nonempty batch;
// false alone means clean source exhaustion. A terminal failure throws and can
// never be reported as exhaustion, including after earlier batches crossed the
// streaming boundary. Cancel and Close are idempotent, non-throwing lifecycle
// signals.
class BatchStream {
public:
	virtual ~BatchStream() noexcept;
	virtual bool Next(ExecutionControl &control, TypedBatch &batch) = 0;
	virtual void Cancel() noexcept = 0;
	virtual void Close() noexcept = 0;
};

// Immutable Remote Runtime service retained by the adapter. Open preserves the
// existing anonymous service. OpenWithAuthorization is the permanent explicit
// capability-envelope service for new consumers: it takes ownership by value,
// rejects moved-from capabilities as authentication failures, and returns a
// new isolated stream for each scan. The default implementation rejects every
// valid envelope as an unsupported executable policy. Existing anonymous
// executors remain source-compatible through Open, while no envelope can fall
// back to anonymous execution until the concrete executor validates and owns
// both alternatives.
//
// Both methods are call-scoped and must not retain ExecutionControl. Open and
// OpenWithAuthorization perform no network I/O; concrete authorized executors
// validate the complete plan/capability intersection before stream creation
// and keep the moved capability isolated to that stream. Authorization release
// on rejection, cancellation, Close, and destruction is owned by Runtime.
class ScanExecutor {
public:
	virtual ~ScanExecutor() noexcept;
	virtual std::unique_ptr<BatchStream> Open(const ScanPlan &plan, ExecutionControl &control) const = 0;

	std::unique_ptr<BatchStream> OpenWithAuthorization(const ScanPlan &plan, ScanAuthorization authorization,
	                                                   ExecutionControl &control) const {
		if (control.IsCancellationRequested()) {
			throw ExecutionCancelled();
		}
		if (!authorization.valid) {
			throw ExecutionError(ErrorStage::AUTHENTICATION, "authorization", "authorization capability is invalid");
		}
		return OpenAuthorizationEnvelope(plan, std::move(authorization), control);
	}

protected:
	// Concrete Runtime executors may distinguish only the two closed
	// alternatives. This inspection exposes no credential bytes and grants no
	// host, placement, or operation authority.
	// GITHUB_USER_BEARER is the bounded 0.7 source-compatibility spelling for
	// existing Query consumers. It aliases BEARER and cannot select a distinct
	// Runtime state or execution path.
	enum class AuthorizationAlternative : uint8_t { ANONYMOUS, BEARER, GITHUB_USER_BEARER = BEARER };
	static AuthorizationAlternative AlternativeOf(const ScanAuthorization &authorization) noexcept {
		return authorization.kind == ScanAuthorization::Kind::ANONYMOUS ? AuthorizationAlternative::ANONYMOUS
		                                                                : AuthorizationAlternative::BEARER;
	}

	// The public entry validates cancellation and moved-from state before this
	// extension point. An executor overriding it must validate the complete
	// plan/alternative intersection before moving the envelope into a stream.
	// The compatibility default refuses all envelope work without invoking the
	// legacy anonymous Open or performing I/O.
	virtual std::unique_ptr<BatchStream>
	OpenAuthorizationEnvelope(const ScanPlan &plan, ScanAuthorization authorization, ExecutionControl &control) const {
		throw ExecutionError(ErrorStage::POLICY, "authorization",
		                     "authorization envelope is not supported by this executor");
	}
};

} // namespace duckdb_api
