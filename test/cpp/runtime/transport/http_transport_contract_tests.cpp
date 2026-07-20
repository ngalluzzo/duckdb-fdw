#include "duckdb_api/internal/runtime/transport/http_transport.hpp"
#include "support/require.hpp"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using duckdb_api_test::Require;

class Control final : public duckdb_api::ExecutionControl {
public:
	explicit Control(bool cancelled_p = false) : cancelled(cancelled_p) {
	}

	bool IsCancellationRequested() const noexcept override {
		return cancelled;
	}

	bool cancelled;
};

class GetOnlyTransport final : public duckdb_api::internal::HttpTransport {
public:
	GetOnlyTransport() : get_count(0) {
	}

	duckdb_api::internal::HttpResponse Get(const duckdb_api::internal::HttpRequest &,
	                                       const duckdb_api::internal::HttpLimits &,
	                                       duckdb_api::ExecutionControl &) const override {
		get_count++;
		return {200, 0, 0, "", {{}, 0}};
	}

	mutable uint64_t get_count;
};

class GetAndPostTransport final : public duckdb_api::internal::HttpTransport {
public:
	GetAndPostTransport() : get_count(0), post_count(0) {
	}

	duckdb_api::internal::HttpResponse Get(const duckdb_api::internal::HttpRequest &,
	                                       const duckdb_api::internal::HttpLimits &,
	                                       duckdb_api::ExecutionControl &) const override {
		get_count++;
		return {200, 0, 0, "", {{}, 0}};
	}

	duckdb_api::internal::HttpResponse Post(const duckdb_api::internal::HttpRequest &,
	                                        const duckdb_api::internal::HttpLimits &,
	                                        duckdb_api::ExecutionControl &) const override {
		post_count++;
		return {200, 0, 0, "", {{}, 0}};
	}

	mutable uint64_t get_count;
	mutable uint64_t post_count;
};

duckdb_api::internal::HttpLimits Limits(uint64_t max_request_body_bytes) {
	return {max_request_body_bytes, 64, 64, 64, 0, std::chrono::steady_clock::now() + std::chrono::seconds(1)};
}

duckdb_api::internal::HttpRequest Request(std::string method) {
	duckdb_api::internal::HttpRequest result;
	result.method = std::move(method);
	result.scheme = "https";
	result.host = "api.github.com";
	result.port = 443;
	result.target = "/graphql";
	return result;
}

void RequireError(const std::function<void()> &action, duckdb_api::ErrorStage stage, const std::string &field,
                  const std::string &label) {
	bool rejected = false;
	try {
		action();
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = true;
		Require(error.Stage() == stage, label + " used the wrong error stage");
		Require(error.Field() == field, label + " used the wrong safe field");
		Require(error.SafeMessage().find("query-canary") == std::string::npos, label + " exposed request content");
	}
	Require(rejected, label + " was not rejected");
}

void TestGetOnlySourceCompatibilityAndCancellation() {
	GetOnlyTransport transport;
	Control active;
	auto request = Request("GET");
	Require(transport.Execute(request, Limits(0), active).status == 200 && transport.get_count == 1,
	        "existing GET-only transport did not execute through the shared dispatcher");

	Control cancelled(true);
	bool saw_cancel = false;
	try {
		(void)transport.Execute(request, Limits(0), cancelled);
	} catch (const duckdb_api::ExecutionCancelled &) {
		saw_cancel = true;
	}
	Require(saw_cancel && transport.get_count == 1,
	        "cancellation did not stop before the concrete transport observed the request");

	auto post = Request("POST");
	post.body = "query-canary";
	post.content_type = "application/json";
	RequireError([&]() { (void)transport.Execute(post, Limits(post.body.size()), active); },
	             duckdb_api::ErrorStage::POLICY, "method", "GET-only POST dispatch");
	Require(transport.get_count == 1, "unsupported POST fell back to GET");
}

void TestPostBodyAndContentTypeContract() {
	GetAndPostTransport transport;
	Control control;
	auto request = Request("POST");
	request.body = "query-canary";
	request.content_type = "application/json";
	Require(transport.Execute(request, Limits(request.body.size()), control).status == 200 && transport.post_count == 1,
	        "exact-boundary JSON POST did not reach the opted-in transport");

	RequireError([&]() { (void)transport.Execute(request, Limits(request.body.size() - 1), control); },
	             duckdb_api::ErrorStage::RESOURCE, "request_body_bytes", "one-byte-over POST body");
	Require(transport.post_count == 1, "oversized POST reached the concrete transport");

	auto wrong_type = request;
	wrong_type.content_type = "application/graphql";
	RequireError([&]() { (void)transport.Execute(wrong_type, Limits(64), control); }, duckdb_api::ErrorStage::POLICY,
	             "request_body", "non-JSON POST content type");

	auto empty_body = request;
	empty_body.body.clear();
	RequireError([&]() { (void)transport.Execute(empty_body, Limits(64), control); }, duckdb_api::ErrorStage::POLICY,
	             "request_body", "empty POST body");

	const char *header_names[] = {"Content-Type", "content-type", "CONTENT-TYPE"};
	for (const auto *header_name : header_names) {
		auto duplicate = request;
		duplicate.headers.push_back({header_name, "application/json"});
		RequireError([&]() { (void)transport.Execute(duplicate, Limits(64), control); }, duckdb_api::ErrorStage::POLICY,
		             "content_type", "duplicate content-type header");
	}
	Require(transport.post_count == 1, "invalid POST contract reached the concrete transport");
}

void TestBodylessAndClosedMethodContract() {
	GetAndPostTransport transport;
	Control control;
	auto get_with_body = Request("GET");
	get_with_body.body = "query-canary";
	get_with_body.content_type = "application/json";
	RequireError([&]() { (void)transport.Execute(get_with_body, Limits(0), control); }, duckdb_api::ErrorStage::POLICY,
	             "request_body", "GET body");

	auto unknown = Request("PATCH");
	unknown.body = "query-canary";
	unknown.content_type = "application/json";
	RequireError([&]() { (void)transport.Execute(unknown, Limits(0), control); }, duckdb_api::ErrorStage::POLICY,
	             "method", "unknown method");
	Require(transport.get_count == 0 && transport.post_count == 0,
	        "invalid method or GET body reached the concrete transport");
}

} // namespace

int main() {
	try {
		TestGetOnlySourceCompatibilityAndCancellation();
		TestPostBodyAndContentTypeContract();
		TestBodylessAndClosedMethodContract();
		std::cout << "HTTP transport contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "HTTP transport contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
