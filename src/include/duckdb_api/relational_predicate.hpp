#pragma once

#include <string>

namespace duckdb_api {

// Protocol-neutral relational intent shared by Query and Semantics. This
// private pre-1.0 team API deliberately represents only the complete base
// domain and RFC 0008's one admitted equality. It contains no DuckDB object,
// SQL text, Connector mapping, request field, credential, or execution state.
enum class RequestedPredicateKind { UNRESTRICTED, VISIBILITY_EQUALS_PRIVATE };

// Query reports the complete predicate that remains owned by DuckDB separately
// from the closed predicate offered for remote selection. This prevents a
// selective conjunct from being mistaken for the whole residual expression.
// COMPLETE_DUCKDB_FILTER is deliberately opaque: it conveys ownership and
// scope, never SQL text or authority to interpret the expression.
enum class RetainedPredicateScope { UNRESTRICTED, REQUESTED_PREDICATE, COMPLETE_DUCKDB_FILTER };

class RequestedPredicate {
public:
	// The default is the complete base domain so ordinary request construction
	// remains conservative when no structured predicate is available.
	RequestedPredicate();

	static RequestedPredicate Unrestricted();
	static RequestedPredicate VisibilityEqualsPrivate();

	RequestedPredicateKind Kind() const noexcept;
	bool operator==(const RequestedPredicate &other) const noexcept;
	bool operator!=(const RequestedPredicate &other) const noexcept;

	// Stable structural explanation. The output is not SQL, serialization, or
	// authority for Connector or Runtime behavior.
	std::string Snapshot() const;

private:
	explicit RequestedPredicate(RequestedPredicateKind kind);

	RequestedPredicateKind kind;
};

} // namespace duckdb_api
