#pragma once

namespace duckdb {
class Expression;
class LogicalGet;

namespace duckdb_api_query_internal {

// Recognizes only RFC 0008's structured DuckDB shape. The function is pure:
// it never formats or retains the expression and cannot mutate DuckDB's filter
// vector, bind data, or logical scan.
bool IsVisibilityEqualsPrivate(const LogicalGet &get, const Expression &expression);

} // namespace duckdb_api_query_internal
} // namespace duckdb
