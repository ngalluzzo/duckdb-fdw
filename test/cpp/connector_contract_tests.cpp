#include "duckdb_api/connector.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <locale>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using duckdb_api_test::Require;

template <typename T>
class HasBaseUrlMember {
	template <typename U>
	static char Test(decltype(&U::base_url));
	template <typename U>
	static long Test(...);

public:
	static const bool VALUE = sizeof(Test<T>(0)) == sizeof(char);
};

template <typename T>
class HasPathMember {
	template <typename U>
	static char Test(decltype(&U::path));
	template <typename U>
	static long Test(...);

public:
	static const bool VALUE = sizeof(Test<T>(0)) == sizeof(char);
};

template <typename T>
class HasQueryMember {
	template <typename U>
	static char Test(decltype(&U::query));
	template <typename U>
	static long Test(...);

public:
	static const bool VALUE = sizeof(Test<T>(0)) == sizeof(char);
};

template <typename T>
class HasFragmentMember {
	template <typename U>
	static char Test(decltype(&U::fragment));
	template <typename U>
	static long Test(...);

public:
	static const bool VALUE = sizeof(Test<T>(0)) == sizeof(char);
};

static_assert(!HasBaseUrlMember<duckdb_api::CompiledRestRequest>::VALUE,
              "CompiledRestRequest must not restore a parseable base URL");
static_assert(!HasPathMember<duckdb_api::CompiledRestOrigin>::VALUE,
              "CompiledRestOrigin must not carry a path component");
static_assert(!HasQueryMember<duckdb_api::CompiledRestOrigin>::VALUE,
              "CompiledRestOrigin must not carry a query component");
static_assert(!HasFragmentMember<duckdb_api::CompiledRestOrigin>::VALUE,
              "CompiledRestOrigin must not carry a fragment component");
static_assert(std::is_same<decltype(duckdb_api::CompiledRestOrigin::scheme), duckdb_api::CompiledUrlScheme>::value,
              "CompiledRestOrigin scheme must remain typed");
static_assert(std::is_same<decltype(duckdb_api::CompiledRestOrigin::host), duckdb_api::CompiledRestHost>::value,
              "CompiledRestOrigin host must remain a validated exact host component");
static_assert(std::is_same<decltype(duckdb_api::CompiledRestOrigin::port), std::uint16_t>::value,
              "CompiledRestOrigin port must remain an explicit uint16_t");

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
	Require(column.name == name, "CompiledConnector column name drifted: " + name);
	Require(column.logical_type == logical_type, "CompiledConnector column type drifted: " + name);
	Require(!column.nullable, "CompiledConnector column became nullable: " + name);
	Require(column.extractor == extractor, "CompiledConnector column extractor drifted: " + name);
}

void RequireQueryParameter(const duckdb_api::CompiledQueryParameter &parameter, const std::string &name,
                           const std::string &encoded_value) {
	Require(parameter.name == name, "CompiledConnector query name drifted: " + name);
	Require(parameter.encoded_value == encoded_value, "CompiledConnector query value drifted: " + name);
}

void RequireHeader(const duckdb_api::CompiledHttpHeader &header, const std::string &name, const std::string &value) {
	Require(header.name == name, "CompiledConnector header name drifted: " + name);
	Require(header.value == value, "CompiledConnector header value drifted: " + name);
}

void RequireInvalidHost(const std::string &value) {
	try {
		duckdb_api::CompiledRestHost host(value);
		(void)host;
		throw std::runtime_error("CompiledRestHost accepted URL structure: " + value);
	} catch (const std::invalid_argument &) {
	}
}

