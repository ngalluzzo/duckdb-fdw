#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace duckdb_api_test {

enum class ControlledSocketMode {
	SUCCESS,
	STATUS,
	REDIRECT,
	MALFORMED,
	OVERSIZED_HEADER,
	OVERSIZED_RESPONSE,
	GZIP_EXACT_DECOMPRESSED_LIMIT,
	GZIP_OVER_DECOMPRESSED_LIMIT,
	DISCONNECT,
	BLOCK
};

// One-connection private HTTP/1.1 service. The service owns no product
// authority and is linked only into focused Runtime tests.
class ControlledSocketService {
public:
	explicit ControlledSocketService(ControlledSocketMode mode);
	~ControlledSocketService() noexcept;

	uint16_t Port() const noexcept;
	bool WaitForRequest(std::chrono::milliseconds timeout);
	std::string Request() const;
	uint64_t ConnectionCount() const noexcept;
	uint64_t ResponseBodyBytes() const noexcept;

private:
	ControlledSocketService(const ControlledSocketService &) = delete;
	ControlledSocketService &operator=(const ControlledSocketService &) = delete;

	static bool SendAll(int socket_fd, const std::string &bytes) noexcept;
	std::string Response() const;
	void Serve() noexcept;

	const ControlledSocketMode mode;
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

} // namespace duckdb_api_test
