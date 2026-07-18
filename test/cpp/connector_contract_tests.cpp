#include "duckdb_api/connector.hpp"
#include "support/connector_catalog_contract.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <locale>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using duckdb_api_test::Require;

class GroupedDigits final : public std::numpunct<char> {
protected:
	char do_thousands_sep() const override {
		return '_';
	}

	std::string do_grouping() const override {
		return "\3";
	}
};

void RequireColumn(const duckdb_api::CompiledColumn &column, const std::string &name, const std::string &logical_type,
                   const std::string &extractor) {
	Require(column.name == name, "CompiledRelation column name drifted: " + name);
	Require(column.logical_type == logical_type, "CompiledRelation column type drifted: " + name);
	Require(!column.nullable, "CompiledRelation column became nullable: " + name);
	Require(column.extractor == extractor, "CompiledRelation column extractor drifted: " + name);
}

void RequireSharedSchema(const duckdb_api::CompiledRelation &relation) {
	const auto &columns = relation.Columns();
	Require(columns.size() == 3, "CompiledRelation schema width drifted");
	RequireColumn(columns[0], "id", "BIGINT", "$.id");
	RequireColumn(columns[1], "login", "VARCHAR", "$.login");
	RequireColumn(columns[2], "site_admin", "BOOLEAN", "$.site_admin");
}

void RequireQueryParameter(const duckdb_api::CompiledQueryParameter &parameter, const std::string &name,
                           const std::string &encoded_value) {
	Require(parameter.name == name, "CompiledOperation query name drifted: " + name);
	Require(parameter.encoded_value == encoded_value, "CompiledOperation query value drifted: " + name);
}

void RequireHeader(const duckdb_api::CompiledHttpHeader &header, const std::string &name, const std::string &value) {
	Require(header.name == name, "CompiledOperation header name drifted: " + name);
	Require(header.value == value, "CompiledOperation header value drifted: " + name);
}

void RequireFixedHeaders(const std::vector<duckdb_api::CompiledHttpHeader> &headers) {
	Require(headers.size() == 3, "CompiledOperation fixed header width drifted");
	RequireHeader(headers[0], "Accept", "application/vnd.github+json");
	RequireHeader(headers[1], "User-Agent", "duckdb-api/0.4.0");
	RequireHeader(headers[2], "X-GitHub-Api-Version", "2022-11-28");
	for (const auto &header : headers) {
		Require(header.name != "Authorization" && header.name != "authorization",
		        "credential placement leaked into fixed headers");
	}
}

void RequireGithubOrigin(const duckdb_api::CompiledRestOrigin &origin) {
	Require(origin.scheme == duckdb_api::CompiledUrlScheme::HTTPS, "CompiledOperation origin scheme drifted");
	Require(origin.host.Value() == "api.github.com", "CompiledOperation origin host drifted");
	Require(origin.host.Value().find_first_of("/:?#@") == std::string::npos,
	        "CompiledOperation origin host contains URL structure");
	Require(origin.port == 443, "CompiledOperation origin port drifted");
}

void RequireBaseOperation(const duckdb_api::CompiledOperation &operation) {
	Require(operation.fallback, "CompiledOperation is no longer the fallback");
	Require(operation.protocol == duckdb_api::CompiledProtocol::REST, "CompiledOperation protocol drifted");
	Require(operation.method == duckdb_api::CompiledHttpMethod::GET, "CompiledOperation method drifted");
	Require(operation.replay_safety == duckdb_api::CompiledReplaySafety::SAFE,
	        "CompiledOperation replay safety drifted");
	Require(!operation.retry_enabled && !operation.pagination_enabled,
	        "CompiledOperation enabled an excluded capability");
	RequireGithubOrigin(operation.request.origin);
	RequireFixedHeaders(operation.request.headers);
}

