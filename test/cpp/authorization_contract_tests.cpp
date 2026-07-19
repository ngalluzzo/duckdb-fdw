#include "duckdb_api/execution.hpp"
#include "support/require.hpp"
#include "support/scan_plan_test_fixtures.hpp"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using duckdb_api_test::Require;

static_assert(!std::is_default_constructible<duckdb_api::ScanAuthorization>::value,
              "authorization must require an explicit alternative");
static_assert(!std::is_copy_constructible<duckdb_api::ScanAuthorization>::value,
              "authorization must not be copy constructible");
static_assert(!std::is_copy_assignable<duckdb_api::ScanAuthorization>::value,
              "authorization must not be copy assignable");
static_assert(std::is_nothrow_move_constructible<duckdb_api::ScanAuthorization>::value,
              "authorization moves must not throw");
static_assert(std::is_nothrow_move_assignable<duckdb_api::ScanAuthorization>::value,
              "authorization move assignment must not throw");
static_assert(std::is_nothrow_destructible<duckdb_api::ScanAuthorization>::value,
              "authorization teardown must not throw");
static_assert(!std::is_convertible<duckdb_api::ScanAuthorization, std::string>::value,
              "authorization must not expose plaintext conversion");

class ManualControl final : public duckdb_api::ExecutionControl {
public:
	ManualControl() : cancelled(false) {
	}

	bool IsCancellationRequested() const noexcept override {
		return cancelled;
	}

	bool cancelled;
};

class AnonymousExecutor final : public duckdb_api::ScanExecutor {
public:
	AnonymousExecutor() : open_count(0) {
	}

	std::unique_ptr<duckdb_api::BatchStream> Open(const duckdb_api::ScanPlan &,
	                                              duckdb_api::ExecutionControl &) const override {
		open_count++;
		return std::unique_ptr<duckdb_api::BatchStream>();
	}

	mutable std::size_t open_count;
};

duckdb_api::ScanPlan AnonymousPlan() {
	return duckdb_api_test::BuildValidAnonymousPlanFixture();
}

std::string TokenCanary() {
	return std::string(11, 'a') + "." + std::string(13, 'B') + "_phase1";
}

void RequireRejected(const std::function<void()> &action, duckdb_api::ErrorStage expected_stage,
                     const std::string &forbidden) {
	bool rejected = false;
	try {
		action();
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = true;
		Require(error.Stage() == expected_stage, "authorization failure used the wrong stage");
		Require(error.Field() == "authorization", "authorization failure used an unstable field");
		Require(!error.SafeMessage().empty() && error.SafeMessage().size() <= 128,
		        "authorization failure was empty or unbounded");
		Require(forbidden.empty() || error.SafeMessage().find(forbidden) == std::string::npos,
		        "authorization failure exposed credential bytes");
	}
	Require(rejected, "authorization operation did not fail closed");
}

void RequireHeaderBudgetRejected(const std::function<void()> &action, const std::string &forbidden) {
	bool rejected = false;
	try {
		action();
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = true;
		Require(error.Stage() == duckdb_api::ErrorStage::RESOURCE, "oversized bearer token used the wrong error stage");
		Require(error.Field() == "header_bytes", "oversized bearer token used an unstable resource field");
		Require(error.SafeMessage() == "bearer token exceeds the 8192-byte request-header limit",
		        "oversized bearer token used an unstable safe diagnostic");
		Require(error.SafeMessage().find(forbidden) == std::string::npos,
		        "oversized bearer token escaped through its diagnostic");
	}
	Require(rejected, "oversized bearer token did not fail closed");
}

void TestLegacyAnonymousCompatibilityAndEnvelopeFailClosed() {
	AnonymousExecutor executor;
	ManualControl control;
	const auto plan = AnonymousPlan();
	const auto legacy_stream = executor.Open(plan, control);
	Require(!legacy_stream, "legacy anonymous executor returned an unexpected stream");
	Require(executor.open_count == 1, "legacy anonymous service did not remain available");

	auto anonymous = duckdb_api::ScanAuthorization::Anonymous();
	RequireRejected([&]() { (void)executor.OpenWithAuthorization(plan, std::move(anonymous), control); },
	                duckdb_api::ErrorStage::POLICY, "");
	Require(executor.open_count == 1, "explicit envelope fell back to legacy anonymous execution");

	RequireRejected([&]() { (void)executor.OpenWithAuthorization(plan, std::move(anonymous), control); },
	                duckdb_api::ErrorStage::AUTHENTICATION, "");
	Require(executor.open_count == 1, "moved-from anonymous authorization reached the executor");
}

