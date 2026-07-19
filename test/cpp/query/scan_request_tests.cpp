#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_request.hpp"
#include "connector/support/connector_catalog_test_fixtures.hpp"
#include "query/support/scan_request_test_support.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using duckdb_api_test::Require;
using duckdb_api_test::query_scan_request::ReadText;
using duckdb_api_test::query_scan_request::RequireCanaryAbsent;
using duckdb_api_test::query_scan_request::RequireFullSelectedSchema;
using duckdb_api_test::query_scan_request::RequireThrows;
using duckdb_api_test::query_scan_request::RuntimeCredentialCanary;
using duckdb_api_test::query_scan_request::ScopedEnvironment;

const char ANONYMOUS_RELATION[] = "duckdb_login_search_page";
const char AUTHENTICATED_RELATION[] = "authenticated_user";
const char REPOSITORY_RELATION[] = "authenticated_repositories";

void TestLogicalSecretReferenceContract() {
	const duckdb_api::LogicalSecretReference absent;
	Require(!absent.IsPresent() && absent.Snapshot() == "none", "default logical reference was not absent");
	RequireThrows<std::logic_error>([&]() { (void)absent.Name(); }, "absent logical reference exposed a name");
	RequireThrows<std::invalid_argument>([]() { (void)duckdb_api::LogicalSecretReference::Named(""); },
	                                     "empty logical reference name was accepted");

	std::string exact_name = "A;\n";
	exact_name.push_back(static_cast<char>(0x01));
	exact_name.push_back(static_cast<char>(0xff));
	const auto named = duckdb_api::LogicalSecretReference::Named(exact_name);
	const auto copy = named;
	Require(named.IsPresent() && named.Name() == exact_name, "named logical reference changed exact bytes");
	Require(named.Snapshot() == "named-hex:413b0a01ff", "logical reference did not use lower-case byte hex");
	Require(copy.Name() == named.Name() && copy.Snapshot() == named.Snapshot(),
	        "logical reference copy changed identity or explanation");
	Require(duckdb_api::LogicalSecretReference::Named(exact_name).Snapshot() == named.Snapshot(),
	        "logical reference snapshot was not deterministic");
	std::string every_byte;
	std::string expected_every_byte = "named-hex:";
	const std::string lower_hex = "0123456789abcdef";
	for (unsigned int value = 0; value <= 0xff; value++) {
		every_byte.push_back(static_cast<char>(value));
		expected_every_byte.push_back(lower_hex[value >> 4]);
		expected_every_byte.push_back(lower_hex[value & 0x0f]);
	}
	Require(duckdb_api::LogicalSecretReference::Named(every_byte).Snapshot() == expected_every_byte,
	        "logical reference did not lower-case and escape every byte");

	auto move_source = duckdb_api::LogicalSecretReference::Named("move_source");
	const auto moved = std::move(move_source);
	Require(moved.IsPresent() && moved.Name() == "move_source", "logical reference move lost destination identity");
	if (move_source.IsPresent()) {
		Require(!move_source.Name().empty() && move_source.Snapshot() != "named-hex:",
		        "moved-from logical reference remained present without a name");
	} else {
		Require(move_source.Snapshot() == "none", "moved-from logical reference did not become valid absence");
		RequireThrows<std::logic_error>([&]() { (void)move_source.Name(); },
		                                "moved-from absent logical reference exposed a name");
	}
}

