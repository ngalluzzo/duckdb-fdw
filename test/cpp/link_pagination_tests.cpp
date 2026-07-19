#include "duckdb_api/internal/link_pagination.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using duckdb_api::internal::LinkPaginationError;
using duckdb_api::internal::LinkPaginationErrorKind;
using duckdb_api::internal::LinkPaginationState;
using duckdb_api_test::Require;

const std::string CANARY = "private-repository-canary";

void RequireRejected(const std::vector<std::string> &fields, LinkPaginationErrorKind expected_kind,
                     const std::string &label) {
	LinkPaginationState state;
	bool rejected = false;
	try {
		state.Advance(fields);
	} catch (const LinkPaginationError &error) {
		rejected = true;
		Require(error.Kind() == expected_kind, label + " used the wrong error kind");
		Require(error.Field() == "pagination.next", label + " used the wrong policy field");
		Require(!error.SafeMessage().empty() && error.SafeMessage().size() <= 128,
		        label + " produced an empty or unbounded diagnostic");
		Require(error.SafeMessage().find(CANARY) == std::string::npos, label + " exposed received Link data");
		Require(error.SafeMessage().find("api.github.com") == std::string::npos,
		        label + " exposed a received destination");
	}
	Require(rejected, label + " was accepted");
	Require(state.Failed(), label + " did not make pagination terminal");
	Require(state.CurrentPage() == 1 && state.SeenPageCount() == 1,
	        label + " changed typed state before validation completed");

	bool terminal = false;
	try {
		state.Advance({});
	} catch (const LinkPaginationError &error) {
		terminal = error.Kind() == LinkPaginationErrorKind::STATE;
	}
	Require(terminal, label + " permitted continuation after rejection");
}

void TestExhaustionWithoutNext() {
	LinkPaginationState absent;
	const auto absent_transition = absent.Advance({});
	Require(!absent_transition.has_next && absent_transition.next_page == 0,
	        "an absent Link field did not exhaust the source");
	Require(absent.Exhausted() && !absent.Failed(), "clean exhaustion used a failure state");

	LinkPaginationState non_next;
	const auto non_next_transition = non_next.Advance({"<https://example.test/page/1>; rel=last"});
	Require(!non_next_transition.has_next && non_next.Exhausted(),
	        "Link metadata without rel=next did not exhaust the source");

	LinkPaginationState empty_list;
	const auto empty_list_transition = empty_list.Advance({" , , \t", ""});
	Require(!empty_list_transition.has_next && empty_list.Exhausted(),
	        "RFC list empty elements did not produce clean exhaustion");

	LinkPaginationState empty_target;
	const auto empty_target_transition = empty_target.Advance({"<>; rel=last"});
	Require(!empty_target_transition.has_next && empty_target.Exhausted(),
	        "an empty relative URI-reference was rejected");

	LinkPaginationState ip_literal;
	const auto ip_literal_transition = ip_literal.Advance({"<https://[2001:db8::1]/page>; rel=last"});
	Require(!ip_literal_transition.has_next && ip_literal.Exhausted(), "a valid IPv6 URI-reference was rejected");

	bool terminal = false;
	try {
		absent.Advance({});
	} catch (const LinkPaginationError &error) {
		terminal = error.Kind() == LinkPaginationErrorKind::STATE;
	}
	Require(terminal, "exhausted pagination advanced again");
}