void TestAuthorizedCapabilityFailsClosedUntilImplemented() {
	AnonymousExecutor executor;
	ManualControl control;
	const auto plan = AnonymousPlan();
	const auto token = TokenCanary();
	auto token_input = token;
	auto authorized = duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token_input));

	RequireRejected([&]() { (void)executor.OpenWithAuthorization(plan, std::move(authorized), control); },
	                duckdb_api::ErrorStage::POLICY, token);
	Require(executor.open_count == 0, "bearer authorization fell back to anonymous execution");

	RequireRejected([&]() { (void)executor.OpenWithAuthorization(plan, std::move(authorized), control); },
	                duckdb_api::ErrorStage::AUTHENTICATION, token);
	Require(executor.open_count == 0, "duplicate authorization use reached the anonymous executor");
}

void TestUnsafeBearerTokensAreRejectedWithoutDisclosure() {
	const std::vector<std::string> invalid_tokens = {"",
	                                                 "contains space",
	                                                 std::string("carriage\rreturn"),
	                                                 std::string("line\nfeed"),
	                                                 std::string("horizontal\ttab"),
	                                                 std::string("embedded\0nul", 12),
	                                                 std::string(1, static_cast<char>(0x7f)),
	                                                 std::string(1, static_cast<char>(0x80))};
	for (const auto &token : invalid_tokens) {
		auto token_input = token;
		RequireRejected([&]() { (void)duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token_input)); },
		                duckdb_api::ErrorStage::AUTHENTICATION, token);
	}
}

void TestBearerTokenByteBoundary() {
	const auto limit = duckdb_api::ScanAuthorization::GithubUserBearerTokenByteLimit();
	Require(limit == 8 * 1024, "fixed GitHub bearer-token byte limit drifted");
	auto exact = std::string(static_cast<std::size_t>(limit), 'e');
	auto authorization = duckdb_api::ScanAuthorization::GithubUserBearer(std::move(exact));
	(void)authorization;

	auto over = std::string(static_cast<std::size_t>(limit + 1), 'o');
	const auto canary = over;
	RequireHeaderBudgetRejected([&]() { (void)duckdb_api::ScanAuthorization::GithubUserBearer(std::move(over)); },
	                            canary);
}

void TestCancellationPrecedesCapabilityUse() {
	AnonymousExecutor executor;
	ManualControl control;
	control.cancelled = true;
	const auto plan = AnonymousPlan();
	const auto token = TokenCanary();
	auto token_input = token;
	auto authorized = duckdb_api::ScanAuthorization::GithubUserBearer(std::move(token_input));
	bool cancelled = false;
	try {
		(void)executor.OpenWithAuthorization(plan, std::move(authorized), control);
	} catch (const duckdb_api::ExecutionCancelled &) {
		cancelled = true;
	}
	Require(cancelled, "authorized open did not preserve call-scoped cancellation");
	Require(executor.open_count == 0, "cancelled authorized open reached the anonymous executor");

	control.cancelled = false;
	RequireRejected([&]() { (void)executor.OpenWithAuthorization(plan, std::move(authorized), control); },
	                duckdb_api::ErrorStage::AUTHENTICATION, token);
	Require(executor.open_count == 0, "capability consumed by cancellation was reusable");
}

} // namespace

int main() {
	try {
		TestLegacyAnonymousCompatibilityAndEnvelopeFailClosed();
		TestAuthorizedCapabilityFailsClosedUntilImplemented();
		TestUnsafeBearerTokensAreRejectedWithoutDisclosure();
		TestBearerTokenByteBoundary();
		TestCancellationPrecedesCapabilityUse();
		std::cout << "authorization contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "authorization contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