void TestAcceptedAnonymousAndAuthenticatedRequests() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto anonymous =
	    duckdb_api::BuildConservativeScanRequest(connector, ANONYMOUS_RELATION, duckdb_api::LogicalSecretReference());
	const auto authenticated = duckdb_api::BuildConservativeScanRequest(
	    connector, AUTHENTICATED_RELATION, duckdb_api::LogicalSecretReference::Named("github_default"));
	const auto *anonymous_relation = connector.FindRelation(ANONYMOUS_RELATION);
	const auto *authenticated_relation = connector.FindRelation(AUTHENTICATED_RELATION);

	Require(anonymous_relation && authenticated_relation, "native provider relations disappeared");
	Require(anonymous.connector_name == "github" && anonymous.relation_name == ANONYMOUS_RELATION,
	        "anonymous request did not copy exact selected identity");
	Require(authenticated.connector_name == "github" && authenticated.relation_name == AUTHENTICATED_RELATION,
	        "authenticated request did not copy exact selected identity");
	RequireFullSelectedSchema(anonymous, *anonymous_relation);
	RequireFullSelectedSchema(authenticated, *authenticated_relation);
	Require(anonymous.explicit_inputs.empty() && authenticated.explicit_inputs.empty(),
	        "logical selector entered explicit relation inputs");
	Require(anonymous.predicate == "TRUE" && authenticated.predicate == "TRUE" && anonymous.orderings.empty() &&
	            authenticated.orderings.empty() && !anonymous.has_limit && !authenticated.has_limit &&
	            !anonymous.has_offset && !authenticated.has_offset,
	        "request builder left the conservative relational profile");
	Require(anonymous.capabilities.HasConservativeRelationalProfile() &&
	            authenticated.capabilities.HasConservativeRelationalProfile() &&
	            anonymous.capabilities.secret_manager && authenticated.capabilities.secret_manager,
	        "request builder did not publish the supported native capability profile");
	Require(!anonymous.secret_reference.IsPresent() && authenticated.secret_reference.Name() == "github_default",
	        "request builder changed logical-reference presence or identity");
	Require(anonymous.Snapshot() ==
	            "connector=github;relation=duckdb_login_search_page;inputs=[];projection=id,login,site_admin;"
	            "predicate=TRUE;ordering=[];limit=unset;offset=unset;capabilities=projection:unavailable,filter:"
	            "unavailable,ordering:unavailable,limit:unavailable,offset:unavailable,progress:unavailable,"
	            "cancellation:verified,secret-manager:available;secret-reference=none",
	        "anonymous request snapshot changed");
	Require(authenticated.Snapshot().find(";relation=authenticated_user;") != std::string::npos &&
	            authenticated.Snapshot().find("secret-reference=named-hex:6769746875625f64656661756c74") !=
	                std::string::npos,
	        "authenticated request snapshot lost exact relation or escaped selector identity");
}

void TestAuthenticatedRepositoryRequest() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto request = duckdb_api::BuildConservativeScanRequest(
	    connector, REPOSITORY_RELATION, duckdb_api::LogicalSecretReference::Named("github_default"));
	const auto copy = request;
	const auto *relation = connector.FindRelation(REPOSITORY_RELATION);

	Require(relation, "native repository relation disappeared");
	Require(request.connector_name == "github" && request.relation_name == REPOSITORY_RELATION,
	        "repository request did not copy exact selected identity");
	RequireFullSelectedSchema(request, *relation);
	Require(request.projected_columns == std::vector<std::string>({"id", "full_name", "private", "fork", "archived"}) &&
	            request.explicit_inputs.empty(),
	        "repository request did not preserve its exact full-projection closure");
	Require(request.predicate == "TRUE" && request.orderings.empty() && !request.has_limit && !request.has_offset &&
	            request.capabilities.HasConservativeRelationalProfile() && request.capabilities.secret_manager,
	        "repository request changed Query's conservative capability profile");
	Require(request.secret_reference.Name() == "github_default",
	        "repository request changed the exact logical secret reference");
	const std::string expected =
	    "connector=github;relation=authenticated_repositories;inputs=[];projection=id,full_name,private,fork,"
	    "archived;predicate=TRUE;ordering=[];limit=unset;offset=unset;capabilities=projection:unavailable,filter:"
	    "unavailable,ordering:unavailable,limit:unavailable,offset:unavailable,progress:unavailable,cancellation:"
	    "verified,secret-manager:available;secret-reference=named-hex:6769746875625f64656661756c74";
	Require(request.Snapshot() == expected && copy.Snapshot() == expected,
	        "repository request copy or exact snapshot drifted");
	for (const auto &forbidden : {"api.github.com", "/user/repos", "per_page", "page=", "Link", "Bearer "}) {
		Require(request.Snapshot().find(forbidden) == std::string::npos,
		        "repository request acquired provider pagination or credential state");
	}
}