void TestCatalogAndLookup() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	Require(connector.Origin() == duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA,
	        "CompiledConnector origin drifted");
	Require(connector.ConnectorName() == "github", "CompiledConnector identifier drifted");
	Require(connector.Version() == "0.4.0", "CompiledConnector metadata version drifted");
	Require(connector.Relations().size() == 2, "CompiledConnector relation catalog width drifted");
	Require(connector.Relations()[0].Name() == "duckdb_login_search_page",
	        "CompiledConnector anonymous relation order drifted");
	Require(connector.Relations()[1].Name() == "authenticated_user",
	        "CompiledConnector authenticated relation order drifted");
	Require(connector.FindRelation("duckdb_login_search_page") == &connector.Relations()[0],
	        "CompiledConnector exact lookup did not return the catalog relation");
	Require(connector.FindRelation("authenticated_user") == &connector.Relations()[1],
	        "CompiledConnector exact lookup did not return the authenticated relation");
	Require(connector.FindRelation("Authenticated_User") == nullptr,
	        "CompiledConnector lookup unexpectedly folded relation case");
	Require(connector.FindRelation("missing") == nullptr, "CompiledConnector lookup fabricated an unknown relation");

	const auto &policy = connector.NetworkPolicy();
	Require(policy.allowed_schemes == std::vector<std::string>({"https"}), "CompiledConnector allowed schemes drifted");
	Require(policy.allowed_hosts == std::vector<std::string>({"api.github.com"}),
	        "CompiledConnector allowed hosts drifted");
	Require(!policy.redirects_enabled && !policy.private_addresses_enabled && !policy.link_local_addresses_enabled &&
	            !policy.loopback_addresses_enabled,
	        "CompiledConnector widened network authority");
	Require(policy.max_response_bytes == 65536, "CompiledConnector response-byte ceiling drifted");
}

void TestAnonymousRelation() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto *relation = connector.FindRelation("duckdb_login_search_page");
	Require(relation != nullptr, "anonymous relation disappeared");
	RequireSharedSchema(*relation);

	const auto &operation = relation->Operation();
	RequireBaseOperation(operation);
	Require(operation.name == "github_search_duckdb_login_page", "anonymous operation identifier drifted");
	Require(operation.cardinality == duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	        "anonymous operation cardinality drifted");
	Require(operation.request.path == "/search/users", "anonymous request path drifted");
	Require(operation.request.query_parameters.size() == 2, "anonymous fixed query width drifted");
	RequireQueryParameter(operation.request.query_parameters[0], "q", "duckdb+in%3Alogin");
	RequireQueryParameter(operation.request.query_parameters[1], "per_page", "3");
	Require(operation.response_source == duckdb_api::CompiledResponseSource::JSON_PATH_MANY,
	        "anonymous response source drifted");
	Require(operation.records_extractor == "$.items[*]", "anonymous response extractor drifted");

	const auto &authentication = relation->Authentication();
	Require(authentication.Requirement() == duckdb_api::CompiledCredentialRequirement::NONE,
	        "anonymous relation unexpectedly requires a credential");
	Require(authentication.LogicalCredential().empty(), "anonymous relation retained a logical credential");
	Require(authentication.Authenticator() == duckdb_api::CompiledAuthenticator::NONE,
	        "anonymous relation retained an authenticator");
	Require(authentication.Placement() == duckdb_api::CompiledCredentialPlacement::NONE,
	        "anonymous relation retained credential placement");
	Require(authentication.Destination() == nullptr, "anonymous relation retained a credential destination");
	Require(relation->ResourceCeilings().max_records == 3, "anonymous record ceiling drifted");
	Require(relation->ResourceCeilings().max_extracted_string_bytes == 256,
	        "anonymous extracted-string ceiling drifted");
}

void TestAuthenticatedRelation() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto *relation = connector.FindRelation("authenticated_user");
	Require(relation != nullptr, "authenticated relation disappeared");
	RequireSharedSchema(*relation);

	const auto &operation = relation->Operation();
	RequireBaseOperation(operation);
	Require(operation.name == "github_authenticated_user", "authenticated operation identifier drifted");
	Require(operation.cardinality == duckdb_api::CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS,
	        "authenticated operation cardinality drifted");
	Require(operation.request.path == "/user", "authenticated request path drifted");
	Require(operation.request.query_parameters.empty(), "authenticated request gained a fixed query");
	Require(operation.response_source == duckdb_api::CompiledResponseSource::ROOT_OBJECT,
	        "authenticated response source drifted");
	Require(operation.records_extractor == "$", "authenticated root-object extractor drifted");

	const auto &authentication = relation->Authentication();
	Require(authentication.Requirement() == duckdb_api::CompiledCredentialRequirement::REQUIRED,
	        "authenticated relation no longer requires a credential");
	Require(authentication.LogicalCredential() == "token", "authenticated logical credential identifier drifted");
	Require(authentication.Authenticator() == duckdb_api::CompiledAuthenticator::BEARER,
	        "authenticated relation authenticator drifted");
	Require(authentication.Placement() == duckdb_api::CompiledCredentialPlacement::AUTHORIZATION_HEADER,
	        "authenticated relation placement drifted");
	Require(authentication.Destination() != nullptr, "authenticated relation lost its credential destination");
	RequireGithubOrigin(*authentication.Destination());
	Require(relation->ResourceCeilings().max_records == 1,
	        "authenticated relation lacks a distinct one-record ceiling");
	Require(relation->ResourceCeilings().max_extracted_string_bytes == 256,
	        "authenticated extracted-string ceiling drifted");
}

