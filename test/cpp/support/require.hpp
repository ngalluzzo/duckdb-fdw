#pragma once

#include <stdexcept>
#include <string>

namespace duckdb_api_test {

inline void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

} // namespace duckdb_api_test
