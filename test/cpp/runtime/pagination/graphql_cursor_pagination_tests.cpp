#include "duckdb_api/internal/runtime/pagination/graphql_cursor_pagination.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Require(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void TestNullFirstCursorAndTerminalExhaustion() {
	duckdb_api::internal::GraphqlCursorState state(32, 512);
	Require(state.CurrentCursor() == nullptr && state.RequestedPages() == 0, "first GraphQL cursor must be null");
	state.MarkRequestStarted();
	state.Advance(true, "cursor-1");
	Require(state.CurrentCursor() && *state.CurrentCursor() == "cursor-1" && state.RequestedPages() == 1,
	        "accepted continuation must become the next request cursor");
	state.MarkRequestStarted();
	state.Advance(false, "ignored-terminal-cursor");
	Require(state.IsExhausted() && state.CurrentCursor() == nullptr,
	        "hasNextPage false must exhaust without retaining a cursor");
}

void TestRepeatedCursorFailsTerminally() {
	duckdb_api::internal::GraphqlCursorState state(32, 512);
	state.MarkRequestStarted();
	state.Advance(true, "same");
	state.MarkRequestStarted();
	try {
		state.Advance(true, "same");
		throw std::runtime_error("repeated cursor must fail");
	} catch (const duckdb_api::internal::GraphqlCursorError &error) {
		Require(error.Field() == "pagination.cursor" && state.IsFailed() && state.CurrentCursor() == nullptr,
		        "repeated cursor failure must be redacted and terminal");
	}
}

void TestAdvertisedThirtyThirdPageFails() {
	duckdb_api::internal::GraphqlCursorState state(32, 512);
	for (uint64_t page = 1; page <= 31; page++) {
		state.MarkRequestStarted();
		state.Advance(true, "cursor-" + std::to_string(page));
	}
	state.MarkRequestStarted();
	try {
		state.Advance(true, "cursor-32");
		throw std::runtime_error("advertised page 33 must fail");
	} catch (const duckdb_api::internal::GraphqlCursorError &) {
		Require(state.IsFailed() && state.RequestedPages() == 32,
		        "cursor state must reject a continuation after the 32nd requested page");
	}
}

void TestPersistentCursorMemoryAndByteBoundary() {
	duckdb_api::internal::GraphqlCursorState state(32, 512);
	const auto empty_state_bytes = state.RetainedMemoryBytes();
	state.MarkRequestStarted();
	state.Advance(true, std::string(512, 'x'));
	Require(state.CurrentCursor() && state.CurrentCursor()->size() == 512 &&
	            state.RetainedMemoryBytes() >= empty_state_bytes + 512,
	        "exact cursor boundary and persistent storage must be measurable");
	state.MarkRequestStarted();
	try {
		state.Advance(true, std::string(513, 'y'));
		throw std::runtime_error("oversized cursor must fail");
	} catch (const duckdb_api::internal::GraphqlCursorError &error) {
		Require(error.Field() == "pagination.cursor" && state.IsFailed() && state.RetainedMemoryBytes() == 0,
		        "oversized cursor must release persistent state terminally");
	}
}

} // namespace

int main() {
	try {
		TestNullFirstCursorAndTerminalExhaustion();
		TestRepeatedCursorFailsTerminally();
		TestAdvertisedThirtyThirdPageFails();
		TestPersistentCursorMemoryAndByteBoundary();
		std::cout << "GraphQL cursor pagination tests passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