void TestProviderOwnedDistinctSchemaRequests() {
	const auto connector = duckdb_api_test::BuildDistinctSchemaConnectorCatalogFixture();
	const auto anonymous = duckdb_api::BuildConservativeScanRequest(
	    connector, duckdb_api_test::DISTINCT_SCHEMA_ANONYMOUS_RELATION, duckdb_api::LogicalSecretReference());
	const auto authenticated =
	    duckdb_api::BuildConservativeScanRequest(connector, duckdb_api_test::DISTINCT_SCHEMA_AUTHENTICATED_RELATION,
	                                             duckdb_api::LogicalSecretReference::Named("fixture_secret_name"));
	const auto *anonymous_relation = connector.FindRelation(duckdb_api_test::DISTINCT_SCHEMA_ANONYMOUS_RELATION);
	const auto *authenticated_relation =
	    connector.FindRelation(duckdb_api_test::DISTINCT_SCHEMA_AUTHENTICATED_RELATION);

	Require(anonymous_relation && authenticated_relation, "Connector-owned distinct relations disappeared");
	Require(anonymous.connector_name == "fixture_distinct_catalog" &&
	            anonymous.relation_name == duckdb_api_test::DISTINCT_SCHEMA_ANONYMOUS_RELATION &&
	            anonymous.projected_columns == std::vector<std::string>({"public_id", "public_label"}) &&
	            !anonymous.secret_reference.IsPresent(),
	        "anonymous request was hard-coded to the native catalog shape");
	Require(authenticated.connector_name == "fixture_distinct_catalog" &&
	            authenticated.relation_name == duckdb_api_test::DISTINCT_SCHEMA_AUTHENTICATED_RELATION &&
	            authenticated.projected_columns ==
	                std::vector<std::string>({"profile_login", "profile_verified", "profile_generation"}) &&
	            authenticated.secret_reference.Name() == "fixture_secret_name",
	        "authenticated request was hard-coded to the native catalog shape");
	RequireFullSelectedSchema(anonymous, *anonymous_relation);
	RequireFullSelectedSchema(authenticated, *authenticated_relation);
}

void TestPresenceRulesAndExactSelection() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	RequireThrows<std::invalid_argument>(
	    [&]() {
		    (void)duckdb_api::BuildConservativeScanRequest(connector, ANONYMOUS_RELATION,
		                                                   duckdb_api::LogicalSecretReference::Named("github_default"));
	    },
	    "anonymous relation accepted a logical reference");
	RequireThrows<std::invalid_argument>(
	    [&]() {
		    (void)duckdb_api::BuildConservativeScanRequest(connector, AUTHENTICATED_RELATION,
		                                                   duckdb_api::LogicalSecretReference());
	    },
	    "authenticated relation accepted an absent logical reference");
	RequireThrows<std::invalid_argument>(
	    [&]() {
		    (void)duckdb_api::BuildConservativeScanRequest(connector, REPOSITORY_RELATION,
		                                                   duckdb_api::LogicalSecretReference());
	    },
	    "repository relation accepted an absent logical reference");
	for (const auto &relation_name : {"Authenticated_User", "Authenticated_Repositories", "missing"}) {
		RequireThrows<std::invalid_argument>(
		    [&]() {
			    (void)duckdb_api::BuildConservativeScanRequest(connector, relation_name,
			                                                   duckdb_api::LogicalSecretReference());
		    },
		    "unknown or wrong-case relation fell back under an absent reference");
		RequireThrows<std::invalid_argument>(
		    [&]() {
			    (void)duckdb_api::BuildConservativeScanRequest(
			        connector, relation_name, duckdb_api::LogicalSecretReference::Named("github_default"));
		    },
		    "unknown or wrong-case relation fell back under a named reference");
	}
}

