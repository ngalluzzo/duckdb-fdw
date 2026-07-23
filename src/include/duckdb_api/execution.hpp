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

// RFC 0021: the primary failure taxonomy. A stable, finer classification layered
// additively on ErrorStage — every terminal remote-scan failure maps to exactly
// one FailureClass. Existing rendered diagnostic strings are preserved verbatim;
// this classification is carried as an additional structured field, not a
// replacement of ErrorStage. No retry, rate-limit-waiting, caching, or
// circuit-breaking mechanism is enabled. The closed set is bound to
// release/1.0.0/freeze.json's failure_taxonomy section and enforced by
// scripts/contract_freeze.py.
enum class FailureClass : uint8_t {
	CONFIGURATION,
	AUTHORIZATION,
	CREDENTIAL_PROVIDER,
	DESTINATION_POLICY,
	TRANSPORT,
	// Reserved: no distinct transport timeout is emitted in v1 (libcurl's timeout
	// is the scan deadline). Produced only by a future distinct idle/connect
	// timeout, which requires its own RFC.
	TIMEOUT,
	REMOTE_STATUS,
	RATE_LIMIT,
	PROTOCOL,
	DECODE,
	SCHEMA,
	RESOURCE_BUDGET,
	CANCELLATION,
	INTERNAL
};

// RFC 0021: replay classification for a traversal step or terminal failure. It
// combines a Semantics-owned plan replay obligation with a Runtime-owned
// exposure state (rows emitted for the step). Indeterminate safety is treated
// as NEVER_REPLAYABLE. A future retry mechanism consumes this fact together with
// an uncommitted replay unit; v1 records it without retrying.
enum class ReplayClassification : uint8_t {
	NEVER_REPLAYABLE,
	REPLAYABLE_BEFORE_EXPOSURE,
	ATOMIC_TRAVERSAL_STEP,
	SERVER_DIRECTED_DELAY,
	INDETERMINATE
};

// Closed-set name functions. They throw std::logic_error on an unknown value so
// the vocabulary cannot drift silently; the names match the freeze's
// failure_taxonomy strings verbatim.
const char *FailureClassName(FailureClass failure_class);
const char *ReplayClassificationName(ReplayClassification classification);

// RFC 0021: the execution phase at which a failure was classified. Finer than
// ErrorStage: it distinguishes pre-execution bind/plan/admission from request,
// transport, decode, pagination, emission, and close. Closed-set; the name
// function throws on an unknown value.
enum class FailurePhase : uint8_t { BIND, PLAN, ADMIT, REQUEST, TRANSPORT, DECODE, PAGINATE, EMIT, CLOSE };

// RFC 0021: which aggregate scan budget counter terminated execution, for
// resource_budget/timeout failures. Matches the aggregate scan ledger's counter
// dimensions. NONE marks failures not terminated by a budget. Closed-set.
enum class BudgetDimension : uint8_t { NONE, TIME, ATTEMPTS, WAITING, PAGES, RESPONSE_BYTES, RECORDS, MEMORY };

// RFC 0021: the class of an observed remote response status. Structural only —
// never a status message, body, or remote string. NONE marks failures that did
// not observe a remote response. Closed-set.
enum class RemoteStatusClass : uint8_t { NONE, SUCCESS, CLIENT_ERROR, RATE_LIMITED, SERVER_ERROR, GRAPHQL_ERRORS };

const char *FailurePhaseName(FailurePhase phase);
const char *BudgetDimensionName(BudgetDimension dimension);
const char *RemoteStatusClassName(RemoteStatusClass status_class);

