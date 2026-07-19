#include "complex_filter_adapter.hpp"

#include "duckdb/common/types/value.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/expression/bound_comparison_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/operator/logical_get.hpp"

namespace duckdb {
namespace duckdb_api_query_internal {
namespace {

bool IsVisibilityColumn(const LogicalGet &get, const Expression &expression) {
	if (expression.GetExpressionClass() != ExpressionClass::BOUND_COLUMN_REF ||
	    expression.return_type != LogicalType::VARCHAR || get.names.empty() ||
	    get.names.size() != get.returned_types.size()) {
		return false;
	}
	const auto &column = expression.Cast<BoundColumnRefExpression>();
	if (column.depth != 0 || column.binding.table_index != get.table_index) {
		return false;
	}

	// DuckDB binds a column reference to the scan-local slot. After column
	// pruning that slot is not necessarily the declared schema ordinal, so use
	// LogicalGet's typed column-id map rather than assuming they are identical.
	const auto &column_ids = get.GetColumnIds();
	idx_t declared_index;
	if (column_ids.empty()) {
		if (column.binding.column_index != 0) {
			return false;
		}
		declared_index = 0;
	} else {
		if (column.binding.column_index >= column_ids.size() ||
		    column_ids[column.binding.column_index].IsVirtualColumn()) {
			return false;
		}
		declared_index = column_ids[column.binding.column_index].GetPrimaryIndex();
	}
	const auto visibility_index = get.names.size() - 1;
	return declared_index == visibility_index && get.names[declared_index] == "visibility" &&
	       get.returned_types[declared_index] == LogicalType::VARCHAR;
}

bool IsPrivateVarcharConstant(const Expression &expression) {
	if (expression.GetExpressionClass() != ExpressionClass::BOUND_CONSTANT ||
	    expression.return_type != LogicalType::VARCHAR) {
		return false;
	}
	const auto &value = expression.Cast<BoundConstantExpression>().value;
	return value.type() == LogicalType::VARCHAR && !value.IsNull() && StringValue::Get(value) == "private";
}

} // namespace

bool IsVisibilityEqualsPrivate(const LogicalGet &get, const Expression &expression) {
	if (expression.GetExpressionClass() != ExpressionClass::BOUND_COMPARISON ||
	    expression.type != ExpressionType::COMPARE_EQUAL || expression.return_type != LogicalType::BOOLEAN) {
		return false;
	}
	const auto &comparison = expression.Cast<BoundComparisonExpression>();
	return (IsVisibilityColumn(get, *comparison.left) && IsPrivateVarcharConstant(*comparison.right)) ||
	       (IsPrivateVarcharConstant(*comparison.left) && IsVisibilityColumn(get, *comparison.right));
}

} // namespace duckdb_api_query_internal
} // namespace duckdb