void RequireRelationalMutationsRejected(bool secret_manager) {
	duckdb_api::AdapterCapabilities capabilities = {false, false, false, false, false, false, true, secret_manager};
	Require(capabilities.HasConservativeRelationalProfile(), "baseline relational profile was not conservative");
	capabilities.projection = true;
	Require(!capabilities.HasConservativeRelationalProfile(), "available projection was classified as conservative");
	capabilities.projection = false;
	capabilities.filter = true;
	Require(!capabilities.HasConservativeRelationalProfile(),
	        "available filter metadata was classified as conservative");
	capabilities.filter = false;
	capabilities.ordering = true;
	Require(!capabilities.HasConservativeRelationalProfile(), "available ordering was classified as conservative");
	capabilities.ordering = false;
	capabilities.limit = true;
	Require(!capabilities.HasConservativeRelationalProfile(), "available limit was classified as conservative");
	capabilities.limit = false;
	capabilities.offset = true;
	Require(!capabilities.HasConservativeRelationalProfile(), "available offset was classified as conservative");
	capabilities.offset = false;
	capabilities.progress = true;
	Require(!capabilities.HasConservativeRelationalProfile(), "available progress was classified as conservative");
	capabilities.progress = false;
	capabilities.cancellation = false;
	Require(!capabilities.HasConservativeRelationalProfile(), "unverified cancellation was classified as conservative");
}

void TestCapabilityClassificationAndSemanticsMutation() {
	RequireRelationalMutationsRejected(false);
	RequireRelationalMutationsRejected(true);

	const auto connector = duckdb_api::BuildNativeGithubConnector();
	auto request = duckdb_api::BuildConservativeScanRequest(
	    connector, AUTHENTICATED_RELATION, duckdb_api::LogicalSecretReference::Named("github_default"));
	request.capabilities.secret_manager = false;
	Require(request.capabilities.HasConservativeRelationalProfile() && request.secret_reference.IsPresent(),
	        "request could not represent missing required capability for defensive Semantics validation");
}

void TestEnvironmentIndependenceAndCredentialAbsence() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto baseline = duckdb_api::BuildConservativeScanRequest(
	    connector, AUTHENTICATED_RELATION, duckdb_api::LogicalSecretReference::Named("github_default"));
	const auto canary = RuntimeCredentialCanary();
	for (const auto &path : {"src/include/duckdb_api/scan_request.hpp", "src/query/scan_request.cpp",
	                         "test/cpp/query/scan_request_tests.cpp",
	                         "test/cpp/query/support/scan_request_test_support.hpp"}) {
		Require(ReadText(path).find(canary) == std::string::npos, "credential canary pre-existed in request source");
	}
	const auto production_source =
	    ReadText("src/include/duckdb_api/scan_request.hpp") + ReadText("src/query/scan_request.cpp");
	for (const auto &ambient_api :
	     {"getenv(", "std::getenv", "::getenv", "setenv(", "unsetenv(", "**environ", "*environ["}) {
		Require(production_source.find(ambient_api) == std::string::npos,
		        "request production source gained ambient environment access");
	}
	RequireCanaryAbsent(baseline, canary);

	ScopedEnvironment environment;
	for (const auto &name :
	     {"DUCKDB_API_TOKEN", "duckdb_api_token", "GITHUB_TOKEN", "github_token", "HOME", "CURL_HOME", "HTTP_PROXY",
	      "http_proxy", "HTTPS_PROXY", "https_proxy", "ALL_PROXY", "all_proxy", "NO_PROXY", "no_proxy"}) {
		environment.Set(name, canary);
	}
	const auto hostile = duckdb_api::BuildConservativeScanRequest(
	    connector, AUTHENTICATED_RELATION, duckdb_api::LogicalSecretReference::Named("github_default"));
	Require(hostile.Snapshot() == baseline.Snapshot(), "request construction depended on ambient environment state");
	RequireCanaryAbsent(hostile, canary);
}

} // namespace

int main() {
	try {
		TestLogicalSecretReferenceContract();
		TestAcceptedAnonymousAndAuthenticatedRequests();
		TestAuthenticatedRepositoryRequest();
		TestProviderOwnedDistinctSchemaRequests();
		TestPresenceRulesAndExactSelection();
		TestCapabilityClassificationAndSemanticsMutation();
		TestEnvironmentIndependenceAndCredentialAbsence();
		std::cout << "scan request tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "scan request tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
