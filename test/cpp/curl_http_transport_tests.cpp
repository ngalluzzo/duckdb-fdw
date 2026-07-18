#include "duckdb_api/http_runtime.hpp"
#include "support/require.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using duckdb_api_test::Require;

void TestCheckedProcessInitializationAndIdentity() {
	std::atomic<uint64_t> initialized(0);
	std::atomic<uint64_t> rejected(0);
	std::vector<std::thread> workers;
	for (std::size_t index = 0; index < 8; index++) {
		workers.push_back(std::thread([&]() {
			try {
				const auto service = duckdb_api::InitializeHttpRuntime();
				if (service.executor && service.identity.libcurl_version == "8.7.1" &&
				    service.identity.ssl_backend == "SecureTransport (LibreSSL/3.3.6)" &&
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
}

void TestOpenRemainsOffline() {
	// The executor is constructed here, but a request remains impossible without
	// an immutable ScanPlan and a subsequent stream pull. Live compatibility is
	// exercised by the product gate rather than this deterministic unit oracle.
	const auto service = duckdb_api::InitializeHttpRuntime();
	Require(service.executor != nullptr, "HTTP runtime did not return its public executor service");
}

} // namespace

int main() {
	try {
		TestCheckedProcessInitializationAndIdentity();
		TestOpenRemainsOffline();
		std::cout << "curl HTTP transport tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "curl HTTP transport tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
