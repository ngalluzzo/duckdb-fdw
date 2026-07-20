#include "duckdb_api/query_generation.hpp"

#include <stdexcept>
#include <utility>

namespace duckdb_api {

namespace {

bool IsSafeDiagnosticPart(const std::string &value, bool allow_empty) {
	if ((!allow_empty && value.empty()) || value.size() > 512) {
		return false;
	}
	for (const auto character : value) {
		const auto byte = static_cast<unsigned char>(character);
		if (byte < 0x20 || byte == 0x7f) {
			return false;
		}
	}
	return true;
}

} // namespace

QueryStagingError::QueryStagingError(std::string code_p, std::string phase_p, std::string source_p, std::string field_p,
                                     std::string safe_detail_p)
    : code(std::move(code_p)), phase(std::move(phase_p)), source(std::move(source_p)), field(std::move(field_p)),
      safe_detail(std::move(safe_detail_p)), message() {
	if (!IsSafeDiagnosticPart(code, false) || !IsSafeDiagnosticPart(phase, false) ||
	    !IsSafeDiagnosticPart(source, true) || !IsSafeDiagnosticPart(field, true) ||
	    !IsSafeDiagnosticPart(safe_detail, false) || (!source.empty() && source[0] == '/') ||
	    source.find("..") != std::string::npos) {
		throw std::invalid_argument("Query staging diagnostic is not safely renderable");
	}
	message = code + ": " + safe_detail;
}

const char *QueryStagingError::what() const noexcept {
	return message.c_str();
}

const std::string &QueryStagingError::Code() const noexcept {
	return code;
}

const std::string &QueryStagingError::Phase() const noexcept {
	return phase;
}

const std::string &QueryStagingError::Source() const noexcept {
	return source;
}

const std::string &QueryStagingError::Field() const noexcept {
	return field;
}

const std::string &QueryStagingError::SafeDetail() const noexcept {
	return safe_detail;
}

QueryScanPlanningService::~QueryScanPlanningService() noexcept = default;
QueryGenerationOwner::~QueryGenerationOwner() noexcept = default;
QueryPackageStagingService::~QueryPackageStagingService() noexcept = default;

QueryPublishedGeneration::QueryPublishedGeneration(CompiledQueryRegistrationView registration_p,
                                                   std::shared_ptr<const QueryScanPlanningService> planning_p,
                                                   std::shared_ptr<const ScanExecutor> executor_p,
                                                   std::shared_ptr<const QueryGenerationOwner> owner_p)
    : registration(std::move(registration_p)), planning(std::move(planning_p)), executor(std::move(executor_p)),
      owner(std::move(owner_p)) {
	if (!registration.GenerationHandle().IsValid()) {
		throw std::invalid_argument("Query generation requires a valid Connector generation handle");
	}
	if (!planning) {
		throw std::invalid_argument("Query generation requires a planning service");
	}
	if (!executor) {
		throw std::invalid_argument("Query generation requires a scan executor");
	}
	if (!owner) {
		throw std::invalid_argument("Query generation requires an opaque Runtime owner");
	}
}

const CompiledQueryRegistrationView &QueryPublishedGeneration::Registration() const noexcept {
	return registration;
}

const std::shared_ptr<const QueryScanPlanningService> &QueryPublishedGeneration::Planning() const noexcept {
	return planning;
}

const std::shared_ptr<const ScanExecutor> &QueryPublishedGeneration::Executor() const noexcept {
	return executor;
}

const std::shared_ptr<const QueryGenerationOwner> &QueryPublishedGeneration::Owner() const noexcept {
	return owner;
}

QueryStagedGeneration::QueryStagedGeneration(std::shared_ptr<const QueryPublishedGeneration> generation_p,
                                             bool changed_p)
    : generation(std::move(generation_p)), changed(changed_p) {
	if (!generation) {
		throw std::invalid_argument("staged Query generation must not be empty");
	}
}

const std::shared_ptr<const QueryPublishedGeneration> &QueryStagedGeneration::Generation() const noexcept {
	return generation;
}

bool QueryStagedGeneration::Changed() const noexcept {
	return changed;
}

} // namespace duckdb_api