// RFC 0021: the additive structured failure-properties field. Every member is a
// closed code, ordinal, or checked count — never content (no body, document,
// cursor, row, credential, or remote message). Remote Runtime populates it at
// terminal-failure emission; Query renders it once at the DuckDB boundary
// without altering existing rendered strings. `step` and `attempt` are the
// stable scan-local identity ordinals (a scan is one BatchStream lifecycle; an
// operation is plan-derived). In v1 attempt is always 1 and no retry/waiting
// mechanism consumes replay_classification or the waiting budget.
struct FailureProperties {
	FailureClass failure_class;
	FailurePhase phase;
	ReplayClassification replay_classification;
	// Traversal-step ordinal, 1-based; 0 before the first step begins.
	std::uint64_t step;
	// Transport-attempt ordinal within the step; 1 in v1.
	std::uint64_t attempt;
	// Rows emitted to DuckDB before this failure (a count, never row content).
	std::uint64_t rows_exposed;
	RemoteStatusClass remote_status_class;
	BudgetDimension terminating_budget;
};

class ExecutionError : public std::exception {
public:
	// Unclassified: preserves the existing rendered string; Classified()==false
	// and Properties() is an unspecified default never read while unclassified.
	ExecutionError(ErrorStage stage, std::string field, std::string safe_message);
	// RFC 0021 classified: carries structured failure properties alongside the
	// stage. The existing stage/field/safe_message render unchanged; properties
	// are an additive field rendered separately by the adapter boundary.
	ExecutionError(ErrorStage stage, std::string field, std::string safe_message, FailureProperties properties);

	const char *what() const noexcept override;
	ErrorStage Stage() const noexcept;
	const std::string &Field() const;
	const std::string &SafeMessage() const;
	bool Classified() const noexcept;
	const FailureProperties &Properties() const noexcept;

private:
	ErrorStage stage;
	std::string field;
	std::string safe_message;
	FailureProperties properties;
	bool classified;
};

// Coarse fallback: derive a primary failure class from an ErrorStage when no
// explicit FailureProperties were attached (e.g. at the adapter translation
// boundary). Emitters override with finer classification by attaching
// FailureProperties at the throw site — e.g. HTTP 429 -> RATE_LIMIT, a GraphQL
// page-budget exhaustion -> RESOURCE_BUDGET, a malformed continuation -> PROTOCOL.
FailureClass ClassifyFailureClass(ErrorStage stage);

// RFC 0021: classify an HTTP response status into failure properties. auth_rejected
// is true when authentication was attempted and the endpoint rejected it (401/403
// -> AUTHORIZATION); false treats a 401/403 as an ordinary remote-status rejection
// (e.g. an anonymous request the endpoint refused). 429/503 -> RATE_LIMIT with a
// server-directed delay. step and rows_exposed are zero (a status is observed
// before decode, so no rows were exposed); the scan catch boundary enriches them.
FailureProperties HttpStatusFailureProperties(uint32_t status, bool auth_rejected);

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

// DuckDB-free scalar kinds supported by the native preview runtime. DOUBLE
// (RFC 0020) is IEEE-754 double precision; -0.0 is normalized to 0.0 at
// construction.
enum class ValueKind : uint8_t { BIGINT, VARCHAR, BOOLEAN, DOUBLE };

enum class ValueShape : uint8_t { SCALAR, ARRAY };

// Immutable structural Runtime/Query schema for one output column. The
// element kind remains scalar even for ARRAY, making nested collections
// unrepresentable. element_nullable is false for every scalar column.
struct OutputValueType {
	OutputValueType();
	OutputValueType(ValueKind kind);
	OutputValueType(ValueShape shape, ValueKind element_kind, bool element_nullable);
	static OutputValueType Scalar(ValueKind kind);
	static OutputValueType Array(ValueKind element_kind, bool element_nullable);

	bool operator==(const OutputValueType &other) const noexcept;
	bool operator!=(const OutputValueType &other) const noexcept;

	ValueShape shape;
	ValueKind element_kind;
	bool element_nullable;
};

// One owned scalar child inside an ARRAY value. This separate non-recursive
// type prevents a Runtime producer from constructing nested lists while
// retaining strict scalar conversion and independent child validity.
struct TypedScalarValue {
	static TypedScalarValue BigInt(int64_t value);
	static TypedScalarValue Varchar(std::string value);
	static TypedScalarValue Boolean(bool value);
	static TypedScalarValue Double(double value);
	static TypedScalarValue Null(ValueKind kind);