void TestAcceptedTransitions() {
	LinkPaginationState case_insensitive;
	const auto uppercase =
	    case_insensitive.Advance({"<https://api.github.com/user/repos?per_page=100&page=2>; rel=NEXT"});
	Require(uppercase.has_next && uppercase.next_page == 2,
	        "case-insensitive registered next relation silently truncated pagination");

	LinkPaginationState extensions;
	const auto extension_transition =
	    extensions.Advance({"<https://api.github.com/user/repos?per_page=100&page=2>; foo; "
	                        "rel=\"https://example.test/relations/archive next\""});
	Require(extension_transition.has_next && extension_transition.next_page == 2,
	        "valid valueless or URI relation extensions suppressed registered next");

	LinkPaginationState anchored;
	const auto anchored_transition =
	    anchored.Advance({"<https://api.github.com/user/repos?per_page=100&page=2>; rel=next; "
	                      "anchor=\"https://credential-canary.invalid/context\""});
	Require(!anchored_transition.has_next && anchored.Exhausted(),
	        "an alternate anchor granted response-relative continuation authority");

	LinkPaginationState duplicate_rel_exhaustion;
	const auto duplicate_rel_exhaustion_transition = duplicate_rel_exhaustion.Advance(
	    {"<https://api.github.com/user/repos?per_page=100&page=2>; rel=prev; REL=next"});
	Require(!duplicate_rel_exhaustion_transition.has_next && duplicate_rel_exhaustion.Exhausted(),
	        "a later duplicate rel parameter overrode the first non-next relation");

	LinkPaginationState duplicate_rel_next;
	const auto duplicate_rel_next_transition =
	    duplicate_rel_next.Advance({"<https://api.github.com/user/repos?per_page=100&page=2>; rel=next; REL=prev"});
	Require(duplicate_rel_next_transition.has_next && duplicate_rel_next_transition.next_page == 2,
	        "a later duplicate rel parameter overrode the first next relation");

	LinkPaginationState bare_duplicate_rel_exhaustion;
	const auto bare_duplicate_rel_exhaustion_transition = bare_duplicate_rel_exhaustion.Advance(
	    {"<https://api.github.com/user/repos?per_page=100&page=2>; rel=prev; REL"});
	Require(!bare_duplicate_rel_exhaustion_transition.has_next && bare_duplicate_rel_exhaustion.Exhausted(),
	        "a valueless later rel changed first-rel exhaustion");

	LinkPaginationState bare_duplicate_rel_next;
	const auto bare_duplicate_rel_next_transition =
	    bare_duplicate_rel_next.Advance({"<https://api.github.com/user/repos?per_page=100&page=2>; rel=next; REL"});
	Require(bare_duplicate_rel_next_transition.has_next && bare_duplicate_rel_next_transition.next_page == 2,
	        "a valueless later rel suppressed the first next relation");

	LinkPaginationState leading_empty_elements;
	const auto leading_empty_transition =
	    leading_empty_elements.Advance({", , <https://api.github.com/user/repos?per_page=100&page=2>; rel=next"});
	Require(leading_empty_transition.has_next && leading_empty_transition.next_page == 2,
	        "leading empty list elements suppressed a valid next relation");

	LinkPaginationState trailing_empty_elements;
	const auto trailing_empty_transition =
	    trailing_empty_elements.Advance({"<https://api.github.com/user/repos?per_page=100&page=2>; rel=next, ,"});
	Require(trailing_empty_transition.has_next && trailing_empty_transition.next_page == 2,
	        "trailing empty list elements rejected a valid next relation");

	LinkPaginationState middle_empty_elements;
	const auto middle_empty_transition =
	    middle_empty_elements.Advance({"<https://example.test/previous>; rel=prev,, ,"
	                                   "<https://api.github.com/user/repos?per_page=100&page=2>; rel=next"});
	Require(middle_empty_transition.has_next && middle_empty_transition.next_page == 2,
	        "middle empty list elements suppressed a valid next relation");

	LinkPaginationState state;
	const auto second = state.Advance({"<https://api.github.com/user/repos?per_page=100&page=2>; rel=next"});
	Require(second.has_next && second.next_page == 2, "the fixed page-two target was rejected");
	Require(state.CurrentPage() == 2 && state.SeenPageCount() == 2,
	        "accepted page two was not recorded as typed state");

	const auto third = state.Advance(
	    {"<https://example.test/previous>; rel=prev, "
	     "<https://api.github.com:443/user/repos?page=3&per_page=100>; title=\"a,b;c\"; REL=\"prev next\""});
	Require(third.has_next && third.next_page == 3, "combined Link grammar did not produce page three");
	Require(state.CurrentPage() == 3 && state.SeenPageCount() == 3,
	        "accepted page three was not recorded exactly once");

	const auto end = state.Advance({"<https://api.github.com/user/repos?page=3&per_page=100>; rel=last"});
	Require(!end.has_next && state.Exhausted(), "final non-next Link metadata did not end pagination");
}

