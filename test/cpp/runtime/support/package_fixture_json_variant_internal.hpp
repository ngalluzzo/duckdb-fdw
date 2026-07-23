#pragma once

#include "package_fixture_execution.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace duckdb_api_test {
namespace internal {

struct RuntimeFixtureJsonColumn {
	std::string name;
	duckdb_api::OutputValueType type;
	bool nullable;
	std::vector<std::string> path;
};

struct RuntimeFixtureJsonShape {
	bool records_are_array;
	std::vector<std::string> records_path;
	std::vector<RuntimeFixtureJsonColumn> columns;
};

// Re-runs production protocol admission and exposes only the admitted response
// shape needed by Runtime's closed JSON variants. It never derives fields from
// package, source, fixture, explanation, or display-key text.
RuntimeFixtureJsonShape AdmitRuntimeFixtureJsonShape(const duckdb_api::ScanPlan &plan);

std::string ReplaceFirstRuntimeFixtureColumn(const std::string &body, const RuntimeFixtureJsonShape &shape,
                                             std::size_t column_ordinal, const std::string &replacement,
                                             bool remove_member, duckdb_api::ExecutionControl &control);

std::string RepeatFirstRuntimeFixtureRecord(const std::string &body, const RuntimeFixtureJsonShape &shape,
                                            uint64_t record_count, uint64_t max_derived_body_bytes,
                                            duckdb_api::ExecutionControl &control);

std::string ReplaceRuntimeFixturePath(const std::string &body, const std::vector<std::string> &path,
                                      const std::string &replacement, bool remove_member,
                                      duckdb_api::ExecutionControl &control);

} // namespace internal
} // namespace duckdb_api_test