	ValueKind kind = ValueKind::VARCHAR;
	bool valid = false;
	int64_t bigint_value = 0;
	std::string varchar_value;
	bool boolean_value = false;
	double double_value = 0.0;
};

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
	static TypedValue Double(double value);
	static TypedValue Null(ValueKind kind);
	static TypedValue Null(OutputValueType type);
	static TypedValue Array(ValueKind element_kind, bool element_nullable, std::vector<TypedScalarValue> elements);

	OutputValueType Type() const noexcept;

	ValueKind kind;
	ValueShape shape;
	bool element_nullable;
	bool valid;
	int64_t bigint_value;
	std::string varchar_value;
	bool boolean_value;
	double double_value;
	std::vector<TypedScalarValue> elements;
};

struct TypedRow {
	std::vector<TypedValue> values;
};

// A batch declares the ordered structural types matching
// ScanPlan::output_columns. Every row has exactly that arity and type sequence.
// The adapter may translate these values into DuckDB vectors but runtime code
// owns strict conversion.
struct TypedBatch {
	std::vector<OutputValueType> column_types;
	std::vector<TypedRow> rows;

	void Clear();
	bool IsSchemaAligned() const noexcept;
	// Runtime handoff paths use the control-aware form so validation of a large
	// ARRAY cannot become an uninterruptible publication window.
	bool IsSchemaAligned(ExecutionControl &control) const;
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
	// Concrete Runtime executors may distinguish only these closed
	// alternatives. This inspection exposes no credential bytes and grants no
	// host, placement, or operation authority.
	// GITHUB_USER_BEARER is the bounded 0.7 source-compatibility spelling for
	// existing Query consumers. It aliases BEARER and cannot select a distinct
	// Runtime state or execution path.
	// CREDENTIAL is the kind-neutral static-credential alternative Query
	// supplies for every authenticated v1 package relation (ScanAuthorization::
	// Credential()), regardless of whether the target relation's compiled
	// credential is bearer or api_key: Query cannot know that at resolution
	// time. It satisfies either a bearer-requiring or an api_key-requiring
	// admitted profile; which authenticator actually decorates the request is
	// decided entirely by the profile's own RequiresBearer()/RequiresApiKey()
	// facts, never by this alternative.
	enum class AuthorizationAlternative : uint8_t { ANONYMOUS, BEARER, CREDENTIAL, GITHUB_USER_BEARER = BEARER };
	static AuthorizationAlternative AlternativeOf(const ScanAuthorization &authorization) noexcept {
		switch (authorization.kind) {
		case ScanAuthorization::Kind::ANONYMOUS:
			return AuthorizationAlternative::ANONYMOUS;
		case ScanAuthorization::Kind::BEARER:
			return AuthorizationAlternative::BEARER;
		case ScanAuthorization::Kind::CREDENTIAL:
			return AuthorizationAlternative::CREDENTIAL;
		}
		return AuthorizationAlternative::ANONYMOUS;
	}

	// True when the supplied alternative is an acceptable authorization for a
	// profile whose admission facts are requires_bearer/requires_api_key.
	// BEARER (direct legacy construction) and CREDENTIAL (the kind-neutral
	// Query path) both satisfy a bearer-requiring profile; only CREDENTIAL
	// satisfies an api_key-requiring profile, since no legitimate caller
	// constructs a BEARER-tagged capability intending api_key placement.
	static bool MatchesRequiredCredential(AuthorizationAlternative alternative, bool requires_bearer,
	                                      bool requires_api_key) noexcept {
		if (requires_api_key) {
			return alternative == AuthorizationAlternative::CREDENTIAL;
		}
		if (requires_bearer) {
			return alternative == AuthorizationAlternative::BEARER ||
			       alternative == AuthorizationAlternative::CREDENTIAL;
		}
		return alternative == AuthorizationAlternative::ANONYMOUS;
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
