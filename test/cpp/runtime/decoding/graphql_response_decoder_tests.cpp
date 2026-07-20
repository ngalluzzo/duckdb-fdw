#include "duckdb_api/internal/runtime/decoding/graphql_response_decoder.hpp"
#include "duckdb_api/internal/runtime/execution/graphql_plan_admission.hpp"
#include "duckdb_api/internal/runtime/execution/http_scan_executor.hpp"
#include "semantics/support/graphql_scan_plan_test_fixtures.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

const char *const APPLICATION_ERROR_MESSAGE = "remote protocol response reported application errors";

class NeverCancelled final : public duckdb_api::ExecutionControl {
public:
	bool IsCancellationRequested() const noexcept override {
		return false;
	}
};

void Require(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

std::unique_ptr<const duckdb_api::internal::AdmittedGraphqlRequestProfile> Profile() {
	const auto plan = duckdb_api_test::BuildValidGraphqlScanPlanFixture("decoder_secret");
	const duckdb_api::internal::HttpExecutionProfile host {
	    duckdb_api::PlannedUrlScheme::HTTPS, "api.github.com", 443, false, false, false, 30000, 100};
	return duckdb_api::internal::TryAdmitGraphqlPlan(plan, host);
}

duckdb_api::internal::GraphqlDecodeLimits Limits() {
	return {100, 512, 16, 2 * 1024 * 1024, std::chrono::steady_clock::now() + std::chrono::seconds(30)};
}

std::string Node(const std::string &id, const std::string &stars = "1", const std::string &language = "null") {
	return "{\"id\":\"" + id + "\",\"nameWithOwner\":\"o/r\",\"owner\":{\"login\":\"o\"}," +
	       "\"stargazerCount\":" + stars + ",\"primaryLanguage\":" + language +
	       ",\"isPrivate\":false,\"isArchived\":false,\"updatedAt\":\"2026-01-01T00:00:00Z\"}";
}

std::string Envelope(const std::string &nodes, bool has_next = false, const std::string &cursor = "null",
                     const std::string &extra = "") {
	return "{" + extra + "\"data\":{\"viewer\":{\"repositories\":{\"nodes\":[" + nodes +
	       "],\"pageInfo\":{\"hasNextPage\":" + (has_next ? "true" : "false") + ",\"endCursor\":" + cursor +
	       "}}}},\"errors\":[]}";
}

void RequireFailure(const std::string &body, duckdb_api::ErrorStage stage, const std::string &field) {
	auto profile = Profile();
	NeverCancelled control;
	try {
		(void)duckdb_api::internal::DecodeGraphqlResponse(body, *profile, Limits(), control);
		throw std::runtime_error("invalid GraphQL response must fail");
	} catch (const duckdb_api::ExecutionError &error) {
		if (error.Stage() != stage || (!field.empty() && error.Field() != field)) {
			throw std::runtime_error("GraphQL response expected field '" + field + "' but received '" + error.Field() +
			                         "'");
		}
	}
}

void TestEightColumnsAndNullableLanguage() {
	auto profile = Profile();
	Require(static_cast<bool>(profile), "decoder test profile must be admitted");
	NeverCancelled control;
	const std::string body =
	    "{\"data\":{\"viewer\":{\"repositories\":{\"nodes\":["
	    "{\"id\":\"R1\",\"nameWithOwner\":\"o/one\",\"owner\":{\"login\":\"o\"},"
	    "\"stargazerCount\":7,\"primaryLanguage\":{\"name\":\"\\uD83D\\uDE00\"},\"isPrivate\":false,"
	    "\"isArchived\":true,\"updatedAt\":\"2026-01-01T00:00:00Z\"},"
	    "{\"id\":\"R2\",\"nameWithOwner\":\"o/two\",\"owner\":{\"login\":\"o\"},"
	    "\"stargazerCount\":0,\"primaryLanguage\":null,\"isPrivate\":true,"
	    "\"isArchived\":false,\"updatedAt\":\"2026-02-01T00:00:00Z\"}],"
	    "\"pageInfo\":{\"hasNextPage\":true,\"endCursor\":\"cursor-2\"}}}},\"errors\":[]}";
	auto decoded = duckdb_api::internal::DecodeGraphqlResponse(body, *profile, Limits(), control);
	Require(decoded.rows.size() == 2 && decoded.rows[0].values.size() == 8 && decoded.has_next &&
	            decoded.end_cursor == "cursor-2",
	        "decoder must produce the admitted schema and cursor metadata");
	Require(decoded.rows[0].values[3].bigint_value == 7 && decoded.rows[0].values[4].valid &&
	            decoded.rows[0].values[4].varchar_value == "\xF0\x9F\x98\x80" &&
	            decoded.rows[0].values[4].varchar_value.size() == 4 && !decoded.rows[1].values[4].valid &&
	            decoded.rows[1].values[4].kind == duckdb_api::ValueKind::VARCHAR,
	        "decoder must preserve strict scalars and nullable language");
}

void TestErrorsPrecedeDataWithoutLeakingRemoteValues() {
	auto profile = Profile();
	NeverCancelled control;
	const std::string data_canary = "remote-secret-data";
	const std::string error_canary = "remote-secret-error";
	const std::string path_canary = "remote-secret-path";
	const std::string body = "{\"data\":{\"private\":\"" + data_canary + "\"},\"errors\":[{\"message\":\"" +
	                         error_canary + "\",\"path\":[\"" + path_canary + "\"]}]}";
	try {
		(void)duckdb_api::internal::DecodeGraphqlResponse(body, *profile, Limits(), control);
		throw std::runtime_error("nonempty errors must fail");
	} catch (const duckdb_api::ExecutionError &error) {
		Require(error.Stage() == duckdb_api::ErrorStage::REMOTE_PROTOCOL && error.Field() == "errors" &&
		            error.SafeMessage() == APPLICATION_ERROR_MESSAGE &&
		            error.SafeMessage().find(data_canary) == std::string::npos &&
		            error.SafeMessage().find(error_canary) == std::string::npos &&
		            error.SafeMessage().find(path_canary) == std::string::npos,
		        "remote errors must win with the exact redacted protocol failure");
	}
	const std::string long_unknown_key(65, 'k');
	const std::string long_key_body = "{\"" + long_unknown_key + "\":\"ignored\",\"errors\":[{}],\"data\":null}";
	try {
		(void)duckdb_api::internal::DecodeGraphqlResponse(long_key_body, *profile, Limits(), control);
		throw std::runtime_error("errors after an overlong unknown key must fail");
	} catch (const duckdb_api::ExecutionError &error) {
		Require(error.Stage() == duckdb_api::ErrorStage::REMOTE_PROTOCOL && error.Field() == "errors" &&
		            error.SafeMessage() == APPLICATION_ERROR_MESSAGE,
		        "allocation-free unknown-key skipping changed GraphQL errors precedence");
	}
}

void TestMalformedDocumentPrecedesRemoteProtocol() {
	auto profile = Profile();
	NeverCancelled control;
	try {
		(void)duckdb_api::internal::DecodeGraphqlResponse("{\"errors\":[{}],\"later\":[}", *profile, Limits(), control);
		throw std::runtime_error("malformed document must fail");
	} catch (const duckdb_api::ExecutionError &error) {
		Require(error.Stage() == duckdb_api::ErrorStage::DECODE,
		        "complete syntax validation must precede GraphQL envelope semantics");
	}
}

void TestRequiredNestedFieldFailsClosed() {
	auto profile = Profile();
	NeverCancelled control;
	const std::string body = "{\"data\":{\"viewer\":{\"repositories\":{\"nodes\":[{\"id\":\"R\","
	                         "\"nameWithOwner\":\"o/r\",\"owner\":{},\"stargazerCount\":1,\"primaryLanguage\":null,"
	                         "\"isPrivate\":false,\"isArchived\":false,\"updatedAt\":\"now\"}],"
	                         "\"pageInfo\":{\"hasNextPage\":false,\"endCursor\":null}}}}}";
	try {
		(void)duckdb_api::internal::DecodeGraphqlResponse(body, *profile, Limits(), control);
		throw std::runtime_error("missing owner login must fail");
	} catch (const duckdb_api::ExecutionError &error) {
		Require(error.Stage() == duckdb_api::ErrorStage::SCHEMA && error.Field() == "owner_login",
		        "nested required-field failure must use the safe output name");
	}
}

void TestZeroHundredAndDuplicateOccurrences() {
	auto profile = Profile();
	NeverCancelled control;
	auto zero = duckdb_api::internal::DecodeGraphqlResponse(Envelope(""), *profile, Limits(), control);
	Require(zero.rows.empty() && !zero.has_next, "zero-node GraphQL page must be valid terminal input");

	std::string hundred_nodes;
	for (std::size_t index = 0; index < 100; index++) {
		if (!hundred_nodes.empty()) {
			hundred_nodes += ',';
		}
		hundred_nodes += Node("R" + std::to_string(index));
	}
	auto hundred = duckdb_api::internal::DecodeGraphqlResponse(Envelope(hundred_nodes), *profile, Limits(), control);
	Require(hundred.rows.size() == 100 && hundred.rows.front().values[0].varchar_value == "R0" &&
	            hundred.rows.back().values[0].varchar_value == "R99",
	        "exactly 100 GraphQL nodes must preserve response order");

	auto duplicates = duckdb_api::internal::DecodeGraphqlResponse(Envelope(Node("same") + "," + Node("same")), *profile,
	                                                              Limits(), control);
	Require(duplicates.rows.size() == 2 && duplicates.rows[0].values[0].varchar_value == "same" &&
	            duplicates.rows[1].values[0].varchar_value == "same",
	        "GraphQL decoder must preserve duplicate repository occurrences");
}

void TestIgnoredValuesDoNotConsumeDecodedMemory() {
	auto profile = Profile();
	NeverCancelled control;
	const std::string ignored(2 * 1024 * 1024 + 1, 'x');
	const auto body = Envelope(Node("R"), false, "null", "\"ignored\":\"" + ignored + "\",");
	Require(body.size() < 8 * 1024 * 1024, "ignored-value fixture must remain inside the response ceiling");
	auto decoded = duckdb_api::internal::DecodeGraphqlResponse(body, *profile, Limits(), control);
	Require(decoded.rows.size() == 1 && decoded.rows[0].values[0].varchar_value == "R",
	        "ignored JSON strings must be validated without materialization");
}

void TestMaterializedStringAndMemoryBoundaries() {
	auto profile = Profile();
	NeverCancelled control;
	const std::string exact(512, 'x');
	auto decoded = duckdb_api::internal::DecodeGraphqlResponse(Envelope(Node(exact)), *profile, Limits(), control);
	Require(decoded.rows.size() == 1 && decoded.rows[0].values[0].varchar_value.size() == 512,
	        "string byte ceiling must accept its exact boundary");
	RequireFailure(Envelope(Node(exact + "x")), duckdb_api::ErrorStage::RESOURCE, "id");

	auto exact_memory = Limits();
	exact_memory.max_decoded_memory_bytes = decoded.retained_memory_bytes;
	auto exact_decoded =
	    duckdb_api::internal::DecodeGraphqlResponse(Envelope(Node(exact)), *profile, exact_memory, control);
	Require(exact_decoded.retained_memory_bytes == decoded.retained_memory_bytes,
	        "decoded-memory exact boundary must retain the same complete page");
	Require(decoded.retained_memory_bytes > 0, "decoded page must report retained memory");
	auto one_under = exact_memory;
	one_under.max_decoded_memory_bytes--;
	try {
		(void)duckdb_api::internal::DecodeGraphqlResponse(Envelope(Node(exact)), *profile, one_under, control);
		throw std::runtime_error("one-byte-under decoded-memory budget must fail");
	} catch (const duckdb_api::ExecutionError &error) {
		Require(error.Stage() == duckdb_api::ErrorStage::RESOURCE && error.Field() == "decoded_memory_bytes",
		        "decoded-memory +1 case used the wrong safe diagnostic");
	}
}

void TestWrongNullDuplicateAndEnvelopeShapesFailClosed() {
	RequireFailure(Envelope(Node("R", "\"1\"")), duckdb_api::ErrorStage::SCHEMA, "stars");
	const auto null_id = Node("R");
	auto null_required = null_id;
	const auto id = null_required.find("\"id\":\"R\"");
	Require(id != std::string::npos, "required-null fixture drifted");
	null_required.replace(id, 8, "\"id\":null");
	RequireFailure(Envelope(null_required), duckdb_api::ErrorStage::SCHEMA, "id");

	auto duplicate = Node("R");
	duplicate.insert(1, "\"id\":\"other\",");
	RequireFailure(Envelope(duplicate), duckdb_api::ErrorStage::SCHEMA, "id");
	RequireFailure("{}", duckdb_api::ErrorStage::SCHEMA, "data");
	RequireFailure("{\"data\":null,\"errors\":[]}", duckdb_api::ErrorStage::SCHEMA, "data");
	auto duplicate_data = Envelope(Node("R"));
	const auto errors = duplicate_data.find(",\"errors\"");
	Require(errors != std::string::npos, "duplicate-data fixture drifted");
	duplicate_data.insert(errors + 1, "\"data\":{},");
	RequireFailure(duplicate_data, duckdb_api::ErrorStage::SCHEMA, "data");
	RequireFailure("{\"data\":{},\"errors\":null}", duckdb_api::ErrorStage::SCHEMA, "errors");
}

} // namespace

int main() {
	try {
		TestEightColumnsAndNullableLanguage();
		TestErrorsPrecedeDataWithoutLeakingRemoteValues();
		TestMalformedDocumentPrecedesRemoteProtocol();
		TestRequiredNestedFieldFailsClosed();
		TestZeroHundredAndDuplicateOccurrences();
		TestIgnoredValuesDoNotConsumeDecodedMemory();
		TestMaterializedStringAndMemoryBoundaries();
		TestWrongNullDuplicateAndEnvelopeShapesFailClosed();
		std::cout << "GraphQL response decoder tests passed\n";
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return EXIT_FAILURE;
	}
}
