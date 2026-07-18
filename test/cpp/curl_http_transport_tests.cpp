#include "duckdb_api/http_runtime.hpp"
#include "support/controlled_socket_service.hpp"
#include "support/loopback_curl_runtime.hpp"
#include "support/private_curl_probe.hpp"
#include "support/require.hpp"
#include "support/runtime_http_test_support.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using duckdb_api_test::ControlledSocketMode;
using duckdb_api_test::ControlledSocketService;
using duckdb_api_test::ManualControl;
using duckdb_api_test::Require;
using duckdb_api_test::RequireExecutionError;

class ScopedEnvironment {
public:
	~ScopedEnvironment() noexcept {
		for (std::vector<Entry>::reverse_iterator entry = entries.rbegin(); entry != entries.rend(); ++entry) {
			if (entry->existed) {
				(void)setenv(entry->name.c_str(), entry->value.c_str(), 1);
			} else {
				(void)unsetenv(entry->name.c_str());
			}
		}
	}

	void Set(const std::string &name, const std::string &value) {
		const auto *previous = getenv(name.c_str());
		entries.push_back({name, previous != nullptr, previous ? previous : ""});
		if (setenv(name.c_str(), value.c_str(), 1) != 0) {
			throw std::runtime_error("could not set controlled curl environment");
		}
	}

private:
	struct Entry {
		std::string name;
		bool existed;
		std::string value;
	};
	std::vector<Entry> entries;
};

class ControlledHome {
public:
	ControlledHome() {
		char pattern[] = "/tmp/duckdb-api-curl-home.XXXXXX";
		const auto *created = mkdtemp(pattern);
		if (!created) {
			throw std::runtime_error("could not create controlled curl home");
		}
		path = created;
		std::ofstream netrc(path + "/.netrc");
		netrc << "machine 127.0.0.1 login AMBIENT_USER password AMBIENT_NETRC_SECRET\n";
		netrc.close();
		if (!netrc || chmod((path + "/.netrc").c_str(), 0600) != 0) {
			throw std::runtime_error("could not create hostile netrc fixture");
		}
	}

	~ControlledHome() noexcept {
		(void)unlink((path + "/.netrc").c_str());
		(void)rmdir(path.c_str());
	}

	const std::string &Path() const noexcept {
		return path;
	}

private:
	std::string path;
};

void TestRealCurlSuccessAndExactRequestUnderHostileEnvironment() {
	ControlledHome home;
	ScopedEnvironment environment;
	environment.Set("HOME", home.Path());
	environment.Set("CURL_HOME", home.Path());
	environment.Set("http_proxy", "http://AMBIENT_PROXY_SECRET@127.0.0.1:1");
	environment.Set("HTTP_PROXY", "http://AMBIENT_PROXY_SECRET@127.0.0.1:1");
	environment.Set("https_proxy", "http://AMBIENT_PROXY_SECRET@127.0.0.1:1");
	environment.Set("HTTPS_PROXY", "http://AMBIENT_PROXY_SECRET@127.0.0.1:1");
	environment.Set("all_proxy", "http://AMBIENT_PROXY_SECRET@127.0.0.1:1");
	environment.Set("ALL_PROXY", "http://AMBIENT_PROXY_SECRET@127.0.0.1:1");
	environment.Set("no_proxy", "");
	environment.Set("NO_PROXY", "");
	environment.Set("HTTP_COOKIE", "AMBIENT_COOKIE_SECRET=1");
	environment.Set("CURL_USERPWD", "AMBIENT_USERPWD_SECRET");

	ControlledSocketService service(ControlledSocketMode::SUCCESS);
	const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
	ManualControl control;
	const auto plan = duckdb_api_test::BuildRuntimePlan(runtime->Connector());
	Require(plan.Operation().origin.scheme == duckdb_api::PlannedUrlScheme::HTTP &&
	            plan.Operation().origin.host == "127.0.0.1" && plan.Operation().origin.port == service.Port() &&
	            plan.Network().loopback_addresses_enabled,
	        "controlled typed origin did not reach the executor plan");
	RequireExecutionError(
	    [&]() {
		    (void)runtime->Executor()->Open(duckdb_api_test::BuildRuntimePlan(duckdb_api::BuildNativeGithubConnector()),
		                                    control);
	    },
	    duckdb_api::ErrorStage::POLICY);
	const auto public_runtime = duckdb_api::InitializeHttpRuntime();
	RequireExecutionError([&]() { (void)public_runtime.executor->Open(plan, control); },
	                      duckdb_api::ErrorStage::POLICY);
	Require(runtime->Observation().request_count == 0 && service.ConnectionCount() == 0,
	        "profile mismatch acquired socket authority");

	auto stream = runtime->Executor()->Open(plan, control);
	Require(runtime->Observation().request_count == 0 && service.ConnectionCount() == 0, "Open performed socket work");
	duckdb_api::TypedBatch batch;
	Require(stream->Next(control, batch) && batch.IsSchemaAligned() && batch.rows.size() == 2,
	        "real curl path did not decode the first bounded batch");
	Require(stream->Next(control, batch) && batch.rows.size() == 1 && batch.rows[0].values[0].bigint_value == 33,
	        "real curl path did not decode the second bounded batch");
	Require(!stream->Next(control, batch), "real curl path did not exhaust cleanly");
	Require(service.WaitForRequest(std::chrono::seconds(2)), "controlled service did not receive a request");
	const auto wire = service.Request();
	Require(wire.find("GET /search/users?q=duckdb+in%3Alogin&per_page=3 HTTP/1.1\r\n") == 0,
	        "curl request target or HTTP version drifted");
	Require(wire.find("\r\nHost: 127.0.0.1:" + std::to_string(service.Port()) + "\r\n") != std::string::npos,
	        "controlled curl authority drifted");
	Require(wire.find("\r\nAccept: application/vnd.github+json\r\n") != std::string::npos &&
	            wire.find("\r\nUser-Agent: duckdb-api/0.3.0\r\n") != std::string::npos &&
	            wire.find("\r\nX-GitHub-Api-Version: 2022-11-28\r\n") != std::string::npos,
	        "curl fixed request headers drifted");
	Require(wire.find("Authorization:") == std::string::npos &&
	            wire.find("Proxy-Authorization:") == std::string::npos && wire.find("Cookie:") == std::string::npos &&
	            wire.find("AMBIENT_") == std::string::npos,
	        "curl emitted ambient credentials or state");
	const auto observation = runtime->Observation();
	Require(observation.request_count == 1 && observation.socket_policy_checks == 1 && service.ConnectionCount() == 1,
	        "curl did not perform exactly one policy-checked request attempt");
}

