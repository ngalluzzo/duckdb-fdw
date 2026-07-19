#pragma once

#include "duckdb_api/execution.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace duckdb_api {
namespace internal {

// Scan-owned storage for exactly one decoded page. Release swaps with an empty
// vector so row objects and vector capacity are returned before accounting
// authorizes another page decode; clear() alone would retain the old page's
// allocation and permit two page buffers to overlap invisibly.
class DecodedPageBuffer {
public:
	std::vector<TypedRow> &Rows() noexcept {
		return rows;
	}

	const std::vector<TypedRow> &Rows() const noexcept {
		return rows;
	}

	void Install(std::vector<TypedRow> incoming) {
		rows = std::move(incoming);
	}

	void Release() noexcept {
		std::vector<TypedRow> empty;
		rows.swap(empty);
	}

	std::size_t Capacity() const noexcept {
		return rows.capacity();
	}

private:
	std::vector<TypedRow> rows;
};

} // namespace internal
} // namespace duckdb_api