const std::string ANONYMOUS_SNAPSHOT =
    "relation=duckdb_login_search_page;schema=id:BIGINT!:$.id,login:VARCHAR!:$.login,"
    "site_admin:BOOLEAN!:$.site_admin;operation=github_search_duckdb_login_page:fallback:zero_to_many:REST:GET:"
    "replay_safe;request=origin:[scheme:https,host:api.github.com,port:443],path:/search/users,"
    "query:[q=duckdb+in%3Alogin,per_page=3],headers:[Accept=application/vnd.github+json,"
    "User-Agent=duckdb-api/0.4.0,X-GitHub-Api-Version=2022-11-28];response=source:json_path_many,"
    "records:$.items[*];features=retry:disabled,pagination:disabled;authentication=requirement:none,"
    "logical_credential:none,authenticator:none,destination:none,placement:none;ceilings=records:3,"
    "extracted_string_bytes:256";

const std::string AUTHENTICATED_SNAPSHOT =
    "relation=authenticated_user;schema=id:BIGINT!:$.id,login:VARCHAR!:$.login,site_admin:BOOLEAN!:$.site_admin;"
    "operation=github_authenticated_user:fallback:exactly_one_on_success:REST:GET:replay_safe;"
    "request=origin:[scheme:https,host:api.github.com,port:443],path:/user,query:[],"
    "headers:[Accept=application/vnd.github+json,User-Agent=duckdb-api/0.4.0,"
    "X-GitHub-Api-Version=2022-11-28];response=source:root_object,records:$;"
    "features=retry:disabled,pagination:disabled;authentication=requirement:required,logical_credential:token,"
    "authenticator:bearer,destination:[scheme:https,host:api.github.com,port:443],placement:Authorization;"
    "ceilings=records:1,extracted_string_bytes:256";

void TestCanonicalSnapshotsAndProvenance() {
	const auto first = duckdb_api::BuildNativeGithubConnector();
	const auto second = duckdb_api::BuildNativeGithubConnector();
	const std::string expected =
	    "origin=native_product_metadata;connector=github;version=0.4.0;network=schemes:[https],"
	    "hosts:[api.github.com],redirects:denied,private:denied,link_local:denied,loopback:denied,"
	    "max_response_bytes:65536;relations=[{" +
	    ANONYMOUS_SNAPSHOT + "},{" + AUTHENTICATED_SNAPSHOT + "}]";

	Require(first.Relations()[0].Snapshot() == ANONYMOUS_SNAPSHOT, "anonymous relation snapshot drifted");
	Require(first.Relations()[1].Snapshot() == AUTHENTICATED_SNAPSHOT, "authenticated relation snapshot drifted");
	Require(first.Snapshot() == expected, "CompiledConnector canonical snapshot drifted");
	Require(second.Snapshot() == first.Snapshot(), "CompiledConnector construction is not deterministic");
	const auto copy = first;
	Require(copy.Snapshot() == first.Snapshot(), "CompiledConnector copy changed source metadata");
	Require(&copy.Relations()[0] != &first.Relations()[0], "CompiledConnector copy did not own its relation catalog");

	const std::locale original_locale;
	std::string localized_snapshot;
	try {
		std::locale::global(std::locale(std::locale::classic(), new GroupedDigits()));
		localized_snapshot = first.Snapshot();
		std::locale::global(original_locale);
	} catch (...) {
		std::locale::global(original_locale);
		throw;
	}
	Require(localized_snapshot == expected, "CompiledConnector snapshot depends on the process-global locale");

	const std::vector<std::string> prohibited = {
	    "github_default", "fixture=",     "digest=",           "package_root=",
	    ".yaml",          "secret_name=", "credential_value=", "Authorization="};
	for (const auto &value : prohibited) {
		Require(first.Snapshot().find(value) == std::string::npos,
		        "CompiledConnector snapshot retained prohibited provenance or credential material: " + value);
	}
	Require(first.Snapshot().find("origin=native_product_metadata") != std::string::npos,
	        "CompiledConnector snapshot lost native provenance");
	Require(first.Snapshot().find("logical_credential:token") != std::string::npos,
	        "CompiledConnector snapshot lost its safe logical credential requirement");
}

} // namespace

int main() {
	try {
		duckdb_api_test::RunConnectorCatalogContractTests();
		TestCatalogAndLookup();
		TestAnonymousRelation();
		TestAuthenticatedRelation();
		TestCanonicalSnapshotsAndProvenance();
		std::cout << "connector contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "connector contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