void TestStatusRedirectAndTransportFailures() {
	struct FailureCase {
		ControlledSocketMode mode;
		duckdb_api::ErrorStage stage;
		const char *forbidden;
	};
	const FailureCase cases[] = {
	    {ControlledSocketMode::STATUS, duckdb_api::ErrorStage::HTTP_STATUS, "SECRET_STATUS_BODY"},
	    {ControlledSocketMode::REDIRECT, duckdb_api::ErrorStage::HTTP_STATUS, "127.0.0.1"},
	    {ControlledSocketMode::DISCONNECT, duckdb_api::ErrorStage::TRANSPORT, "127.0.0.1"}};
	for (std::size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		ControlledSocketService service(cases[index].mode);
		const auto runtime = duckdb_api_test::BuildLoopbackCurlRuntime(service.Port());
		ManualControl control;
		auto stream = runtime->Executor()->Open(duckdb_api_test::BuildRuntimePlan(runtime->Connector()), control);
		duckdb_api::TypedBatch batch;
		RequireExecutionError([&]() { stream->Next(control, batch); }, cases[index].stage, cases[index].forbidden);
		Require(!stream->Next(control, batch), "failed real request was replayed");
		Require(runtime->Observation().request_count == 1 && service.ConnectionCount() == 1,
		        "real curl failure performed more than one attempt");
	}
}

void TestDeniedAddressCallbackPreventsConnection() {
	ControlledSocketService service(ControlledSocketMode::SUCCESS);
	ManualControl control;
	uint64_t policy_checks = 0;
	const duckdb_api_test::PrivateCurlProbeOptions options {"http://127.0.0.1:" + std::to_string(service.Port()) +
	                                                            "/search/users?q=duckdb+in%3Alogin&per_page=3",
	                                                        "http",
	                                                        "http",
	                                                        "127.0.0.1",
	                                                        service.Port(),
	                                                        "",
	                                                        "",
	                                                        duckdb_api_test::PrivateCurlSocketPolicy::DENY_ALL,
	                                                        1000,
	                                                        &policy_checks};
	RequireExecutionError([&]() { (void)duckdb_api_test::PerformPrivateCurlProbe(options, control); },
	                      duckdb_api::ErrorStage::POLICY);
	Require(policy_checks == 1 && service.ConnectionCount() == 0,
	        "denied resolved address reached the controlled service");
}

void TestOneSocketAcrossMultipleResolvedAddresses() {
	ControlledSocketService service(ControlledSocketMode::SUCCESS);
	ManualControl control;
	uint64_t policy_checks = 0;
	const auto port = std::to_string(service.Port());
	const duckdb_api_test::PrivateCurlProbeOptions options {
	    "http://multi.test:" + port + "/search/users?q=duckdb+in%3Alogin&per_page=3",
	    "http",
	    "http",
	    "multi.test",
	    service.Port(),
	    "",
	    "multi.test:" + port + ":[::1],127.0.0.1",
	    duckdb_api_test::PrivateCurlSocketPolicy::ALLOW_LOOPBACK_PORT,
	    1000,
	    &policy_checks};
	RequireExecutionError([&]() { (void)duckdb_api_test::PerformPrivateCurlProbe(options, control); },
	                      duckdb_api::ErrorStage::TRANSPORT);
	Require(policy_checks == 1 && service.ConnectionCount() == 0,
	        "multi-answer transfer opened a second socket after the first connection attempt");
}

} // namespace

int main() {
	(void)std::signal(SIGPIPE, SIG_IGN);
	try {
		TestRealCurlSuccessAndExactRequestUnderHostileEnvironment();
		TestStatusRedirectAndTransportFailures();
		TestDeniedAddressCallbackPreventsConnection();
		TestOneSocketAcrossMultipleResolvedAddresses();
		std::cout << "curl HTTP request and policy tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "curl HTTP request and policy tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
