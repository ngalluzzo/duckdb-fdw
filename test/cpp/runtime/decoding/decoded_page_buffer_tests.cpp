#include "duckdb_api/internal/runtime/decoding/decoded_page_buffer.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
	try {
		const auto handoff = duckdb_api::internal::TypedBatchHandoffMemoryBytes(2, 4);
		duckdb_api::internal::RequireTypedBatchHandoffMemory(100, 100 + handoff, 2, 4);
		bool one_under_rejected = false;
		try {
			duckdb_api::internal::RequireTypedBatchHandoffMemory(100, 99 + handoff, 2, 4);
		} catch (const duckdb_api::ExecutionError &error) {
			one_under_rejected =
			    error.Stage() == duckdb_api::ErrorStage::RESOURCE && error.Field() == "decoded_memory_bytes";
		}
		duckdb_api_test::Require(one_under_rejected,
		                         "typed batch handoff accepted one byte below its co-live structural capacity");

		duckdb_api::internal::DecodedPageBuffer buffer;
		std::vector<duckdb_api::TypedRow> page(100);
		buffer.Install(std::move(page));
		duckdb_api_test::Require(buffer.Rows().size() == 100 && buffer.Capacity() >= 100,
		                         "decoded page buffer did not own the installed page allocation");
		buffer.Release();
		duckdb_api_test::Require(buffer.Rows().empty() && buffer.Capacity() == 0,
		                         "decoded page release retained prior vector capacity");
		buffer.Release();
		duckdb_api_test::Require(buffer.Capacity() == 0, "decoded page release was not idempotent");
		std::cout << "decoded page buffer tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "decoded page buffer tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
