#include "package_fixture_index_parser_internal.hpp"

#include <utility>

namespace duckdb_api {
namespace connector {
namespace internal {
namespace fixture_index_detail {
namespace {

PackageFixtureRow ParseRow(const FailsafeYamlNode &node, const std::string &path, const CompiledRelation &relation) {
	RequireType(node, FailsafeYamlNode::Kind::MAPPING, path);
	if (node.Size() != relation.Columns().size()) {
		Fail(node, path, "fixture expected row does not contain the complete relation schema");
	}
	PackageFixtureRow row;
	for (const auto &column : relation.Columns()) {
		const auto *cell = node.Find(column.name);
		if (cell == nullptr) {
			Fail(node, path, "fixture expected row is missing a compiled column");
		}
		PackageFixtureCell value;
		if (cell->Type() == FailsafeYamlNode::Kind::MAPPING) {
			const auto parsed = ParseValue(*cell, path + "." + column.name);
			if (!parsed.is_null || !column.nullable) {
				Fail(*cell, path + "." + column.name, "fixture expected NULL violates column nullability");
			}
			value = {true, ""};
		} else {
			const auto scalar = Scalar(*cell, path + "." + column.name);
			if (!IsTypedScalar(column.ScalarType(), scalar)) {
				Fail(*cell, path + "." + column.name, "fixture expected cell has the wrong scalar type");
			}
			value = {false, scalar};
		}
		row.cells.push_back(std::move(value));
	}
	return row;
}

} // namespace

PackageFixtureExpected ParseExpected(const FailsafeYamlNode &node, const std::string &path,
                                     const CompiledRelation &relation) {
	RequireType(node, FailsafeYamlNode::Kind::MAPPING, path);
	if (node.Find("diagnostic_code")) {
		ClosedMapping(node, {"diagnostic_code"}, path);
		return {PackageFixtureExpectedKind::COMPILER_DIAGNOSTIC,
		        PackageFixtureRemoteAccuracy::UNSUPPORTED,
		        {},
		        {},
		        Scalar(Required(node, "diagnostic_code", path), path + ".diagnostic_code"),
		        "",
		        ""};
	}
	if (node.Find("runtime_error")) {
		ClosedMapping(node, {"runtime_error"}, path);
		const auto &error = Required(node, "runtime_error", path);
		ClosedMapping(error, {"stage", "field"}, path + ".runtime_error");
		return {PackageFixtureExpectedKind::RUNTIME_ERROR,
		        PackageFixtureRemoteAccuracy::UNSUPPORTED,
		        {},
		        {},
		        "",
		        Scalar(Required(error, "stage", path + ".runtime_error"), path + ".runtime_error.stage"),
		        Scalar(Required(error, "field", path + ".runtime_error"), path + ".runtime_error.field")};
	}
	ClosedMapping(node, {"residual_owner", "remote_accuracy", "rows", "explain"}, path);
	if (Scalar(Required(node, "residual_owner", path), path + ".residual_owner") != "duckdb") {
		Fail(node, path, "fixture residual owner must remain DuckDB");
	}
	const auto accuracy = Scalar(Required(node, "remote_accuracy", path), path + ".remote_accuracy");
	PackageFixtureRemoteAccuracy parsed_accuracy;
	if (accuracy == "exact") {
		parsed_accuracy = PackageFixtureRemoteAccuracy::EXACT;
	} else if (accuracy == "superset") {
		parsed_accuracy = PackageFixtureRemoteAccuracy::SUPERSET;
	} else if (accuracy == "unsupported") {
		parsed_accuracy = PackageFixtureRemoteAccuracy::UNSUPPORTED;
	} else {
		Fail(node, path, "fixture remote accuracy is outside the closed vocabulary");
	}
	const auto &rows = Required(node, "rows", path);
	RequireType(rows, FailsafeYamlNode::Kind::SEQUENCE, path + ".rows");
	if (rows.Size() > 100000) {
		Fail(rows, path + ".rows", "fixture expected-row budget is exhausted", "", "", "",
		     FixtureIndexFailureKind::RESOURCE_EXHAUSTED);
	}
	std::vector<PackageFixtureRow> parsed_rows;
	for (std::size_t index = 0; index < rows.Size(); index++) {
		parsed_rows.push_back(
		    ParseRow(rows.SequenceValue(index), path + ".rows[" + std::to_string(index) + "]", relation));
	}
	const auto &explain = Required(node, "explain", path);
	ClosedMapping(explain,
	              {"protocol", "pagination", "conditional_input", "predicate_owner", "relation", "operation", "auth"},
	              path + ".explain");
	std::vector<PackageFixtureFact> facts;
	for (std::size_t index = 0; index < explain.Size(); index++) {
		facts.push_back({explain.MappingKey(index),
		                 Scalar(explain.MappingValue(index), path + ".explain." + explain.MappingKey(index))});
	}
	return {PackageFixtureExpectedKind::SUCCESS, parsed_accuracy, std::move(parsed_rows), std::move(facts), "", "", ""};
}

} // namespace fixture_index_detail
} // namespace internal
} // namespace connector
} // namespace duckdb_api
