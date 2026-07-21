#include "duckdb_api/internal/runtime/execution/rest_request_materialization.hpp"

#include "duckdb_api/internal/runtime/policy/request_validation.hpp"

#include <algorithm>
#include <cstddef>
#include <set>
#include <utility>

namespace duckdb_api {
namespace internal {
namespace {

bool TryColumnKind(const std::string &logical_type, ValueKind &kind) {
	if (logical_type == "VARCHAR") {
		kind = ValueKind::VARCHAR;
		return true;
	}
	if (logical_type == "BIGINT") {
		kind = ValueKind::BIGINT;
		return true;
	}
	if (logical_type == "BOOLEAN") {
		kind = ValueKind::BOOLEAN;
		return true;
	}
	return false;
}

bool TryColumnKind(PlannedRestScalarKind planned, ValueKind &kind) {
	switch (planned) {
	case PlannedRestScalarKind::BOOLEAN:
		kind = ValueKind::BOOLEAN;
		return true;
	case PlannedRestScalarKind::BIGINT:
		kind = ValueKind::BIGINT;
		return true;
	case PlannedRestScalarKind::VARCHAR:
		kind = ValueKind::VARCHAR;
		return true;
	}
	return false;
}

bool TryDirectExtractor(const std::string &extractor, std::string &field) {
	if (extractor.size() < 3 || extractor.compare(0, 2, "$.") != 0) {
		return false;
	}
	field = extractor.substr(2);
	return IsGraphqlName(field);
}

bool TryLegacyRecordsField(const PlannedRestOperation &operation, std::string &field) {
	field.clear();
	if (operation.response_source == PlannedResponseSource::ROOT_ARRAY ||
	    operation.response_source == PlannedResponseSource::ROOT_OBJECT) {
		return operation.records_extractor == "$";
	}
	static const std::string PREFIX = "$.";
	static const std::string SUFFIX = "[*]";
	if (operation.response_source != PlannedResponseSource::JSON_PATH_MANY ||
	    operation.records_extractor.size() <= PREFIX.size() + SUFFIX.size() ||
	    operation.records_extractor.compare(0, PREFIX.size(), PREFIX) != 0 ||
	    operation.records_extractor.compare(operation.records_extractor.size() - SUFFIX.size(), SUFFIX.size(),
	                                        SUFFIX) != 0) {
		return false;
	}
	field = operation.records_extractor.substr(PREFIX.size(),
	                                           operation.records_extractor.size() - PREFIX.size() - SUFFIX.size());
	return IsGraphqlName(field);
}

bool TryCopyPermanentRecordsPath(const PlannedRestOperation &operation, std::vector<std::string> &path) {
	path = operation.records_path.segments;
	if (operation.response_source == PlannedResponseSource::ROOT_ARRAY ||
	    operation.response_source == PlannedResponseSource::ROOT_OBJECT) {
		return path.empty();
	}
	return operation.response_source == PlannedResponseSource::JSON_PATH_MANY && IsSafeRestCollectionPath(path);
}

bool TryCopyLegacyColumns(const std::vector<PlannedColumn> &planned, std::vector<AdmittedRestColumn> &columns) {
	if (planned.empty() || planned.size() > 256) {
		return false;
	}
	std::set<std::string> names;
	columns.clear();
	columns.reserve(planned.size());
	for (const auto &column : planned) {
		ValueKind kind = ValueKind::VARCHAR;
		std::string field;
		if (!IsSafeLogicalId(column.name) || !names.insert(column.name).second ||
		    !TryColumnKind(column.logical_type, kind) || !TryDirectExtractor(column.extractor, field)) {
			return false;
		}
		columns.push_back({column.name, {std::move(field)}, kind, column.nullable});
	}
	return true;
}

bool TryCopyPermanentColumns(const std::vector<PlannedRestResultColumn> &planned,
                             std::vector<AdmittedRestColumn> &columns) {
	if (planned.empty() || planned.size() > 256) {
		return false;
	}
	std::set<std::string> names;
	std::vector<std::vector<std::string>> paths;
	columns.clear();
	columns.reserve(planned.size());
	for (const auto &column : planned) {
		ValueKind kind = ValueKind::VARCHAR;
		if (!IsSafeLogicalId(column.name) || !names.insert(column.name).second ||
		    !IsSafeRestExtractPath(column.response_path.segments) || !TryColumnKind(column.scalar_kind, kind)) {
			return false;
		}
		for (const auto &path : paths) {
			const auto common = std::min(path.size(), column.response_path.segments.size());
			if (std::equal(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(common),
			               column.response_path.segments.begin())) {
				return false;
			}
		}
		paths.push_back(column.response_path.segments);
		columns.push_back({column.name, column.response_path.segments, kind, column.nullable});
	}
	return true;
}

std::string FormUrlEncode(const std::string &value) {
	static const char HEX[] = "0123456789ABCDEF";
	std::string result;
	for (const auto character : value) {
		const auto byte = static_cast<unsigned char>(character);
		const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
		                        (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' || byte == '_' ||
		                        byte == '~';
		if (unreserved) {
			result.push_back(character);
		} else if (byte == 0x20) {
			result.push_back('+');
		} else {
			result.push_back('%');
			result.push_back(HEX[(byte >> 4U) & 0x0FU]);
			result.push_back(HEX[byte & 0x0FU]);
		}
	}
	return result;
}

bool MatchesConditionalAuthority(const PlannedRestQueryBinding &binding,
                                 const RestConditionalBindingAuthority &authority) {
	if (!authority.enabled || binding.SourceId() != authority.source_id || binding.Kind() != authority.kind) {
		return false;
	}
	switch (binding.Kind()) {
	case PlannedRestScalarKind::BOOLEAN:
		return binding.BooleanValue() == authority.boolean_value;
	case PlannedRestScalarKind::BIGINT:
		return binding.BigintValue() == authority.bigint_value;
	case PlannedRestScalarKind::VARCHAR:
		return binding.VarcharValue() == authority.varchar_value;
	}
	return false;
}

bool HasCanonicalBinding(const PlannedRestQueryBinding &binding, const RestConditionalBindingAuthority &conditional,
                         bool &conditional_seen) {
	if (!IsSafeEncodedQueryName(binding.Name()) || !IsSafeEncodedQueryValue(binding.EncodedValue()) ||
	    binding.Encoding() != PlannedRestQueryEncoding::FORM_URLENCODED) {
		return false;
	}
	switch (binding.Source()) {
	case PlannedRestQueryValueSource::FIXED:
	case PlannedRestQueryValueSource::PAGINATION_PAGE_SIZE:
	case PlannedRestQueryValueSource::PAGINATION_PAGE_NUMBER:
		if (!binding.SourceId().empty()) {
			return false;
		}
		break;
	case PlannedRestQueryValueSource::RELATION_INPUT:
		if (binding.SourceId().empty()) {
			return false;
		}
		break;
	case PlannedRestQueryValueSource::CONDITIONAL_INPUT:
		if (conditional_seen || !MatchesConditionalAuthority(binding, conditional)) {
			return false;
		}
		conditional_seen = true;
		break;
	default:
		return false;
	}
	switch (binding.Kind()) {
	case PlannedRestScalarKind::BOOLEAN:
		return binding.EncodedValue() == (binding.BooleanValue() ? "true" : "false");
	case PlannedRestScalarKind::BIGINT:
		return binding.EncodedValue() == std::to_string(binding.BigintValue());
	case PlannedRestScalarKind::VARCHAR:
		return binding.EncodedValue() == FormUrlEncode(binding.VarcharValue());
	}
	return false;
}

bool TryCopyPermanentQuery(const std::vector<PlannedRestQueryBinding> &planned,
                           const RestConditionalBindingAuthority &conditional,
                           std::vector<AdmittedQueryParameter> &query) {
	if (planned.size() > 64) {
		return false;
	}
	std::set<std::string> names;
	query.clear();
	query.reserve(planned.size());
	uint64_t query_bytes = 0;
	bool conditional_seen = false;
	for (const auto &binding : planned) {
		if (!HasCanonicalBinding(binding, conditional, conditional_seen) || !names.insert(binding.Name()).second) {
			return false;
		}
		const uint64_t field_bytes = 1 + binding.Name().size() + 1 + binding.EncodedValue().size();
		if (field_bytes > 8192 - query_bytes) {
			return false;
		}
		query_bytes += field_bytes;
		query.push_back({binding.Name(), binding.EncodedValue()});
	}
	return conditional_seen == conditional.enabled;
}

bool TryCopyLegacyQuery(const std::vector<PlannedQueryParameter> &planned, std::vector<AdmittedQueryParameter> &query) {
	if (planned.size() > 64) {
		return false;
	}
	std::set<std::string> names;
	query.clear();
	query.reserve(planned.size());
	uint64_t query_bytes = 0;
	for (const auto &parameter : planned) {
		if (!IsSafeEncodedQueryName(parameter.name) || !IsSafeEncodedQueryValue(parameter.encoded_value) ||
		    !names.insert(parameter.name).second) {
			return false;
		}
		const uint64_t field_bytes = 1 + parameter.name.size() + 1 + parameter.encoded_value.size();
		if (field_bytes > 8192 - query_bytes) {
			return false;
		}
		query_bytes += field_bytes;
		query.push_back({parameter.name, parameter.encoded_value});
	}
	return true;
}

} // namespace

bool TryMaterializeRestRequest(const ScanPlan &plan, const RestConditionalBindingAuthority &conditional,
                               MaterializedRestRequest &request) {
	if (plan.Operation().Protocol() != PlannedProtocol::REST) {
		return false;
	}
	const auto &operation = plan.Operation().Rest();
	request = MaterializedRestRequest();
	const bool permanent = !operation.result_columns.empty();
	if (permanent) {
		if (!TryCopyPermanentQuery(operation.query_bindings, conditional, request.query) ||
		    !TryCopyPermanentColumns(operation.result_columns, request.columns) ||
		    !TryCopyPermanentRecordsPath(operation, request.records_path)) {
			return false;
		}
	} else {
		std::string legacy_records_field;
		if (!operation.query_bindings.empty() || !operation.records_path.segments.empty() ||
		    !TryCopyLegacyQuery(operation.query_parameters, request.query) ||
		    !TryCopyLegacyColumns(plan.OutputColumns(), request.columns) ||
		    !TryLegacyRecordsField(operation, legacy_records_field)) {
			return false;
		}
		if (!legacy_records_field.empty()) {
			request.records_path.assign(1, std::move(legacy_records_field));
		}
	}
	return FitsRestRequestTarget(operation.path, request.query) &&
	       TryCopyFixedHeaders(operation.headers, false, plan.Budgets().header_bytes, request.headers);
}

bool FitsRestRequestTarget(const std::string &path, const std::vector<AdmittedQueryParameter> &query,
                           uint64_t additional_bytes) noexcept {
	if (path.size() > 8192 || additional_bytes > 8192 - path.size()) {
		return false;
	}
	uint64_t bytes = path.size() + additional_bytes;
	for (const auto &parameter : query) {
		const uint64_t field_bytes = 1 + parameter.name.size() + 1 + parameter.encoded_value.size();
		if (field_bytes > 8192 - bytes) {
			return false;
		}
		bytes += field_bytes;
	}
	return true;
}

const char *RestSchemeName(PlannedUrlScheme scheme) {
	switch (scheme) {
	case PlannedUrlScheme::HTTP:
		return "http";
	case PlannedUrlScheme::HTTPS:
		return "https";
	}
	throw ExecutionError(ErrorStage::POLICY, "", "execution profile contains an unknown URL scheme");
}

std::string BuildRestTarget(const std::string &path, const std::vector<AdmittedQueryParameter> &query,
                            const std::string *page_name, uint64_t page,
                            AdmittedPaginatedRestConditionalInput conditional) {
	std::string result = path;
	bool first = true;
	for (const auto &parameter : query) {
		result += first ? "?" : "&";
		first = false;
		result += parameter.name + "=";
		result += page_name && parameter.name == *page_name ? std::to_string(page) : parameter.encoded_value;
	}
	// Bounded 0.7 compatibility bridge. Native visibility authority remains a
	// distinct plan discriminant rather than a package query binding.
	if (conditional == AdmittedPaginatedRestConditionalInput::LEGACY_VISIBILITY_PRIVATE) {
		result += first ? "?" : "&";
		result += "visibility=private";
	}
	return result;
}

} // namespace internal
} // namespace duckdb_api
