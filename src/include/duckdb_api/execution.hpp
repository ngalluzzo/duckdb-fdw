#pragma once

#include "duckdb_api/scan_plan.hpp"

#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace duckdb_api {

// Stable, redacted failure categories crossing from Remote Runtime into the
// DuckDB adapter. Raw response values, destinations, and dependency exceptions
// must never appear in field or safe_message.
enum class ErrorStage : uint8_t { TRANSPORT, HTTP_STATUS, DECODE, SCHEMA, POLICY, RESOURCE, INTERNAL };

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

// DuckDB-free scalar kinds supported by the native preview runtime. Values are
// never nullable in RFC 0005's required schema; adding nullability is a shared
// interface change rather than an adapter convention.
enum class ValueKind : uint8_t { BIGINT, VARCHAR, BOOLEAN };

struct TypedValue {
	static TypedValue BigInt(int64_t value);
	static TypedValue Varchar(std::string value);
	static TypedValue Boolean(bool value);

	ValueKind kind;
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
// batch size. Cancel and Close are idempotent, non-throwing lifecycle signals.
class BatchStream {
public:
	virtual ~BatchStream() noexcept;
	virtual bool Next(ExecutionControl &control, TypedBatch &batch) = 0;
	virtual void Cancel() noexcept = 0;
	virtual void Close() noexcept = 0;
};

// Immutable Remote Runtime service retained by the adapter. Open validates the
// executable capability envelope before acquiring a source and returns a new,
// isolated stream for each scan.
class ScanExecutor {
public:
	virtual ~ScanExecutor() noexcept;
	virtual std::unique_ptr<BatchStream> Open(const ScanPlan &plan, ExecutionControl &control) const = 0;
};

} // namespace duckdb_api