void TestMalformedLinkGrammar() {
	const std::vector<std::pair<std::string, std::string>> cases = {
	    {"https://api.github.com/user/repos?per_page=100&page=2; rel=next", "missing target brackets"},
	    {"<https://api.github.com/user/repos?per_page=100&page=2; rel=next", "unterminated target"},
	    {"<https://api.github.com/user/repos?per_page=100&page=2> rel=next", "missing parameter separator"},
	    {"<https://api.github.com/user/repos?per_page=100&page=2>; rel", "missing parameter value"},
	    {"<https://api.github.com/user/repos?per_page=100&page=2>", "missing required relation parameter"},
	    {"<https://api.github.com/user/repos?per_page=100&page=2>; rel=\"next", "unterminated quote"},
	    {"<https://api.github.com/user/repos?per_page=100&page=2>; rel=\" next\"", "leading relation space"},
	    {"<https://api.github.com/user/repos?per_page=100&page=2>; rel=\"next \"", "trailing relation space"},
	    {"<https://api.github.com/user/repos?per_page=100&page=2>; rel=\"next\tprev\"", "relation tab"},
	    {"<https://api.github.com/user/repos?per_page=100&page=2>; rel=\"/not-a-relation\"",
	     "relative URI relation type"},
	    {"<https://api.github.com/user/repos?per_page=100&page=2>; rel=\"https://[::1\"",
	     "malformed extension relation URI"},
	    {"<https://api.github.com/user/repos?per_page=100&page=2>; rel=\"next\\", "unterminated escape"},
	    {"<https://api.github.com/user/repos?per_page=100&page=2 bad>; rel=next", "invalid URI character"},
	    {"<https://api.github.com/user/repos?per_page=100&page=%ZZ>; rel=next", "malformed percent escape"},
	    {"<https://[2001:db8::1/page>; rel=last", "unmatched IP-literal bracket"},
	    {"<https://[not-ip]/page>; rel=last", "malformed IP literal"},
	    {"<https://example.test:not-a-port/page>; rel=last", "malformed authority port"},
	    {"<https://first@second@example.test/page>; rel=last", "multiple user-info delimiters"},
	    {"<https://example.test/page>; rel=last; anchor=\"https://[::1\"", "malformed anchor URI"}};
	for (const auto &test_case : cases) {
		RequireRejected({test_case.first}, LinkPaginationErrorKind::MALFORMED, test_case.second);
	}
	RequireRejected({std::string(129, ',')}, LinkPaginationErrorKind::MALFORMED,
	                "unreasonable empty list element count");
}

void TestMultipleNextTargets() {
	RequireRejected({"<https://api.github.com/user/repos?per_page=100&page=2>; rel=next, "
	                 "<https://api.github.com:443/user/repos?page=2&per_page=100>; rel=next"},
	                LinkPaginationErrorKind::POLICY, "two next targets in one field");
	RequireRejected({"<https://api.github.com/user/repos?per_page=100&page=2>; rel=next",
	                 "<https://api.github.com/user/repos?page=2&per_page=100>; rel=next"},
	                LinkPaginationErrorKind::POLICY, "two next targets in physical fields");
}

