#include "controlled_runtime_scenario.hpp"

#include "runtime/support/controlled_http_transport.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace duckdb_api_test {

struct ControlledRuntimeScenario::State {
	State(std::shared_ptr<ControlledHttpRuntime> runtime_p, uint64_t expected_request_count_p,
	      bool has_terminal_stage_p, duckdb_api::ErrorStage terminal_stage_p)
	    : runtime(std::move(runtime_p)), expected_request_count(expected_request_count_p),
	      has_terminal_stage(has_terminal_stage_p), terminal_stage(terminal_stage_p) {
	}

	std::shared_ptr<ControlledHttpRuntime> runtime;
	uint64_t expected_request_count;
	bool has_terminal_stage;
	duckdb_api::ErrorStage terminal_stage;
};

namespace {

std::string GraphqlNode(const char *language) {
	return std::string("{\"id\":\"R-duplicate\",\"nameWithOwner\":\"duckdb/duckdb\",") +
	       "\"owner\":{\"login\":\"duckdb\"},\"stargazerCount\":42,\"primaryLanguage\":" + language +
	       ",\"isPrivate\":false,\"isArchived\":false,\"updatedAt\":\"2026-07-01T00:00:00Z\"}";
}

std::string GraphqlRelationalNode(const char *id, int64_t stars, bool archived) {
	return std::string("{\"id\":\"") + id + "\",\"nameWithOwner\":\"fixture/" + id +
	       "\",\"owner\":{\"login\":\"fixture\"},\"stargazerCount\":" + std::to_string(stars) +
	       ",\"primaryLanguage\":null,\"isPrivate\":false,\"isArchived\":" + (archived ? "true" : "false") +
	       ",\"updatedAt\":\"2026-07-01T00:00:00Z\"}";
}

std::string GraphqlPage(const std::string &node, bool has_next, const char *cursor) {
	return "{\"data\":{\"viewer\":{\"repositories\":{\"nodes\":[" + node +
	       "],\"pageInfo\":{\"hasNextPage\":" + (has_next ? "true" : "false") + ",\"endCursor\":" + cursor +
	       "}}}},\"errors\":[]}";
}

} // namespace

ControlledRuntimeScenario::ControlledRuntimeScenario(std::shared_ptr<State> state_p,
                                                     std::shared_ptr<const duckdb_api::ScanExecutor> executor_p)
    : state(std::move(state_p)), executor(std::move(executor_p)) {
}

std::shared_ptr<const duckdb_api::ScanExecutor> ControlledRuntimeScenario::Executor() const {
	return executor;
}

ControlledRuntimeScenarioObservation ControlledRuntimeScenario::Observation() const {
	return {static_cast<uint64_t>(state->runtime->Observations().size()), state->expected_request_count,
	        state->has_terminal_stage, state->terminal_stage};
}

std::shared_ptr<ControlledRuntimeScenario> BuildControlledRuntimeScenario(ControlledRuntimeScenarioId scenario) {
	auto runtime = scenario == ControlledRuntimeScenarioId::RICKANDMORTY_CHARACTER_EPISODES
	                   ? BuildControlledHttpRuntimeForHost("rickandmortyapi.com")
	                   : BuildControlledHttpRuntime();
	uint64_t expected_request_count = 1;
	bool has_terminal_stage = false;
	auto terminal_stage = duckdb_api::ErrorStage::INTERNAL;
	switch (scenario) {
	case ControlledRuntimeScenarioId::RETAINED_REST_USER:
		runtime->Respond(200, "{\"id\":11,\"login\":\"duckdb\",\"site_admin\":false}");
		break;
	case ControlledRuntimeScenarioId::RICKANDMORTY_CHARACTER_EPISODES:
		expected_request_count = 2;
		{
			const auto response = ControlledResponse(
			    200, "{\"info\":{\"count\":4,\"pages\":1,\"next\":null,\"prev\":null},\"results\":["
			         "{\"id\":4,\"name\":\"Beth Smith\",\"status\":\"Alive\",\"species\":\"Human\","
			         "\"origin\":{\"name\":\"Earth (Replacement Dimension)\"},\"episode\":["
			         "\"https://rickandmortyapi.com/api/episode/4\","
			         "\"https://rickandmortyapi.com/api/episode/1\","
			         "\"https://rickandmortyapi.com/api/episode/4\"]},"
			         "{\"id\":1,\"name\":\"Rick Sanchez\",\"status\":\"Alive\",\"species\":\"Human\","
			         "\"origin\":{\"name\":\"Earth (C-137)\"},\"episode\":["
			         "\"https://rickandmortyapi.com/api/episode/1\","
			         "\"https://rickandmortyapi.com/api/episode/2\"]},"
			         "{\"id\":3,\"name\":\"Summer Smith\",\"status\":\"Alive\",\"species\":\"Human\","
			         "\"origin\":{\"name\":\"Earth (Replacement Dimension)\"},\"episode\":[]},"
			         "{\"id\":2,\"name\":\"Morty Smith\",\"status\":\"Alive\",\"species\":\"Human\","
			         "\"origin\":{\"name\":\"unknown\"},\"episode\":["
			         "\"https://rickandmortyapi.com/api/episode/2\"]}]}");
			runtime->RespondSequence({response, response});
		}
		break;
	case ControlledRuntimeScenarioId::GRAPHQL_MULTI_PAGE_NULL_DUPLICATE: {
		expected_request_count = 4;
		const auto first_page =
		    ControlledResponse(200, GraphqlPage(GraphqlNode("null"), true, "\"runtime-owned-next\""));
		const auto second_page = ControlledResponse(200, GraphqlPage(GraphqlNode("{\"name\":\"C++\"}"), false, "null"));
		runtime->RespondSequence({first_page, second_page, first_page, second_page});
		break;
	}
	case ControlledRuntimeScenarioId::GRAPHQL_RELATIONAL_COMPOSITION:
		runtime->Respond(200, GraphqlPage(GraphqlRelationalNode("R-archived-high", 999, true) + "," +
		                                      GraphqlRelationalNode("R-active-low", 7, false) + "," +
		                                      GraphqlRelationalNode("R-active-high", 42, false),
		                                  false, "null"));
		break;
	case ControlledRuntimeScenarioId::GRAPHQL_APPLICATION_ERROR:
		has_terminal_stage = true;
		terminal_stage = duckdb_api::ErrorStage::REMOTE_PROTOCOL;
		runtime->Respond(200, "{\"data\":null,\"errors\":[{\"message\":\"runtime-owned private canary\"}]}");
		break;
	case ControlledRuntimeScenarioId::GRAPHQL_LATE_HTTP_STATUS:
		expected_request_count = 2;
		has_terminal_stage = true;
		terminal_stage = duckdb_api::ErrorStage::HTTP_STATUS;
		runtime->RespondSequence(
		    {ControlledResponse(200, GraphqlPage(GraphqlNode("null"), true, "\"runtime-owned-late\"")),
		     ControlledResponse(429, "runtime-owned private status body")});
		break;
	case ControlledRuntimeScenarioId::BLOCK_UNTIL_CANCEL:
		runtime->BlockUntilCancelled();
		break;
	default:
		throw std::invalid_argument("unknown controlled Runtime scenario");
	}
	auto state = std::make_shared<ControlledRuntimeScenario::State>(runtime, expected_request_count, has_terminal_stage,
	                                                                terminal_stage);
	return std::shared_ptr<ControlledRuntimeScenario>(
	    new ControlledRuntimeScenario(std::move(state), runtime->Executor()));
}

} // namespace duckdb_api_test
