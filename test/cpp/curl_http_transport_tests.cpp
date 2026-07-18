#include "duckdb_api/connector.hpp"
#include "duckdb_api/http_runtime.hpp"
#include "duckdb_api/internal/curl_http_transport.hpp"
#include "duckdb_api/scan_plan.hpp"
#include "duckdb_api/scan_request.hpp"
#include "support/loopback_curl_runtime.hpp"
#include "support/require.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using duckdb_api_test::Require;

enum class ServiceMode {
	SUCCESS,
	STATUS,
	REDIRECT,
	MALFORMED,
	OVERSIZED_HEADER,
	OVERSIZED_RESPONSE,
	DISCONNECT,
	BLOCK
};

class ManualControl final : public duckdb_api::ExecutionControl {
public:
	ManualControl() : cancelled(false) {
	}

	bool IsCancellationRequested() const noexcept override {
		return cancelled.load(std::memory_order_acquire);
	}

	void Cancel() noexcept {
		cancelled.store(true, std::memory_order_release);
	}

private:
	std::atomic<bool> cancelled;
};

class ControlledSocketService {
public:
	explicit ControlledSocketService(ServiceMode mode_p)
	    : mode(mode_p), listener(-1), client(-1), port(0), request_ready(false), stop(false), connection_count(0) {
		listener = socket(AF_INET, SOCK_STREAM, 0);
		if (listener < 0) {
			throw std::runtime_error("controlled service socket failed");
		}
		int enabled = 1;
		(void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
		sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = 0;
		if (bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
		    listen(listener, 1) != 0) {
			close(listener);
			throw std::runtime_error("controlled service bind failed");
		}
		socklen_t address_length = sizeof(address);
		if (getsockname(listener, reinterpret_cast<sockaddr *>(&address), &address_length) != 0) {
			close(listener);
			throw std::runtime_error("controlled service address failed");
		}
		port = ntohs(address.sin_port);
		worker = std::thread(&ControlledSocketService::Serve, this);
	}

	~ControlledSocketService() noexcept {
		{
			std::lock_guard<std::mutex> guard(mutex);
			stop = true;
		}
		condition.notify_all();
		if (client.load(std::memory_order_acquire) >= 0) {
			(void)shutdown(client.load(std::memory_order_relaxed), SHUT_RDWR);
		}
		(void)shutdown(listener, SHUT_RDWR);
		if (worker.joinable()) {
			worker.join();
		}
		close(listener);
	}

	uint16_t Port() const noexcept {
		return port;
	}

	bool WaitForRequest(std::chrono::milliseconds timeout) {
		std::unique_lock<std::mutex> guard(mutex);
		return condition.wait_for(guard, timeout, [&]() { return request_ready; });
	}

	std::string Request() const {
		std::lock_guard<std::mutex> guard(mutex);
		return request;
	}

	uint64_t ConnectionCount() const noexcept {
		return connection_count.load(std::memory_order_relaxed);
	}

private:
	static bool SendAll(int socket_fd, const std::string &bytes) noexcept {
		std::size_t offset = 0;
		while (offset < bytes.size()) {
			const auto sent = send(socket_fd, bytes.data() + offset, bytes.size() - offset, 0);
			if (sent <= 0) {
				return false;
			}
			offset += static_cast<std::size_t>(sent);
		}
		return true;
	}

	std::string Response() const {
		const std::string success_body =
		    "{\"items\":[{\"id\":11,\"login\":\"duckdb\",\"site_admin\":false},"
		    "{\"id\":22,\"login\":\"duckdb-fdw\",\"site_admin\":true},"
		    "{\"id\":33,\"login\":\"three\",\"site_admin\":false}]}";
		if (mode == ServiceMode::SUCCESS) {
			return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
			       std::to_string(success_body.size()) + "\r\nConnection: close\r\n\r\n" + success_body;
		}
		if (mode == ServiceMode::STATUS) {
			const std::string body = "SECRET_STATUS_BODY http://127.0.0.1/private";
			return "HTTP/1.1 503 Service Unavailable\r\nContent-Length: " + std::to_string(body.size()) +
			       "\r\nConnection: close\r\n\r\n" + body;
		}
		if (mode == ServiceMode::REDIRECT) {
			return "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:" + std::to_string(port) +
			       "/forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		}
		if (mode == ServiceMode::MALFORMED) {
			const std::string body = "{SECRET_MALFORMED";
			return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) +
			       "\r\nConnection: close\r\n\r\n" + body;
		}
		if (mode == ServiceMode::OVERSIZED_HEADER) {
			return "HTTP/1.1 200 OK\r\nX-Controlled: " + std::string(17000, 'h') +
			       "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		}
		if (mode == ServiceMode::OVERSIZED_RESPONSE) {
			return "HTTP/1.1 200 OK\r\nContent-Length: 65537\r\nConnection: close\r\n\r\n" +
			       std::string(65537, 'b');
		}
		return "";
	}

	void Serve() noexcept {
		sockaddr_in peer;
		socklen_t peer_length = sizeof(peer);
		const auto accepted = accept(listener, reinterpret_cast<sockaddr *>(&peer), &peer_length);
		if (accepted < 0) {
			return;
		}
		client.store(accepted, std::memory_order_release);
		connection_count.fetch_add(1, std::memory_order_relaxed);
		std::string received;
		char buffer[2048];
		while (received.find("\r\n\r\n") == std::string::npos && received.size() < 65536) {
			const auto count = recv(accepted, buffer, sizeof(buffer), 0);
			if (count <= 0) {
				break;
			}
			try {
				received.append(buffer, static_cast<std::size_t>(count));
			} catch (...) {
				break;
			}
		}
		{
			std::lock_guard<std::mutex> guard(mutex);
			request = received;
			request_ready = true;
		}
		condition.notify_all();
		if (mode == ServiceMode::BLOCK) {
			std::unique_lock<std::mutex> guard(mutex);
			condition.wait(guard, [&]() { return stop; });
		} else if (mode != ServiceMode::DISCONNECT) {
			(void)SendAll(accepted, Response());
		}
		(void)shutdown(accepted, SHUT_RDWR);
		close(accepted);
		client.store(-1, std::memory_order_release);
	}

	const ServiceMode mode;
	int listener;
	std::atomic<int> client;
	uint16_t port;
	mutable std::mutex mutex;
	std::condition_variable condition;
	std::thread worker;
	std::string request;
	bool request_ready;
	bool stop;
	std::atomic<uint64_t> connection_count;
};

duckdb_api::ScanRequest Request() {
	duckdb_api::ScanRequest request;
	request.connector_name = "github";
	request.relation_name = "duckdb_login_search_page";
	request.projected_columns = {"id", "login", "site_admin"};
	request.predicate = "TRUE";
	request.has_limit = false;
	request.has_offset = false;
	request.capabilities = {false, false, false, false, false, false, true, false};
	return request;
}

duckdb_api::ScanPlan Plan(const duckdb_api::CompiledConnector &connector) {
	return duckdb_api::BuildConservativeScanPlan(connector, Request());
}

void RequireError(const std::function<void()> &action, duckdb_api::ErrorStage stage,
	              const std::string &forbidden = "") {
	bool rejected = false;
	try {
		action();
	} catch (const duckdb_api::ExecutionError &error) {
		rejected = true;
		Require(error.Stage() == stage, "curl execution error stage drifted");
		Require(!error.SafeMessage().empty() && error.SafeMessage().size() <= 128,
		        "curl diagnostic was empty or unbounded");
		if (!forbidden.empty()) {
			Require(error.SafeMessage().find(forbidden) == std::string::npos,
			        "curl diagnostic exposed controlled response data or authority");
		}
	}
	Require(rejected, "expected a structured curl execution error");
}

void TestCheckedProcessInitializationAndIdentity() {
	const auto *owner_before_service_teardown = duckdb_api::internal::AcquireCurlProcessLifetime();
	std::atomic<uint64_t> initialized(0);
	std::atomic<uint64_t> rejected(0);
	std::vector<std::thread> workers;
	for (std::size_t index = 0; index < 8; index++) {
		workers.push_back(std::thread([&]() {
			try {
				const auto service = duckdb_api::InitializeHttpRuntime();
				if (service.executor && service.identity.libcurl_version == "8.7.1" &&
				    service.identity.ssl_backend == "(SecureTransport) LibreSSL/3.3.6" &&
				    service.identity.thread_safe) {
					initialized.fetch_add(1, std::memory_order_relaxed);
				} else {
					rejected.fetch_add(1, std::memory_order_relaxed);
				}
			} catch (...) {
				rejected.fetch_add(1, std::memory_order_relaxed);
			}
		}));
	}
	for (std::size_t index = 0; index < workers.size(); index++) {
		workers[index].join();
	}
	Require(initialized.load(std::memory_order_relaxed) == workers.size() &&
	            rejected.load(std::memory_order_relaxed) == 0,
	        "checked process-global HTTP initialization or identity failed");
	const auto *owner_after_service_teardown = duckdb_api::internal::AcquireCurlProcessLifetime();
	Require(owner_before_service_teardown == owner_after_service_teardown,
	        "accepted process lifetime was replaced during service teardown");
}

void TestRealCurlSuccessAndExactRequest() {
	ControlledSocketService service(ServiceMode::SUCCESS);
	const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
	ManualControl control;
	const auto plan = Plan(runtime->Connector());
	Require(plan.Operation().origin.scheme == duckdb_api::PlannedUrlScheme::HTTP &&
	            plan.Operation().origin.host == "127.0.0.1" && plan.Operation().origin.port == service.Port() &&
	            plan.Network().loopback_addresses_enabled,
	        "controlled typed origin did not reach the executor plan");
	RequireError(
	    [&]() { (void)runtime->Executor()->Open(Plan(duckdb_api::BuildNativeGithubConnector()), control); },
	    duckdb_api::ErrorStage::POLICY);
	const auto public_runtime = duckdb_api::InitializeHttpRuntime();
	RequireError([&]() { (void)public_runtime.executor->Open(plan, control); }, duckdb_api::ErrorStage::POLICY);
	Require(runtime->Observation().request_count == 0 && service.ConnectionCount() == 0,
	        "profile mismatch acquired socket authority");
	auto stream = runtime->Executor()->Open(plan, control);
	Require(runtime->Observation().request_count == 0 && service.ConnectionCount() == 0,
	        "Open performed socket work");
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.IsSchemaAligned() && batch.rows.size() == 2,
	        "real curl path did not decode the first bounded batch");
	Require(stream->Next(control, batch) && batch.rows.size() == 1 &&
	            batch.rows[0].values[0].bigint_value == 33,
	        "real curl path did not decode the second bounded batch");
	Require(!stream->Next(control, batch), "real curl path did not exhaust cleanly");
	Require(service.WaitForRequest(std::chrono::seconds(2)), "controlled service did not receive a request");
	const auto wire = service.Request();
	Require(wire.find("GET /search/users?q=duckdb+in%3Alogin&per_page=3 HTTP/1.1\r\n") == 0,
	        "curl request target or HTTP version drifted");
	Require(wire.find("\r\nHost: 127.0.0.1:" + std::to_string(service.Port()) + "\r\n") !=
	            std::string::npos,
	        "controlled curl authority drifted");
	Require(wire.find("\r\nAccept: application/vnd.github+json\r\n") != std::string::npos &&
	            wire.find("\r\nUser-Agent: duckdb-api/0.3.0\r\n") != std::string::npos &&
	            wire.find("\r\nX-GitHub-Api-Version: 2022-11-28\r\n") != std::string::npos,
	        "curl fixed request headers drifted");
	Require(wire.find("Authorization:") == std::string::npos &&
	            wire.find("Proxy-Authorization:") == std::string::npos &&
	            wire.find("Cookie:") == std::string::npos,
	        "curl emitted ambient credentials");
	const auto observation = runtime->Observation();
	Require(observation.request_count == 1 && observation.socket_policy_checks == 1 &&
	            service.ConnectionCount() == 1,
	        "curl did not perform exactly one policy-checked request attempt");
}

void TestStatusRedirectDecodeAndTransportFailures() {
	struct FailureCase {
		ServiceMode mode;
		duckdb_api::ErrorStage stage;
		const char *forbidden;
	};
	const FailureCase cases[] = {{ServiceMode::STATUS, duckdb_api::ErrorStage::HTTP_STATUS, "SECRET_STATUS_BODY"},
	                             {ServiceMode::REDIRECT, duckdb_api::ErrorStage::HTTP_STATUS, "127.0.0.1"},
	                             {ServiceMode::MALFORMED, duckdb_api::ErrorStage::DECODE, "SECRET_MALFORMED"},
	                             {ServiceMode::DISCONNECT, duckdb_api::ErrorStage::TRANSPORT, "127.0.0.1"}};
	for (std::size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		ControlledSocketService service(cases[index].mode);
		const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
		ManualControl control;
		auto stream = runtime->Executor()->Open(Plan(runtime->Connector()), control);
		duckdb_api::TypedBatch batch;
		RequireError([&]() { stream->Next(control, batch); }, cases[index].stage, cases[index].forbidden);
		Require(!stream->Next(control, batch), "failed real request was replayed");
		Require(runtime->Observation().request_count == 1 && service.ConnectionCount() == 1,
		        "real curl failure performed more than one attempt");
	}
}

void TestWireBudgetsAndDeadline() {
	const ServiceMode oversized[] = {ServiceMode::OVERSIZED_HEADER, ServiceMode::OVERSIZED_RESPONSE};
	for (std::size_t index = 0; index < sizeof(oversized) / sizeof(oversized[0]); index++) {
		ControlledSocketService service(oversized[index]);
		const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
		ManualControl control;
		auto stream = runtime->Executor()->Open(Plan(runtime->Connector()), control);
		duckdb_api::TypedBatch batch;
		RequireError([&]() { stream->Next(control, batch); }, duckdb_api::ErrorStage::RESOURCE);
		Require(runtime->Observation().request_count == 1 && service.ConnectionCount() == 1,
		        "budget failure replayed the request");
	}

	ControlledSocketService blocked(ServiceMode::BLOCK);
	const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(blocked.Port());
	ManualControl control;
	auto stream = runtime->Executor()->Open(Plan(runtime->Connector()), control);
	duckdb_api::TypedBatch batch;
	const auto started = std::chrono::steady_clock::now();
	RequireError([&]() { stream->Next(control, batch); }, duckdb_api::ErrorStage::RESOURCE);
	const auto elapsed = std::chrono::steady_clock::now() - started;
	Require(elapsed >= std::chrono::seconds(4) && elapsed < std::chrono::seconds(7),
	        "real curl deadline was not bounded by the plan");
}

void TestConcurrentCloseAndRecovery() {
	ControlledSocketService blocked(ServiceMode::BLOCK);
	const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(blocked.Port());
	ManualControl control;
	auto stream = runtime->Executor()->Open(Plan(runtime->Connector()), control);
	duckdb_api::TypedBatch batch;
	std::atomic<bool> cancelled(false);
	std::thread worker([&]() {
		try {
			stream->Next(control, batch);
		} catch (const duckdb_api::ExecutionCancelled &) {
			cancelled.store(true, std::memory_order_release);
		}
	});
	Require(blocked.WaitForRequest(std::chrono::seconds(2)), "curl request did not reach the blocking service");
	const auto started = std::chrono::steady_clock::now();
	stream->Close();
	stream->Close();
	stream->Cancel();
	worker.join();
	Require(cancelled.load(std::memory_order_acquire) &&
	            std::chrono::steady_clock::now() - started < std::chrono::seconds(6),
	        "concurrent Close did not contain the real curl transfer within its deadline");
	Require(runtime->Observation().request_count == 1, "cancelled curl request was replayed");
	Require(!stream->Next(control, batch), "closed curl stream resumed after concurrent teardown");

	ControlledSocketService recovered(ServiceMode::SUCCESS);
	const auto recovered_runtime = duckdb_api_test::BuildLoopbackCurlRuntime(recovered.Port());
	ManualControl recovered_control;
	auto recovered_stream = recovered_runtime->Executor()->Open(Plan(recovered_runtime->Connector()), recovered_control);
	Require(recovered_stream->Next(recovered_control, batch) && batch.IsSchemaAligned(),
	        "curl runtime did not recover after a cancelled transfer");
}

} // namespace

int main() {
	(void)std::signal(SIGPIPE, SIG_IGN);
	try {
		TestCheckedProcessInitializationAndIdentity();
		TestRealCurlSuccessAndExactRequest();
		TestStatusRedirectDecodeAndTransportFailures();
		TestWireBudgetsAndDeadline();
		TestConcurrentCloseAndRecovery();
		std::cout << "curl HTTP transport tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "curl HTTP transport tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
