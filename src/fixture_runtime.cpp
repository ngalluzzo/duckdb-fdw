#include "duckdb_api/internal/fixture_runtime.hpp"

#include "duckdb_api/internal/fixture_decoder.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace duckdb_api {

namespace {

void ReportInterruptionNoThrow(FixtureSource &source, bool &reported) noexcept {
	if (reported) {
		return;
	}
	reported = true;
	try {
		source.OnInterruption();
	} catch (...) {
		// Cancellation is the primary outcome; an observer cannot replace it.
	}
}

void CloseSourceNoThrow(FixtureSource &source) noexcept {
	try {
		source.OnStreamClose();
	} catch (...) {
		// Teardown is best-effort and cannot escape a lifecycle boundary.
	}
}

void ValidateExecutablePlan(const ScanPlan &plan) {
	const std::vector<std::string> expected_columns = {"id", "name", "active"};
	if (plan.operation_name != "items_list" || plan.executor_name != "fixture_rest" || plan.method != "GET" ||
	    plan.path != "/items" || plan.extractor != "$.items[*]" || plan.fixture_digest.empty() ||
	    plan.output_columns != expected_columns || plan.remote_predicate != "TRUE" ||
	    plan.runtime_residual_predicate != "TRUE" || !plan.remote_ordering.empty() || !plan.runtime_ordering.empty() ||
	    plan.has_remote_limit || plan.has_remote_offset || plan.has_runtime_limit || plan.has_runtime_offset ||
	    plan.pagination_enabled || plan.providers_enabled || plan.retry_enabled || plan.cache_enabled ||
	    plan.network_enabled || !plan.budgets.IsPreviewBudget()) {
		throw ExecutionError(ErrorStage::POLICY, "", "scan plan is not authorized for fixture execution");
	}
}

void RequireNotCancelled(ExecutionControl &control) {
	if (control.IsCancellationRequested()) {
		throw ExecutionCancelled();
	}
}

class FixtureBatchStream : public BatchStream {
public:
	FixtureBatchStream(ScanPlan plan_p, std::unique_ptr<FixtureSource> source_p, ExecutionControl &control)
	    : plan(std::move(plan_p)), source(std::move(source_p)), offset(0), loaded(false), cancelled(false),
	      interruption_reported(false), closed(false),
	      deadline(std::chrono::steady_clock::now() + std::chrono::milliseconds(plan.budgets.wall_milliseconds)) {
		try {
			if (!source || source->ContentDigest() != plan.fixture_digest) {
				throw ExecutionError(ErrorStage::POLICY, "", "fixture identity does not match the immutable scan plan");
			}
			if (control.IsCancellationRequested()) {
				ReportInterruptionNoThrow(*source, interruption_reported);
				throw ExecutionCancelled();
			}
			source->OnStreamOpen();
		} catch (...) {
			if (source) {
				CloseSourceNoThrow(*source);
			}
			closed = true;
			throw;
		}
	}

	~FixtureBatchStream() noexcept override {
		Close();
	}

	bool Next(ExecutionControl &control, std::vector<ItemRow> &output) override {
		output.clear();
		if (closed) {
			return false;
		}
		FixtureReadBuffer checkpoint(control, *source, cancelled, interruption_reported, plan.budgets.fixture_bytes,
		                             deadline);
		checkpoint.Checkpoint();
		if (!loaded) {
			source->Read(checkpoint);
			checkpoint.Checkpoint();
			rows = internal::DecodeFixtureItems(checkpoint, plan.budgets);
			checkpoint.Checkpoint();
			loaded = true;
		}
		checkpoint.Checkpoint();
		if (offset >= rows.size()) {
			return false;
		}
		const auto remaining = static_cast<uint64_t>(rows.size() - offset);
		const auto count = std::min(plan.budgets.batch_rows, remaining);
		output.reserve(static_cast<std::size_t>(count));
		for (uint64_t index = 0; index < count; index++) {
			checkpoint.Checkpoint();
			output.push_back(rows[offset + static_cast<std::size_t>(index)]);
		}
		offset += static_cast<std::size_t>(count);
		source->OnBatch(count);
		return true;
	}

	void Cancel() noexcept override {
		cancelled.store(true, std::memory_order_relaxed);
	}

	void Close() noexcept override {
		if (closed) {
			return;
		}
		closed = true;
		if (source) {
			CloseSourceNoThrow(*source);
		}
		rows.clear();
	}

private:
	ScanPlan plan;
	std::unique_ptr<FixtureSource> source;
	std::vector<ItemRow> rows;
	std::size_t offset;
	bool loaded;
	std::atomic<bool> cancelled;
	bool interruption_reported;
	bool closed;
	std::chrono::steady_clock::time_point deadline;
};

class FixtureScanExecutor : public ScanExecutor {
public:
	explicit FixtureScanExecutor(std::shared_ptr<FixtureFactory> factory_p) : factory(std::move(factory_p)) {
	}

	std::unique_ptr<BatchStream> Open(const ScanPlan &plan, ExecutionControl &control) const override {
		ValidateExecutablePlan(plan);
		RequireNotCancelled(control);
		if (factory->ContentDigest() != plan.fixture_digest) {
			throw ExecutionError(ErrorStage::POLICY, "", "fixture identity does not match the immutable scan plan");
		}
		RequireNotCancelled(control);
		return std::unique_ptr<BatchStream>(new FixtureBatchStream(plan, factory->Open(), control));
	}

private:
	std::shared_ptr<FixtureFactory> factory;
};

} // namespace

FixtureReadBuffer::FixtureReadBuffer(ExecutionControl &control_p, FixtureSource &source_p,
                                     const std::atomic<bool> &cancelled_p, bool &interruption_reported_p,
                                     uint64_t max_bytes_p, std::chrono::steady_clock::time_point deadline_p)
    : control(control_p), source(source_p), cancelled(cancelled_p), interruption_reported(interruption_reported_p),
      max_bytes(max_bytes_p), deadline(deadline_p) {
}

void FixtureReadBuffer::Checkpoint() {
	if (cancelled.load(std::memory_order_relaxed) || control.IsCancellationRequested()) {
		ReportInterruptionNoThrow(source, interruption_reported);
		throw ExecutionCancelled();
	}
	if (std::chrono::steady_clock::now() >= deadline) {
		throw ExecutionError(ErrorStage::POLICY, "", "execution exceeds the wall-time budget");
	}
}

void FixtureReadBuffer::Append(const std::string &chunk) {
	Checkpoint();
	if (contents.size() > max_bytes || chunk.size() > max_bytes - contents.size()) {
		throw ExecutionError(ErrorStage::POLICY, "", "response exceeds the fixture-byte budget");
	}
	contents.append(chunk);
}

const std::string &FixtureReadBuffer::Contents() const {
	return contents;
}

FixtureSource::~FixtureSource() noexcept {
}

void FixtureSource::OnStreamOpen() {
}

void FixtureSource::OnBatch(uint64_t) {
}

void FixtureSource::OnInterruption() {
}

void FixtureSource::OnStreamClose() {
}

FixtureFactory::~FixtureFactory() noexcept {
}

std::shared_ptr<const ScanExecutor> BuildFixtureScanExecutor(std::shared_ptr<FixtureFactory> factory) {
	if (!factory) {
		throw ExecutionError(ErrorStage::INTERNAL, "", "fixture executor requires a provider");
	}
	return std::shared_ptr<const ScanExecutor>(new FixtureScanExecutor(std::move(factory)));
}

} // namespace duckdb_api
