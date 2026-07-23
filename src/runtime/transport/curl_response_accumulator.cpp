#include "duckdb_api/internal/runtime/transport/curl_response_accumulator.hpp"

#include <sys/socket.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace duckdb_api {
namespace internal {
namespace {

bool TryAddStringBytes(const std::string &value, uint64_t limit, uint64_t &result) noexcept {
	const auto object_begin = reinterpret_cast<std::uintptr_t>(&value);
	const auto object_end = object_begin + sizeof(value);
	const auto data = reinterpret_cast<std::uintptr_t>(value.data());
	if (data >= object_begin && data < object_end) {
		return true;
	}
	const auto allocation = static_cast<uint64_t>(value.capacity()) + 1;
	if (result > limit || allocation > limit - result) {
		return false;
	}
	result += allocation;
	return true;
}

bool TryRetainedMetadataBytes(const CurlTransferState &state, uint64_t limit, uint64_t &result) noexcept {
	const auto &values = state.link_field_values;
	if (values.capacity() > limit / sizeof(std::string)) {
		return false;
	}
	result = static_cast<uint64_t>(values.capacity()) * sizeof(std::string);
	for (const auto &value : values) {
		if (!TryAddStringBytes(value, limit, result)) {
			return false;
		}
	}
	if (state.rate_limit_fields.capacity() > (limit - result) / sizeof(HttpObservedHeader)) {
		return false;
	}
	result += static_cast<uint64_t>(state.rate_limit_fields.capacity()) * sizeof(HttpObservedHeader);
	for (const auto &field : state.rate_limit_fields) {
		if (!TryAddStringBytes(field.name, limit, result) || !TryAddStringBytes(field.value, limit, result)) {
			return false;
		}
	}
	if (state.date_field_values.capacity() > (limit - result) / sizeof(std::string)) {
		return false;
	}
	result += static_cast<uint64_t>(state.date_field_values.capacity()) * sizeof(std::string);
	for (const auto &value : state.date_field_values) {
		if (!TryAddStringBytes(value, limit, result)) {
			return false;
		}
	}
	return result <= limit;
}

bool AddWithin(uint64_t current, std::size_t amount, uint64_t limit, uint64_t &result) noexcept {
	if (current > limit || static_cast<uint64_t>(amount) > limit - current) {
		return false;
	}
	result = current + static_cast<uint64_t>(amount);
	return true;
}

char AsciiLower(char value) noexcept {
	return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

bool IsStatusLine(const char *data, std::size_t length) noexcept {
	return length >= 5 && AsciiLower(data[0]) == 'h' && AsciiLower(data[1]) == 't' && AsciiLower(data[2]) == 't' &&
	       AsciiLower(data[3]) == 'p' && data[4] == '/';
}

bool IsHeaderSectionEnd(const char *data, std::size_t length) noexcept {
	return (length == 2 && data[0] == '\r' && data[1] == '\n') || (length == 1 && data[0] == '\n');
}

bool IsNamedField(const char *data, std::size_t length, const char *name, std::size_t &value_offset) noexcept {
	std::size_t colon = 0;
	while (colon < length && data[colon] != ':' && data[colon] != '\r' && data[colon] != '\n') {
		colon++;
	}
	std::size_t name_length = 0;
	while (name[name_length] != '\0') {
		name_length++;
	}
	if (colon != name_length || colon >= length || data[colon] != ':') {
		return false;
	}
	for (std::size_t index = 0; index < colon; index++) {
		if (AsciiLower(data[index]) != name[index]) {
			return false;
		}
	}
	value_offset = colon + 1;
	return true;
}

bool EqualsFieldValue(const char *data, std::size_t begin, std::size_t end, const char *expected) noexcept {
	while (begin < end && (data[begin] == ' ' || data[begin] == '\t')) {
		begin++;
	}
	while (end > begin &&
	       (data[end - 1] == '\r' || data[end - 1] == '\n' || data[end - 1] == ' ' || data[end - 1] == '\t')) {
		end--;
	}
	std::size_t expected_length = 0;
	while (expected[expected_length] != '\0') {
		expected_length++;
	}
	if (end - begin != expected_length) {
		return false;
	}
	for (std::size_t index = 0; index < expected_length; index++) {
		if (AsciiLower(data[begin + index]) != AsciiLower(expected[index])) {
			return false;
		}
	}
	return true;
}

} // namespace

CurlTransferState::CurlTransferState(ExecutionControl &control_p, const HttpLimits &limits_p,
                                     const CurlTransferProfile &profile_p)
    : control(control_p), limits(limits_p), profile(profile_p), header_bytes(0), response_bytes(0), cancelled(false),
      timed_out(false), header_oversized(false), response_oversized(false), decompressed_oversized(false),
      metadata_oversized(false), body_allocation_failed(false), metadata_allocation_failed(false),
      address_denied(false), socket_attempted(false), metadata_bytes(0), header_section_complete(false),
      retry_after_present(false), transfer_encoding_seen(false), transfer_chunked(false),
      transfer_encoding_unsupported(false), content_encoded(false), retained_header_kind(RetainedHeaderKind::NONE),
      retained_header_index(0), retained_link_index(0), easy_handle(nullptr) {
}

bool CurlTransferState::ShouldContinue() noexcept {
	if (control.IsCancellationRequested()) {
		cancelled = true;
		return false;
	}
	if (std::chrono::steady_clock::now() >= limits.deadline) {
		timed_out = true;
		return false;
	}
	return true;
}

void ReleaseCurlLinkMetadata(CurlTransferState &state) noexcept {
	std::vector<std::string>().swap(state.link_field_values);
	uint64_t retained = 0;
	state.metadata_bytes = TryRetainedMetadataBytes(state, state.limits.max_metadata_bytes, retained) ? retained : 0;
}

void ReleaseAllCurlMetadata(CurlTransferState &state) noexcept {
	std::vector<std::string>().swap(state.link_field_values);
	std::vector<HttpObservedHeader>().swap(state.rate_limit_fields);
	std::vector<std::string>().swap(state.date_field_values);
	state.metadata_bytes = 0;
}

std::size_t WriteCurlBody(char *data, std::size_t size, std::size_t count, void *opaque) noexcept {
	auto &state = *static_cast<CurlTransferState *>(opaque);
	if (state.response_oversized) {
		return 0;
	}
	if (!state.ShouldContinue()) {
		return 0;
	}
	if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
		state.decompressed_oversized = true;
		return 0;
	}
	const auto length = size * count;
	uint64_t updated_size = 0;
	const auto body_limit =
	    state.transfer_chunked ? state.limits.max_response_bytes : state.limits.max_decompressed_bytes;
	if (!AddWithin(static_cast<uint64_t>(state.body.size()), length, body_limit, updated_size)) {
		if (state.transfer_chunked) {
			state.response_oversized = true;
		} else {
			state.decompressed_oversized = true;
		}
		return 0;
	}
	(void)updated_size;
	try {
		state.body.append(data, length);
	} catch (...) {
		state.body_allocation_failed = true;
		return 0;
	}
#ifdef DUCKDB_API_PRIVATE_CURL_TESTS
	if (state.profile.body_observer) {
		state.profile.body_observer(state.profile.body_observer_context);
	}
#endif
	return length;
}

std::size_t ReadCurlHeader(char *data, std::size_t size, std::size_t count, void *opaque) noexcept {
	auto &state = *static_cast<CurlTransferState *>(opaque);
	if (!state.ShouldContinue()) {
		return 0;
	}
	if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
		state.header_oversized = true;
		return 0;
	}
	const auto length = size * count;
	uint64_t updated_size = 0;
	if (!AddWithin(state.header_bytes, length, state.limits.max_header_bytes, updated_size)) {
		state.header_oversized = true;
		return 0;
	}
	state.header_bytes = updated_size;
	// A new HTTP status line begins a new response header section. Resetting the
	// candidate values here ensures 1xx/interim metadata cannot influence the
	// terminal response. Redirect following remains disabled independently.
	if (IsStatusLine(data, length)) {
		ReleaseAllCurlMetadata(state);
		state.header_section_complete = false;
		state.retry_after_present = false;
		state.transfer_encoding_seen = false;
		state.transfer_chunked = false;
		state.transfer_encoding_unsupported = false;
		state.content_encoded = false;
		state.retained_header_kind = CurlTransferState::RetainedHeaderKind::NONE;
		state.retained_header_index = 0;
		state.retained_link_index = 0;
		return length;
	}
	if (IsHeaderSectionEnd(data, length)) {
		state.header_section_complete = true;
		state.retained_header_kind = CurlTransferState::RetainedHeaderKind::NONE;
		state.retained_header_index = 0;
		state.retained_link_index = 0;
		return length;
	}
	// libcurl sends HTTP trailers through the header callback after the blank
	// line that ended the terminal response header section. Trailers are still
	// charged to the header budget, but they cannot grant continuation authority.
	if (state.header_section_complete) {
		return length;
	}
	if (length != 0 && (data[0] == ' ' || data[0] == '\t')) {
		if (state.retained_header_kind == CurlTransferState::RetainedHeaderKind::NONE) {
			return length;
		}
		std::size_t value_end = length;
		while (value_end > 0 && (data[value_end - 1] == '\r' || data[value_end - 1] == '\n')) {
			value_end--;
		}
		try {
			if (state.retained_header_kind == CurlTransferState::RetainedHeaderKind::RATE_LIMIT ||
			    state.retained_header_kind == CurlTransferState::RetainedHeaderKind::RATE_LIMIT_LINK) {
				state.rate_limit_fields.at(state.retained_header_index).value.append(data, value_end);
			} else if (state.retained_header_kind == CurlTransferState::RetainedHeaderKind::DATE) {
				state.date_field_values.at(state.retained_header_index).append(data, value_end);
			}
			if (state.retained_header_kind == CurlTransferState::RetainedHeaderKind::LINK) {
				state.link_field_values.at(state.retained_header_index).append(data, value_end);
			} else if (state.retained_header_kind == CurlTransferState::RetainedHeaderKind::RATE_LIMIT_LINK) {
				state.link_field_values.at(state.retained_link_index).append(data, value_end);
			}
			uint64_t retained = 0;
			if (!TryRetainedMetadataBytes(state, state.limits.max_metadata_bytes, retained)) {
				state.metadata_oversized = true;
				ReleaseAllCurlMetadata(state);
				return 0;
			}
			state.metadata_bytes = retained;
		} catch (...) {
			state.metadata_allocation_failed = true;
			ReleaseAllCurlMetadata(state);
			return 0;
		}
		return length;
	}
	state.retained_header_kind = CurlTransferState::RetainedHeaderKind::NONE;
	state.retained_header_index = 0;
	state.retained_link_index = 0;
	std::size_t value_offset = 0;
	bool transport_field = false;
	if (IsNamedField(data, length, "transfer-encoding", value_offset)) {
		if (state.transfer_encoding_seen) {
			state.transfer_encoding_unsupported = true;
			return 0;
		}
		state.transfer_encoding_seen = true;
		state.transfer_chunked = EqualsFieldValue(data, value_offset, length, "chunked");
		state.transfer_encoding_unsupported = !state.transfer_chunked;
		if (state.transfer_encoding_unsupported) {
			return 0;
		}
		transport_field = true;
	}
	if (IsNamedField(data, length, "content-encoding", value_offset)) {
		state.content_encoded = !EqualsFieldValue(data, value_offset, length, "identity");
		transport_field = true;
	}
	if (IsNamedField(data, length, "retry-after", value_offset)) {
		state.retry_after_present = true;
	}
	const std::string *retained_name = nullptr;
	for (const auto &name : state.limits.retained_header_names) {
		if (IsNamedField(data, length, name.c_str(), value_offset)) {
			retained_name = &name;
			break;
		}
	}
	const bool retain_date = state.limits.retain_date && IsNamedField(data, length, "date", value_offset);
	const bool retain_link = IsNamedField(data, length, "link", value_offset);
	if (retained_name != nullptr || retain_date) {
		if (state.limits.max_metadata_bytes == 0) {
			state.metadata_oversized = true;
			return 0;
		}
		while (value_offset < length && (data[value_offset] == ' ' || data[value_offset] == '\t')) {
			value_offset++;
		}
		std::size_t value_end = length;
		while (value_end > value_offset && (data[value_end - 1] == '\r' || data[value_end - 1] == '\n' ||
		                                    data[value_end - 1] == ' ' || data[value_end - 1] == '\t')) {
			value_end--;
		}
		try {
			const std::string value(data + value_offset, value_end - value_offset);
			if (retained_name != nullptr) {
				state.rate_limit_fields.push_back({*retained_name, value});
				state.retained_header_index = state.rate_limit_fields.size() - 1;
				state.retained_header_kind = retain_link ? CurlTransferState::RetainedHeaderKind::RATE_LIMIT_LINK
				                                         : CurlTransferState::RetainedHeaderKind::RATE_LIMIT;
			} else {
				state.date_field_values.push_back(value);
				state.retained_header_kind = CurlTransferState::RetainedHeaderKind::DATE;
				state.retained_header_index = state.date_field_values.size() - 1;
			}
			if (retain_link) {
				state.link_field_values.push_back(value);
				state.retained_link_index = state.link_field_values.size() - 1;
			}
			uint64_t retained = 0;
			if (!TryRetainedMetadataBytes(state, state.limits.max_metadata_bytes, retained)) {
				state.metadata_oversized = true;
				ReleaseAllCurlMetadata(state);
				return 0;
			}
			state.metadata_bytes = retained;
		} catch (...) {
			state.metadata_allocation_failed = true;
			ReleaseAllCurlMetadata(state);
			return 0;
		}
		return length;
	}
	if (transport_field || !retain_link) {
		return length;
	}
	if (state.limits.max_metadata_bytes == 0) {
		return length;
	}
	while (value_offset < length && (data[value_offset] == ' ' || data[value_offset] == '\t')) {
		value_offset++;
	}
	std::size_t value_end = length;
	while (value_end > value_offset && (data[value_end - 1] == '\r' || data[value_end - 1] == '\n' ||
	                                    data[value_end - 1] == ' ' || data[value_end - 1] == '\t')) {
		value_end--;
	}
	const auto value_length = value_end - value_offset;
	if (value_length > state.limits.max_metadata_bytes) {
		state.metadata_oversized = true;
		return 0;
	}
	try {
		state.link_field_values.push_back(std::string(data + value_offset, value_length));
		state.retained_header_kind = CurlTransferState::RetainedHeaderKind::LINK;
		state.retained_header_index = state.link_field_values.size() - 1;
		uint64_t retained = 0;
		if (!TryRetainedMetadataBytes(state, state.limits.max_metadata_bytes, retained)) {
			state.metadata_oversized = true;
			ReleaseCurlLinkMetadata(state);
			return 0;
		}
		state.metadata_bytes = retained;
	} catch (...) {
		state.metadata_allocation_failed = true;
		ReleaseCurlLinkMetadata(state);
		return 0;
	}
	return length;
}