void TestDeniedNextTargets() {
	const std::vector<std::pair<std::string, std::string>> cases = {
	    {"http://api.github.com/user/repos?per_page=100&page=2", "non-HTTPS scheme"},
	    {"https://API.github.com/user/repos?per_page=100&page=2", "alternate authority spelling"},
	    {"https://api.github.com:444/user/repos?per_page=100&page=2", "wrong explicit port"},
	    {"https://user@api.github.com/user/repos?per_page=100&page=2", "user information"},
	    {"https://api.github.com.evil.test/user/repos?per_page=100&page=2", "authority suffix"},
	    {"https://api.github.com/users/repos?per_page=100&page=2", "wrong path"},
	    {"https://api.github.com/user/repos/?per_page=100&page=2", "trailing path slash"},
	    {"https://api.github.com/user/repos?per_page=100&page=2#fragment", "fragment"},
	    {"https://api.github.com/user/%72epos?per_page=100&page=2", "encoded path"},
	    {"https://api.github.com/user/repos?per%5Fpage=100&page=2", "encoded field name"},
	    {"https://api.github.com/user/repos?per_page=%31%30%30&page=2", "encoded page size"},
	    {"https://api.github.com/user/repos?per_page=100&page=%32", "encoded page number"},
	    {"https://api.github.com/user/repos?per_page=99&page=2", "wrong page size"},
	    {"https://api.github.com/user/repos?per_page=100", "missing page field"},
	    {"https://api.github.com/user/repos?page=2", "missing per-page field"},
	    {"https://api.github.com/user/repos?per_page=100&page=2&sort=id", "unknown field"},
	    {"https://api.github.com/user/repos?per_page=100&per_page=100&page=2", "duplicate per-page field"},
	    {"https://api.github.com/user/repos?per_page=100&page=2&page=2", "duplicate page field"},
	    {"https://api.github.com/user/repos?per_page=100&page=2&", "trailing empty field"},
	    {"https://api.github.com/user/repos?&per_page=100&page=2", "leading empty field"},
	    {"https://api.github.com/user/repos?per_page=100&&page=2", "middle empty field"},
	    {"https://api.github.com/user/repos?per_page=100&page=", "empty page number"},
	    {"https://api.github.com/user/repos?per_page=100&page=0", "zero page number"},
	    {"https://api.github.com/user/repos?per_page=100&page=02", "leading-zero page number"},
	    {"https://api.github.com/user/repos?per_page=100&page=+2", "signed page number"},
	    {"https://api.github.com/user/repos?per_page=100&page=3", "nonincrementing page number"},
	    {"https://api.github.com/user/repos?per_page=100&page=18446744073709551616", "overflowing page number"},
	    {"https://api.github.com/user/repos?per_page=100=100&page=2", "extra equals sign"}};
	for (const auto &test_case : cases) {
		RequireRejected({"<" + test_case.first + ">; rel=next; title=\"" + CANARY + "\""},
		                LinkPaginationErrorKind::POLICY, test_case.second);
	}
}

void TestRepeatedTypedPageIsRejected() {
	LinkPaginationState state;
	state.Advance({"<https://api.github.com/user/repos?per_page=100&page=2>; rel=next"});
	bool rejected = false;
	try {
		state.Advance({"<https://api.github.com/user/repos?per_page=100&page=2>; rel=next"});
	} catch (const LinkPaginationError &error) {
		rejected = error.Kind() == LinkPaginationErrorKind::POLICY && error.Field() == "pagination.next";
	}
	Require(rejected && state.Failed(), "a repeated typed page was not a terminal policy error");
	Require(state.CurrentPage() == 2 && state.SeenPageCount() == 2,
	        "repeated pagination metadata mutated accepted state");
}

} // namespace

int main() {
	try {
		TestExhaustionWithoutNext();
		TestAcceptedTransitions();
		TestMalformedLinkGrammar();
		TestMultipleNextTargets();
		TestDeniedNextTargets();
		TestRepeatedTypedPageIsRejected();
		std::cout << "Link pagination tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "Link pagination tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