void TestNativeGithubMetadata() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	Require(connector.origin == duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA,
	        "CompiledConnector origin drifted");
	Require(connector.connector_name == "github", "CompiledConnector identifier drifted");
	Require(connector.version == "0.3.0", "CompiledConnector metadata version drifted");
	Require(connector.relation_name == "duckdb_login_search_page", "CompiledConnector relation drifted");

	Require(connector.columns.size() == 3, "CompiledConnector schema width drifted");
	RequireColumn(connector.columns[0], "id", "BIGINT", "$.id");
	RequireColumn(connector.columns[1], "login", "VARCHAR", "$.login");
	RequireColumn(connector.columns[2], "site_admin", "BOOLEAN", "$.site_admin");

	const auto &operation = connector.operation;
	Require(operation.name == "github_search_duckdb_login_page", "CompiledConnector operation identifier drifted");
	Require(operation.fallback, "CompiledConnector operation is no longer the fallback");
	Require(operation.cardinality == duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	        "CompiledConnector cardinality drifted");
	Require(operation.protocol == duckdb_api::CompiledProtocol::REST, "CompiledConnector protocol drifted");
	Require(operation.method == duckdb_api::CompiledHttpMethod::GET, "CompiledConnector method drifted");
	Require(operation.replay_safety == duckdb_api::CompiledReplaySafety::SAFE,
	        "CompiledConnector replay safety drifted");
	Require(!operation.retry_enabled && !operation.authentication_enabled && !operation.pagination_enabled,
	        "CompiledConnector enabled an excluded operation capability");

	Require(operation.request.origin.scheme == duckdb_api::CompiledUrlScheme::HTTPS,
	        "CompiledConnector origin scheme drifted");
	Require(operation.request.origin.host.Value() == "api.github.com", "CompiledConnector origin host drifted");
	Require(operation.request.origin.host.Value().find_first_of("/:?#@") == std::string::npos,
	        "CompiledConnector origin host contains URL structure");
	Require(operation.request.origin.port == 443, "CompiledConnector origin port drifted");
	Require(operation.request.path == "/search/users", "CompiledConnector request path drifted");
	Require(operation.request.query_parameters.size() == 2, "CompiledConnector fixed query width drifted");
	RequireQueryParameter(operation.request.query_parameters[0], "q", "duckdb+in%3Alogin");
	RequireQueryParameter(operation.request.query_parameters[1], "per_page", "3");
	Require(operation.request.headers.size() == 3, "CompiledConnector fixed header width drifted");
	RequireHeader(operation.request.headers[0], "Accept", "application/vnd.github+json");
	RequireHeader(operation.request.headers[1], "User-Agent", "duckdb-api/0.3.0");
	RequireHeader(operation.request.headers[2], "X-GitHub-Api-Version", "2022-11-28");
	Require(operation.records_extractor == "$.items[*]", "CompiledConnector response extractor drifted");

	const auto &policy = connector.network_policy;
	Require(policy.allowed_schemes == std::vector<std::string>({"https"}), "CompiledConnector allowed schemes drifted");
	Require(policy.allowed_hosts == std::vector<std::string>({"api.github.com"}),
	        "CompiledConnector allowed hosts drifted");
	Require(!policy.redirects_enabled && !policy.private_addresses_enabled && !policy.link_local_addresses_enabled &&
	            !policy.loopback_addresses_enabled,
	        "CompiledConnector widened network authority");
	Require(policy.max_response_bytes == 65536, "CompiledConnector response-byte ceiling drifted");
	Require(connector.resource_ceilings.max_records == 3, "CompiledConnector record ceiling drifted");
	Require(connector.resource_ceilings.max_extracted_string_bytes == 256,
	        "CompiledConnector extracted-string ceiling drifted");
}

void TestCanonicalSnapshot() {
	const auto first = duckdb_api::BuildNativeGithubConnector();
	const auto second = duckdb_api::BuildNativeGithubConnector();
	const std::string expected =
	    "origin=native_product_metadata;connector=github;version=0.3.0;relation=duckdb_login_search_page;"
	    "schema=id:BIGINT!:$.id,login:VARCHAR!:$.login,site_admin:BOOLEAN!:$.site_admin;"
	    "operation=github_search_duckdb_login_page:fallback:zero_to_many:REST:GET:replay_safe;"
	    "request=origin:[scheme:https,host:api.github.com,port:443],path:/search/users,"
	    "query:[q=duckdb+in%3Alogin,per_page=3],"
	    "headers:[Accept=application/vnd.github+json,User-Agent=duckdb-api/0.3.0,"
	    "X-GitHub-Api-Version=2022-11-28];response_records=$.items[*];"
	    "features=retry:disabled,authentication:disabled,pagination:disabled;"
	    "network=schemes:[https],hosts:[api.github.com],redirects:denied,private:denied,link_local:denied,"
	    "loopback:denied,max_response_bytes:65536;ceilings=records:3,extracted_string_bytes:256";

	Require(first.Snapshot() == expected, "CompiledConnector canonical snapshot drifted");
	Require(second.Snapshot() == first.Snapshot(), "CompiledConnector construction is not deterministic");
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
	Require(first.Snapshot().find("fixture=") == std::string::npos,
	        "CompiledConnector retained fixture-response provenance");
	Require(first.Snapshot().find("digest=") == std::string::npos,
	        "CompiledConnector retained response-content identity");
}

void TestOriginCounterexamplesRemainStructural() {
	const auto canonical = duckdb_api::BuildNativeGithubConnector();
	auto wrong_port = canonical;
	wrong_port.operation.request.origin.port = 444;
	Require(wrong_port.Snapshot() != canonical.Snapshot(), "CompiledConnector snapshot hid a non-canonical port");
	Require(wrong_port.Snapshot().find("origin:[scheme:https,host:api.github.com,port:444]") != std::string::npos,
	        "CompiledConnector snapshot failed to expose a non-canonical port structurally");

	auto wrong_scheme = canonical;
	wrong_scheme.operation.request.origin.scheme = duckdb_api::CompiledUrlScheme::HTTP;
	Require(wrong_scheme.Snapshot() != canonical.Snapshot(), "CompiledConnector snapshot hid a non-canonical scheme");
	Require(wrong_scheme.Snapshot().find("origin:[scheme:http,host:api.github.com,port:443]") != std::string::npos,
	        "CompiledConnector snapshot failed to expose a non-canonical scheme structurally");

	RequireInvalidHost("api.github.com:444");
	RequireInvalidHost("api.github.com/root");
	RequireInvalidHost("api.github.com?pre=1");
	RequireInvalidHost("api.github.com#fragment");
	RequireInvalidHost("user@api.github.com");
	RequireInvalidHost("https://api.github.com:444/root?pre=1#fragment");
}

} // namespace

int main() {
	try {
		TestNativeGithubMetadata();
		TestCanonicalSnapshot();
		TestOriginCounterexamplesRemainStructural();
		std::cout << "connector contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "connector contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