int CountIncomingCurlProtocolData(CURL *handle, curl_infotype type, char *, std::size_t length, void *opaque) noexcept {
	auto &state = *static_cast<CurlTransferState *>(opaque);
	// DATA_IN observes the body before content decoding. With HTTP transfer
	// decoding disabled, the write callback separately bounds raw chunk framing
	// and replaces this count with its exact buffered size. Count only our easy
	// handle and never inspect, retain, or log potentially sensitive bytes.
	if (handle != state.easy_handle || type != CURLINFO_DATA_IN) {
		return 0;
	}
	uint64_t updated_size = 0;
	if (!AddWithin(state.response_bytes, length, state.limits.max_response_bytes, updated_size)) {
		state.response_oversized = true;
		return 0;
	}
	state.response_bytes = updated_size;
	return 0;
}

int ObserveCurlTransferProgress(void *opaque, curl_off_t total, curl_off_t current, curl_off_t, curl_off_t) noexcept {
	auto &state = *static_cast<CurlTransferState *>(opaque);
	if (state.response_oversized) {
		return 1;
	}
	if (total > 0 && static_cast<uint64_t>(total) > state.limits.max_response_bytes) {
		state.response_oversized = true;
		return 1;
	}
	if (current > 0 && static_cast<uint64_t>(current) > state.limits.max_response_bytes) {
		state.response_oversized = true;
		return 1;
	}
	return state.ShouldContinue() ? 0 : 1;
}

curl_socket_t OpenCurlPolicySocket(void *opaque, curlsocktype purpose, struct curl_sockaddr *address) noexcept {
	auto &state = *static_cast<CurlTransferState *>(opaque);
	// libcurl may otherwise try multiple DNS answers (including Happy Eyeballs)
	// within one easy transfer. The preview permits one connection attempt, so
	// a second socket is unavailable even when its address would be allowed.
	if (state.socket_attempted) {
		return CURL_SOCKET_BAD;
	}
	if (state.address_denied || purpose != CURLSOCKTYPE_IPCXN || !address || !state.profile.socket_policy ||
	    !state.profile.socket_policy(&address->addr, address->addrlen, state.profile.socket_policy_context)) {
		state.address_denied = true;
		return CURL_SOCKET_BAD;
	}
	state.socket_attempted = true;
	return socket(address->family, address->socktype, address->protocol);
}

} // namespace internal
} // namespace duckdb_api
