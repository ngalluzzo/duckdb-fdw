#include "duckdb_api/relational_predicate.hpp"

#include <stdexcept>

namespace duckdb_api {

RequestedPredicate::RequestedPredicate() : kind(RequestedPredicateKind::UNRESTRICTED) {
}

RequestedPredicate::RequestedPredicate(RequestedPredicateKind kind_p) : kind(kind_p) {
}

RequestedPredicate RequestedPredicate::Unrestricted() {
	return RequestedPredicate(RequestedPredicateKind::UNRESTRICTED);
}

RequestedPredicate RequestedPredicate::VisibilityEqualsPrivate() {
	return RequestedPredicate(RequestedPredicateKind::VISIBILITY_EQUALS_PRIVATE);
}

RequestedPredicateKind RequestedPredicate::Kind() const noexcept {
	return kind;
}

bool RequestedPredicate::operator==(const RequestedPredicate &other) const noexcept {
	return kind == other.kind;
}

bool RequestedPredicate::operator!=(const RequestedPredicate &other) const noexcept {
	return !(*this == other);
}

std::string RequestedPredicate::Snapshot() const {
	switch (kind) {
	case RequestedPredicateKind::UNRESTRICTED:
		return "unrestricted";
	case RequestedPredicateKind::VISIBILITY_EQUALS_PRIVATE:
		return "visibility_equals_private";
	}
	throw std::logic_error("requested predicate contains an unknown closed state");
}

} // namespace duckdb_api
