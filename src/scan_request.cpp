#include "duckdb_api/scan_request.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace duckdb_api {

LogicalSecretReference::LogicalSecretReference() : exact_duckdb_secret_name() {
}

LogicalSecretReference::LogicalSecretReference(std::string exact_duckdb_secret_name_p)
    : exact_duckdb_secret_name(std::move(exact_duckdb_secret_name_p)) {
}

LogicalSecretReference LogicalSecretReference::Named(std::string exact_duckdb_secret_name) {
	if (exact_duckdb_secret_name.empty()) {
		throw std::invalid_argument("logical secret reference name must not be empty");
	}
	return LogicalSecretReference(std::move(exact_duckdb_secret_name));
}

bool LogicalSecretReference::IsPresent() const noexcept {
	return !exact_duckdb_secret_name.empty();
}

const std::string &LogicalSecretReference::Name() const {
	if (!IsPresent()) {
		throw std::logic_error("logical secret reference is absent");
	}
	return exact_duckdb_secret_name;
}

std::string LogicalSecretReference::Snapshot() const {
	if (!IsPresent()) {
		return "none";
	}
	static const char HEX_DIGITS[] = "0123456789abcdef";
	std::string result = "named-hex:";
	result.reserve(result.size() + exact_duckdb_secret_name.size() * 2);
	for (const char character : exact_duckdb_secret_name) {
		const auto byte = static_cast<unsigned char>(character);
		result.push_back(HEX_DIGITS[byte >> 4]);
		result.push_back(HEX_DIGITS[byte & 0x0f]);
	}
	return result;
}

bool AdapterCapabilities::HasConservativeRelationalProfile() const {
	return !projection && !filter && !ordering && !limit && !offset && !progress && cancellation;
}

std::string ScanRequest::Snapshot() const {
	std::ostringstream result;
	result << "connector=" << connector_name << ";relation=" << relation_name
	       << ";inputs=" << (explicit_inputs.empty() ? "[]" : "unexpected") << ";projection=";
	for (std::size_t index = 0; index < projected_columns.size(); index++) {
		if (index > 0) {
			result << ',';
		}
		result << projected_columns[index];
	}
	result << ";predicate=" << predicate << ";ordering=" << (orderings.empty() ? "[]" : "unexpected")
	       << ";limit=" << (has_limit ? "set" : "unset") << ";offset=" << (has_offset ? "set" : "unset")
	       << ";capabilities=projection:" << (capabilities.projection ? "available" : "unavailable")
	       << ",filter:" << (capabilities.filter ? "available" : "unavailable")
	       << ",ordering:" << (capabilities.ordering ? "available" : "unavailable")
	       << ",limit:" << (capabilities.limit ? "available" : "unavailable")
	       << ",offset:" << (capabilities.offset ? "available" : "unavailable")
	       << ",progress:" << (capabilities.progress ? "available" : "unavailable")
	       << ",cancellation:" << (capabilities.cancellation ? "verified" : "unavailable")
	       << ",secret-manager:" << (capabilities.secret_manager ? "available" : "unavailable")
	       << ";secret-reference=" << secret_reference.Snapshot();
	return result.str();
}

ScanRequest BuildConservativeScanRequest(const CompiledConnector &connector, const std::string &relation_name,
                                         LogicalSecretReference secret_reference) {
	const auto *relation = connector.FindRelation(relation_name);
	if (!relation) {
		throw std::invalid_argument("requested connector relation was not found");
	}
	const auto requirement = relation->Authentication().Requirement();
	if (requirement == CompiledCredentialRequirement::NONE && secret_reference.IsPresent()) {
		throw std::invalid_argument("anonymous relation does not accept a logical secret reference");
	}
	if (requirement == CompiledCredentialRequirement::REQUIRED && !secret_reference.IsPresent()) {
		throw std::invalid_argument("authenticated relation requires a logical secret reference");
	}
	if (requirement != CompiledCredentialRequirement::NONE && requirement != CompiledCredentialRequirement::REQUIRED) {
		throw std::invalid_argument("selected relation has an unsupported credential requirement");
	}

	ScanRequest result;
	result.connector_name = connector.ConnectorName();
	result.relation_name = relation->Name();
	result.explicit_inputs.clear();
	result.projected_columns.reserve(relation->Columns().size());
	for (const auto &column : relation->Columns()) {
		result.projected_columns.push_back(column.name);
	}
	result.predicate = "TRUE";
	result.orderings.clear();
	result.has_limit = false;
	result.has_offset = false;
	result.capabilities = {false, false, false, false, false, false, true, true};
	result.secret_reference = std::move(secret_reference);
	return result;
}

} // namespace duckdb_api
