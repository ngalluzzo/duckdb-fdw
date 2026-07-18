#include "support/controlled_socket_service.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>

namespace duckdb_api_test {
namespace {

const unsigned char GZIP_EXACT[] = {
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xed, 0xca, 0x31, 0x0e, 0x82, 0x40, 0x14, 0x40,
    0xc1, 0xab, 0x98, 0x5f, 0xd3, 0xd0, 0xee, 0x55, 0x8c, 0x31, 0xc0, 0xa2, 0xd9, 0x00, 0x52, 0x2c, 0x54, 0x84,
    0xbb, 0xa3, 0xd7, 0x30, 0x33, 0xe5, 0xcb, 0x3b, 0xa2, 0x6c, 0xe3, 0x52, 0x23, 0xdd, 0x8f, 0x28, 0x39, 0x52,
    0xdb, 0x36, 0x31, 0xaf, 0xef, 0xf2, 0x89, 0x14, 0x79, 0x1f, 0xa6, 0xdc, 0x47, 0x13, 0xf5, 0xfb, 0x3c, 0xbb,
    0xbc, 0xfc, 0xea, 0xab, 0x9b, 0xeb, 0x78, 0x3e, 0xce, 0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xff, 0xe4, 0x02, 0xd4, 0xf9, 0x4b, 0x19, 0x00, 0x00, 0x01, 0x00};

const unsigned char GZIP_OVER[] = {
    0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0xed, 0xca, 0x31, 0x0e, 0x82, 0x40, 0x14, 0x40,
    0xc1, 0xab, 0x98, 0x5f, 0xd3, 0xd0, 0xee, 0x55, 0x8c, 0x31, 0xc0, 0xa2, 0xd9, 0x00, 0x52, 0x2c, 0x54, 0x84,
    0xbb, 0xa3, 0xd7, 0x30, 0x33, 0xe5, 0xcb, 0x3b, 0xa2, 0x6c, 0xe3, 0x52, 0x23, 0xdd, 0x8f, 0x28, 0x39, 0x52,
    0xdb, 0x36, 0x31, 0xaf, 0xef, 0xf2, 0x89, 0x14, 0x79, 0x1f, 0xa6, 0xdc, 0x47, 0x13, 0xf5, 0xfb, 0x3c, 0xbb,
    0xbc, 0xfc, 0xea, 0xab, 0x9b, 0xeb, 0x78, 0x3e, 0xce, 0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x7f, 0xe5, 0x02, 0x71, 0x92, 0xcb, 0x68, 0x01, 0x00, 0x01, 0x00};

std::string Binary(const unsigned char *bytes, std::size_t size) {
	return std::string(reinterpret_cast<const char *>(bytes), size);
}

} // namespace

ControlledSocketService::ControlledSocketService(ControlledSocketMode mode_p)
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

ControlledSocketService::~ControlledSocketService() noexcept {
	{
		std::lock_guard<std::mutex> guard(mutex);
		stop = true;
	}
	condition.notify_all();
	if (client.load(std::memory_order_acquire) >= 0) {
		(void)shutdown(client.load(std::memory_order_relaxed), SHUT_RDWR);
	}
	(void)shutdown(listener, SHUT_RDWR);
	close(listener);
	listener = -1;
	if (worker.joinable()) {
		worker.join();
	}
}

uint16_t ControlledSocketService::Port() const noexcept {
	return port;
}

bool ControlledSocketService::WaitForRequest(std::chrono::milliseconds timeout) {
	std::unique_lock<std::mutex> guard(mutex);
	return condition.wait_for(guard, timeout, [&]() { return request_ready; });
}

std::string ControlledSocketService::Request() const {
	std::lock_guard<std::mutex> guard(mutex);
	return request;
}

uint64_t ControlledSocketService::ConnectionCount() const noexcept {
	return connection_count.load(std::memory_order_relaxed);
}

uint64_t ControlledSocketService::ResponseBodyBytes() const noexcept {
	if (mode == ControlledSocketMode::GZIP_EXACT_DECOMPRESSED_LIMIT) {
		return sizeof(GZIP_EXACT);
	}
	if (mode == ControlledSocketMode::GZIP_OVER_DECOMPRESSED_LIMIT) {
		return sizeof(GZIP_OVER);
	}
	return 0;
}

bool ControlledSocketService::SendAll(int socket_fd, const std::string &bytes) noexcept {
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

std::string ControlledSocketService::Response() const {
	const std::string success_body = "{\"items\":[{\"id\":11,\"login\":\"duckdb\",\"site_admin\":false},"
	                                 "{\"id\":22,\"login\":\"duckdb-fdw\",\"site_admin\":true},"
	                                 "{\"id\":33,\"login\":\"three\",\"site_admin\":false}]}";
	if (mode == ControlledSocketMode::SUCCESS) {
		return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
		       std::to_string(success_body.size()) + "\r\nConnection: close\r\n\r\n" + success_body;
	}
	if (mode == ControlledSocketMode::STATUS) {
		const std::string body = "SECRET_STATUS_BODY http://127.0.0.1/private";
		return "HTTP/1.1 503 Service Unavailable\r\nContent-Length: " + std::to_string(body.size()) +
		       "\r\nConnection: close\r\n\r\n" + body;
	}
	if (mode == ControlledSocketMode::REDIRECT) {
		return "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:" + std::to_string(port) +
		       "/forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	}
	if (mode == ControlledSocketMode::MALFORMED) {
		const std::string body = "{SECRET_MALFORMED";
		return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" +
		       body;
	}
	if (mode == ControlledSocketMode::OVERSIZED_HEADER) {
		return "HTTP/1.1 200 OK\r\nX-Controlled: " + std::string(17000, 'h') +
		       "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	}
	if (mode == ControlledSocketMode::OVERSIZED_RESPONSE) {
		return "HTTP/1.1 200 OK\r\nContent-Length: 65537\r\nConnection: close\r\n\r\n" + std::string(65537, 'b');
	}
	if (mode == ControlledSocketMode::GZIP_EXACT_DECOMPRESSED_LIMIT ||
	    mode == ControlledSocketMode::GZIP_OVER_DECOMPRESSED_LIMIT) {
		const auto body = mode == ControlledSocketMode::GZIP_EXACT_DECOMPRESSED_LIMIT
		                      ? Binary(GZIP_EXACT, sizeof(GZIP_EXACT))
		                      : Binary(GZIP_OVER, sizeof(GZIP_OVER));
		return "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Type: application/json\r\nContent-Length: " +
		       std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
	}
	return "";
}

void ControlledSocketService::Serve() noexcept {
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
	if (mode == ControlledSocketMode::BLOCK) {
		std::unique_lock<std::mutex> guard(mutex);
		condition.wait(guard, [&]() { return stop; });
	} else if (mode != ControlledSocketMode::DISCONNECT) {
		(void)SendAll(accepted, Response());
	}
	(void)shutdown(accepted, SHUT_RDWR);
	close(accepted);
	client.store(-1, std::memory_order_release);
}

} // namespace duckdb_api_test
