#include "live_rest/runtime.hpp"

#include <utility>

namespace live_rest {

// Error taxonomy and interface destructors live independently from transport
// and decoding so consumers can classify safe failures without either module.
RuntimeError::RuntimeError(RuntimeStage stage_p, std::string field_p, std::string safe_message)
    : std::runtime_error(std::move(safe_message)), stage(stage_p), field(std::move(field_p)) {
}

RuntimeStage RuntimeError::Stage() const noexcept {
	return stage;
}

const std::string &RuntimeError::Field() const noexcept {
	return field;
}

ExecutionCancelled::ExecutionCancelled() : std::runtime_error("live REST execution was cancelled") {
}

CancellationView::~CancellationView() noexcept = default;
HttpTransport::~HttpTransport() noexcept = default;
BatchStream::~BatchStream() noexcept = default;
ScanExecutor::~ScanExecutor() noexcept = default;

} // namespace live_rest
