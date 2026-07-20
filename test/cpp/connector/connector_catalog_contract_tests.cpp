#include "duckdb_api/connector_catalog.hpp"
#include "connector/support/catalog_contract.hpp"
#include "connector/support/catalog_test_access.hpp"
#include "support/require.hpp"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using duckdb_api_test::ConnectorCatalogTestAccess;
using duckdb_api_test::Require;

#define DEFINE_MEMBER_PROBE(PROBE_NAME, MEMBER_NAME)                                                                   \
	template <typename T>                                                                                              \
	class PROBE_NAME {                                                                                                 \
		template <typename U>                                                                                          \
		static char Test(decltype(&U::MEMBER_NAME));                                                                   \
		template <typename U>                                                                                          \
		static long Test(...);                                                                                         \
                                                                                                                       \
	public:                                                                                                            \
		static const bool VALUE = sizeof(Test<T>(0)) == sizeof(char);                                                  \
	}

DEFINE_MEMBER_PROBE(HasBaseUrlMember, base_url);
DEFINE_MEMBER_PROBE(HasPathMember, path);
DEFINE_MEMBER_PROBE(HasQueryMember, query);
DEFINE_MEMBER_PROBE(HasFragmentMember, fragment);
DEFINE_MEMBER_PROBE(HasAuthenticationEnabledMember, authentication_enabled);
DEFINE_MEMBER_PROBE(HasSecretNameMember, secret_name);
DEFINE_MEMBER_PROBE(HasCredentialValueMember, credential_value);
DEFINE_MEMBER_PROBE(HasTokenValueMember, token_value);
DEFINE_MEMBER_PROBE(HasSecretHandleMember, secret_handle);

#undef DEFINE_MEMBER_PROBE

static_assert(!HasBaseUrlMember<duckdb_api::CompiledRestRequest>::VALUE,
              "CompiledRestRequest must not restore a parseable base URL");
static_assert(!HasPathMember<duckdb_api::CompiledRestOrigin>::VALUE,
              "CompiledRestOrigin must not carry a path component");
static_assert(!HasQueryMember<duckdb_api::CompiledRestOrigin>::VALUE,
              "CompiledRestOrigin must not carry a query component");
static_assert(!HasFragmentMember<duckdb_api::CompiledRestOrigin>::VALUE,
              "CompiledRestOrigin must not carry a fragment component");
static_assert(!HasAuthenticationEnabledMember<duckdb_api::CompiledOperation>::VALUE,
              "authentication must have one owner in the relation policy");
static_assert(!HasSecretNameMember<duckdb_api::CompiledAuthenticationPolicy>::VALUE,
              "credential policy must not expose a DuckDB secret name");
static_assert(!HasCredentialValueMember<duckdb_api::CompiledAuthenticationPolicy>::VALUE,
              "credential policy must not expose credential bytes");
static_assert(!HasTokenValueMember<duckdb_api::CompiledAuthenticationPolicy>::VALUE,
              "credential policy must not expose token bytes");
static_assert(!HasSecretHandleMember<duckdb_api::CompiledAuthenticationPolicy>::VALUE,
              "credential policy must not expose a provider handle");
static_assert(std::is_default_constructible<duckdb_api::CompiledOperationSelector>::value,
              "installed fallback operations require the closed empty selector");
static_assert(std::is_copy_constructible<duckdb_api::CompiledOperationSelector>::value,
              "immutable selectors must support catalog copies");
static_assert(std::is_move_constructible<duckdb_api::CompiledOperationSelector>::value,
              "immutable selectors must support catalog ownership transfer");
static_assert(!std::is_copy_assignable<duckdb_api::CompiledOperationSelector>::value,
              "selector assignment would permit post-construction replacement");
static_assert(!std::is_move_assignable<duckdb_api::CompiledOperationSelector>::value,
              "selector assignment would permit post-construction replacement");
static_assert(
    !std::is_constructible<duckdb_api::CompiledOperationSelector, std::vector<std::string>,
                           std::vector<std::vector<std::string>>, std::vector<std::string>, std::int32_t>::value,
    "production consumers must not construct arbitrary operation selectors");
static_assert(std::is_same<decltype(duckdb_api::CompiledRestOrigin::scheme), duckdb_api::CompiledUrlScheme>::value,
              "CompiledRestOrigin scheme must remain typed");
static_assert(std::is_same<decltype(duckdb_api::CompiledRestOrigin::host), duckdb_api::CompiledRestHost>::value,
              "CompiledRestOrigin host must remain a validated exact host component");
static_assert(std::is_same<decltype(duckdb_api::CompiledRestOrigin::port), std::uint16_t>::value,
              "CompiledRestOrigin port must remain an explicit uint16_t");
static_assert(std::is_copy_constructible<duckdb_api::CompiledConnector>::value,
              "immutable catalog must support bind/composition copies");
static_assert(std::is_move_constructible<duckdb_api::CompiledConnector>::value,
              "immutable catalog must support ownership transfer");
static_assert(!std::is_copy_assignable<duckdb_api::CompiledConnector>::value,
              "catalog assignment would permit post-construction replacement");
static_assert(!std::is_move_assignable<duckdb_api::CompiledConnector>::value,
              "catalog assignment would permit post-construction replacement");
static_assert(!std::is_default_constructible<duckdb_api::CompiledConnector>::value,
              "catalog must not admit partial construction");
static_assert(
    !std::is_constructible<duckdb_api::CompiledConnector, duckdb_api::CompiledConnectorOrigin, std::string, std::string,
                           std::vector<duckdb_api::CompiledRelation>, duckdb_api::CompiledNetworkPolicy>::value,
    "production callers must not construct arbitrary catalog provenance or authority");
static_assert(std::is_copy_constructible<duckdb_api::CompiledRelation>::value,
              "immutable relation must support catalog copies");
static_assert(std::is_move_constructible<duckdb_api::CompiledRelation>::value,
              "immutable relation must support catalog ownership transfer");
static_assert(!std::is_copy_assignable<duckdb_api::CompiledRelation>::value,
              "relation assignment would permit post-construction replacement");
static_assert(!std::is_move_assignable<duckdb_api::CompiledRelation>::value,
              "relation assignment would permit post-construction replacement");
static_assert(!std::is_default_constructible<duckdb_api::CompiledRelation>::value,
              "relation must not admit partial construction");
static_assert(
    !std::is_constructible<duckdb_api::CompiledRelation, std::string, std::vector<duckdb_api::CompiledColumn>,
                           std::vector<duckdb_api::CompiledPredicateMapping>, duckdb_api::CompiledOperation,
                           duckdb_api::CompiledAuthenticationPolicy, duckdb_api::CompiledResourceCeilings>::value,
    "production callers must not construct arbitrary relation authority");
static_assert(!std::is_constructible<
                  duckdb_api::CompiledRelation, std::string, std::vector<duckdb_api::CompiledColumn>,
                  std::vector<duckdb_api::CompiledPredicateMapping>, std::vector<duckdb_api::CompiledOperation>,
                  duckdb_api::CompiledAuthenticationPolicy, duckdb_api::CompiledResourceCeilings>::value,
              "production callers must not construct arbitrary multi-operation relation authority");

template <typename Callable>
void RequireInvalid(const std::string &message, Callable callback) {
	bool rejected = false;
	try {
		callback();
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	Require(rejected, message);
}

duckdb_api::CompiledOperation WithSelector(duckdb_api::CompiledOperation operation, bool fallback,
                                           duckdb_api::CompiledOperationSelector selector) {
	return duckdb_api::CompiledOperation {std::move(operation.name),
	                                      fallback,
	                                      operation.cardinality,
	                                      operation.protocol,
	                                      operation.method,
	                                      operation.replay_safety,
	                                      operation.retry_enabled,
	                                      std::move(operation.pagination),
	                                      std::move(operation.request),
	                                      operation.response_source,
	                                      std::move(operation.records_extractor),
	                                      std::move(selector)};
}

duckdb_api::CompiledConnector BuildValidCatalogFixture() {
	const duckdb_api::CompiledRestOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledRestHost("api.github.com"), 443};
	const std::vector<duckdb_api::CompiledColumn> columns = {{"id", "BIGINT", false, "$.id"}};
	const std::vector<duckdb_api::CompiledHttpHeader> headers = {{"X-Fixture", "safe"}};

	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    "anonymous_rows", columns,
	    duckdb_api::CompiledOperation {"fixture_anonymous_rows",
	                                   true,
	                                   duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	                                   duckdb_api::CompiledProtocol::REST,
	                                   duckdb_api::CompiledHttpMethod::GET,
	                                   duckdb_api::CompiledReplaySafety::SAFE,
	                                   false,
	                                   ConnectorCatalogTestAccess::DisabledPagination(),
	                                   {origin, "/rows", {}, headers},
	                                   duckdb_api::CompiledResponseSource::JSON_PATH_MANY,
	                                   "$.items[*]",
	                                   duckdb_api::CompiledOperationSelector()},
	    ConnectorCatalogTestAccess::Anonymous(), ConnectorCatalogTestAccess::UnpaginatedResources(2, 64)));
	relations.push_back(ConnectorCatalogTestAccess::Relation(
	    "current_row", columns,
	    duckdb_api::CompiledOperation {"fixture_current_row",
	                                   true,
	                                   duckdb_api::CompiledOperationCardinality::EXACTLY_ONE_ON_SUCCESS,
	                                   duckdb_api::CompiledProtocol::REST,
	                                   duckdb_api::CompiledHttpMethod::GET,
	                                   duckdb_api::CompiledReplaySafety::SAFE,
	                                   false,
	                                   ConnectorCatalogTestAccess::DisabledPagination(),
	                                   {origin, "/row", {}, headers},
	                                   duckdb_api::CompiledResponseSource::ROOT_OBJECT,
	                                   "$",
	                                   duckdb_api::CompiledOperationSelector()},
	    ConnectorCatalogTestAccess::RequiredBearer(), ConnectorCatalogTestAccess::UnpaginatedResources(1, 64)));
	return ConnectorCatalogTestAccess::Catalog(
	    duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA, "fixture", "1.0.0", std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"api.github.com"}, false, false, false, false, 4096});
}

void TestSafeImmutableService() {
	const auto catalog = BuildValidCatalogFixture();
	Require(catalog.Relations().size() == 2, "validated fixture catalog lost relations");
	Require(catalog.FindRelation("anonymous_rows") == &catalog.Relations()[0],
	        "exact lookup did not return the owned anonymous relation");
	Require(catalog.FindRelation("current_row") == &catalog.Relations()[1],
	        "exact lookup did not return the owned authenticated relation");
	Require(catalog.FindRelation("Current_Row") == nullptr, "exact lookup folded identifier case");
	Require(catalog.FindRelation("missing") == nullptr, "exact lookup fabricated a relation");
	Require(catalog.Relations()[0].HasSingleOperation() && catalog.Relations()[0].Operations().size() == 1 &&
	            &catalog.Relations()[0].Operation() == &catalog.Relations()[0].Operations()[0],
	        "single-operation compatibility access diverged from the immutable operation collection");
	const auto &selector = catalog.Relations()[0].Operation().selector;
	Require(selector.RequiredInputs().empty() && selector.AnyInputSets().empty() &&
	            selector.ForbiddenInputs().empty() && selector.Priority() == 0,
	        "installed-compatible operation did not receive the closed empty selector");

	const auto copy = catalog;
	Require(copy.Snapshot() == catalog.Snapshot(), "copy construction changed immutable catalog metadata");
	Require(&copy.Relations()[0] != &catalog.Relations()[0], "copy did not own its immutable relation storage");
	Require(catalog.Relations()[0].Authentication().Destination() == nullptr,
	        "anonymous policy retained credential destination authority");
	Require(catalog.Relations()[1].Authentication().LogicalCredential() == "token",
	        "required policy lost its safe logical identifier");
	Require(catalog.Relations()[1].Authentication().Destination() != nullptr,
	        "required policy lost its exact destination");
	Require(catalog.Snapshot().find("Authorization=") == std::string::npos,
	        "safe snapshot rendered credential placement as a fixed header");
	Require(catalog.Snapshot().find("secret_name=") == std::string::npos,
	        "safe snapshot rendered a secret-binding identifier");
}

void TestOperationSelectorValidation() {
	const auto selector =
	    ConnectorCatalogTestAccess::OperationSelector({"zeta", "alpha"}, {{"gamma"}, {"beta", "alpha"}}, {"omega"}, 17);
	Require(selector.RequiredInputs() == std::vector<std::string>({"alpha", "zeta"}) &&
	            selector.AnyInputSets() == std::vector<std::vector<std::string>>({{"alpha", "beta"}, {"gamma"}}) &&
	            selector.ForbiddenInputs() == std::vector<std::string>({"omega"}) && selector.Priority() == 17,
	        "compiled selector did not canonicalize its immutable set facts");
	const auto negative_priority = ConnectorCatalogTestAccess::OperationSelector({}, {}, {}, -3);
	Require(negative_priority.Priority() == -3, "compiled selector rejected a valid signed priority");

	RequireInvalid("selector accepted an invalid required-input identifier",
	               []() { (void)ConnectorCatalogTestAccess::OperationSelector({"Bad-Input"}, {}, {}); });
	RequireInvalid("selector accepted duplicate required inputs",
	               []() { (void)ConnectorCatalogTestAccess::OperationSelector({"visibility", "visibility"}, {}, {}); });
	RequireInvalid("selector accepted an empty any-input alternative",
	               []() { (void)ConnectorCatalogTestAccess::OperationSelector({}, {{}}, {}); });
	RequireInvalid("selector accepted duplicate input within an alternative", []() {
		(void)ConnectorCatalogTestAccess::OperationSelector({}, {{"visibility", "visibility"}}, {});
	});
	RequireInvalid("selector accepted duplicate canonical alternatives", []() {
		(void)ConnectorCatalogTestAccess::OperationSelector(
		    {}, {{"repository_visibility", "visibility"}, {"visibility", "repository_visibility"}}, {});
	});
	RequireInvalid("selector both required and forbade an input",
	               []() { (void)ConnectorCatalogTestAccess::OperationSelector({"visibility"}, {}, {"visibility"}); });
	RequireInvalid("selector alternative contained a forbidden input",
	               []() { (void)ConnectorCatalogTestAccess::OperationSelector({}, {{"visibility"}}, {"visibility"}); });

	const auto catalog = BuildValidCatalogFixture();
	const auto &anonymous = catalog.Relations()[0];
	RequireInvalid("relation accepted a selector input absent from operation-scoped declarations", [&anonymous]() {
		auto operation = WithSelector(anonymous.Operation(), false,
		                              ConnectorCatalogTestAccess::OperationSelector({"phantom_input"}, {}, {}));
		ConnectorCatalogTestAccess::Relation(anonymous.Name(), anonymous.Columns(), std::move(operation),
		                                     anonymous.Authentication(), anonymous.ResourceCeilings());
	});
}

void TestClosedValidation() {
	const auto catalog = BuildValidCatalogFixture();
	const auto &anonymous = catalog.Relations()[0];
	const auto &authenticated = catalog.Relations()[1];

	const std::vector<std::string> invalid_hosts = {
	    "service.example:444",      "service.example/root", "service.example?pre=1",
	    "service.example#fragment", "user@service.example", "https://service.example:444/root?pre=1#fragment",
	    "Service.example",          ".service.example"};
	for (const auto &value : invalid_hosts) {
		RequireInvalid("CompiledRestHost accepted URL structure: " + value,
		               [value]() { duckdb_api::CompiledRestHost host(value); });
	}

	const duckdb_api::CompiledRestOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledRestHost("api.github.com"), 443};
	RequireInvalid("required bearer policy accepted an empty logical credential",
	               [origin]() { ConnectorCatalogTestAccess::ValidateRequiredBearer("", origin); });
	RequireInvalid("required bearer policy accepted an open-ended logical credential",
	               [origin]() { ConnectorCatalogTestAccess::ValidateRequiredBearer("password", origin); });
	RequireInvalid("required bearer policy accepted a cleartext destination", []() {
		const duckdb_api::CompiledRestOrigin destination = {duckdb_api::CompiledUrlScheme::HTTP,
		                                                    duckdb_api::CompiledRestHost("api.github.com"), 80};
		ConnectorCatalogTestAccess::ValidateRequiredBearer("token", destination);
	});
	RequireInvalid("required bearer policy accepted an alternate port", []() {
		const duckdb_api::CompiledRestOrigin destination = {duckdb_api::CompiledUrlScheme::HTTPS,
		                                                    duckdb_api::CompiledRestHost("api.github.com"), 444};
		ConnectorCatalogTestAccess::ValidateRequiredBearer("token", destination);
	});
	RequireInvalid("required bearer policy accepted an alternate host", []() {
		const duckdb_api::CompiledRestOrigin destination = {duckdb_api::CompiledUrlScheme::HTTPS,
		                                                    duckdb_api::CompiledRestHost("other.example"), 443};
		ConnectorCatalogTestAccess::ValidateRequiredBearer("token", destination);
	});

	RequireInvalid("relation accepted a fixed Authorization header", [&authenticated]() {
		auto operation = authenticated.Operation();
		operation.request.headers.push_back({"authorization", "x"});
		ConnectorCatalogTestAccess::Relation(authenticated.Name(), authenticated.Columns(), std::move(operation),
		                                     authenticated.Authentication(), authenticated.ResourceCeilings());
	});
	RequireInvalid("relation accepted root-object shape with zero-to-many cardinality", [&anonymous]() {
		auto operation = anonymous.Operation();
		operation.response_source = duckdb_api::CompiledResponseSource::ROOT_OBJECT;
		operation.records_extractor = "$";
		ConnectorCatalogTestAccess::Relation(anonymous.Name(), anonymous.Columns(), std::move(operation),
		                                     anonymous.Authentication(), anonymous.ResourceCeilings());
	});
	RequireInvalid("relation accepted multi-record shape with exactly-one cardinality", [&authenticated]() {
		auto operation = authenticated.Operation();
		operation.response_source = duckdb_api::CompiledResponseSource::JSON_PATH_MANY;
		operation.records_extractor = "$.items[*]";
		ConnectorCatalogTestAccess::Relation(authenticated.Name(), authenticated.Columns(), std::move(operation),
		                                     authenticated.Authentication(), authenticated.ResourceCeilings());
	});
	RequireInvalid("relation accepted root-array shape with exactly-one cardinality", [&authenticated]() {
		auto operation = authenticated.Operation();
		operation.response_source = duckdb_api::CompiledResponseSource::ROOT_ARRAY;
		operation.records_extractor = "$";
		ConnectorCatalogTestAccess::Relation(authenticated.Name(), authenticated.Columns(), std::move(operation),
		                                     authenticated.Authentication(), authenticated.ResourceCeilings());
	});
	RequireInvalid("relation inferred a root array from an extractor", [&anonymous]() {
		auto operation = anonymous.Operation();
		operation.response_source = duckdb_api::CompiledResponseSource::ROOT_ARRAY;
		operation.records_extractor = "$[*]";
		ConnectorCatalogTestAccess::Relation(anonymous.Name(), anonymous.Columns(), std::move(operation),
		                                     anonymous.Authentication(), anonymous.ResourceCeilings());
	});
	RequireInvalid("exactly-one relation accepted a wider record ceiling", [&authenticated]() {
		ConnectorCatalogTestAccess::Relation(authenticated.Name(), authenticated.Columns(), authenticated.Operation(),
		                                     authenticated.Authentication(),
		                                     ConnectorCatalogTestAccess::UnpaginatedResources(2, 64));
	});
	RequireInvalid("authenticated relation accepted a query-bearing request", [&authenticated]() {
		auto operation = authenticated.Operation();
		operation.request.query_parameters.push_back({"page", "1"});
		ConnectorCatalogTestAccess::Relation(authenticated.Name(), authenticated.Columns(), std::move(operation),
		                                     authenticated.Authentication(), authenticated.ResourceCeilings());
	});
	RequireInvalid("authenticated relation accepted a mismatched credential destination", [&authenticated]() {
		auto operation = authenticated.Operation();
		operation.request.origin.host = duckdb_api::CompiledRestHost("other.example");
		ConnectorCatalogTestAccess::Relation(authenticated.Name(), authenticated.Columns(), std::move(operation),
		                                     ConnectorCatalogTestAccess::RequiredBearer(),
		                                     authenticated.ResourceCeilings());
	});
	RequireInvalid("relation accepted duplicate output columns", [&anonymous]() {
		auto columns = anonymous.Columns();
		columns.push_back(columns[0]);
		ConnectorCatalogTestAccess::Relation(anonymous.Name(), std::move(columns), anonymous.Operation(),
		                                     anonymous.Authentication(), anonymous.ResourceCeilings());
	});
	RequireInvalid("relation accepted an invalid request path", [&anonymous]() {
		auto operation = anonymous.Operation();
		operation.request.path = "/rows?escape=1";
		ConnectorCatalogTestAccess::Relation(anonymous.Name(), anonymous.Columns(), std::move(operation),
		                                     anonymous.Authentication(), anonymous.ResourceCeilings());
	});
	RequireInvalid("relation accepted header injection", [&anonymous]() {
		auto operation = anonymous.Operation();
		operation.request.headers[0].value = "value\r\ninjected";
		ConnectorCatalogTestAccess::Relation(anonymous.Name(), anonymous.Columns(), std::move(operation),
		                                     anonymous.Authentication(), anonymous.ResourceCeilings());
	});
	RequireInvalid("relation accepted query injection", [&anonymous]() {
		auto operation = anonymous.Operation();
		operation.request.query_parameters.push_back({"page", "value&injected=1"});
		ConnectorCatalogTestAccess::Relation(anonymous.Name(), anonymous.Columns(), std::move(operation),
		                                     anonymous.Authentication(), anonymous.ResourceCeilings());
	});
	RequireInvalid("catalog accepted duplicate relation identifiers", [&catalog]() {
		std::vector<duckdb_api::CompiledRelation> relations = {catalog.Relations()[0], catalog.Relations()[0]};
		ConnectorCatalogTestAccess::Catalog(catalog.Origin(), catalog.ConnectorName(), catalog.Version(),
		                                    std::move(relations), catalog.NetworkPolicy());
	});
	RequireInvalid("catalog accepted a destination outside its network policy", [&catalog]() {
		auto policy = catalog.NetworkPolicy();
		policy.allowed_hosts = {"other.example"};
		ConnectorCatalogTestAccess::Catalog(catalog.Origin(), catalog.ConnectorName(), catalog.Version(),
		                                    catalog.Relations(), std::move(policy));
	});
	RequireInvalid("relation accepted an empty operation collection", [&anonymous]() {
		ConnectorCatalogTestAccess::Relation(anonymous.Name(), anonymous.Columns(),
		                                     std::vector<duckdb_api::CompiledOperation> {}, anonymous.Authentication(),
		                                     anonymous.ResourceCeilings());
	});
	RequireInvalid("relation accepted duplicate operation identifiers", [&anonymous]() {
		std::vector<duckdb_api::CompiledOperation> operations = {anonymous.Operation(), anonymous.Operation()};
		operations[1].request.path = "/other-rows";
		ConnectorCatalogTestAccess::Relation(anonymous.Name(), anonymous.Columns(), std::move(operations),
		                                     anonymous.Authentication(), anonymous.ResourceCeilings());
	});
	RequireInvalid("catalog ignored a later operation destination outside network policy", [&catalog, &anonymous]() {
		auto second = anonymous.Operation();
		second.name = "fixture_other_rows";
		second.fallback = false;
		second.request.origin.host = duckdb_api::CompiledRestHost("other.example");
		std::vector<duckdb_api::CompiledOperation> operations = {anonymous.Operation(), std::move(second)};
		std::vector<duckdb_api::CompiledRelation> relations;
		relations.push_back(ConnectorCatalogTestAccess::Relation(anonymous.Name(), anonymous.Columns(),
		                                                         std::move(operations), anonymous.Authentication(),
		                                                         anonymous.ResourceCeilings()));
		ConnectorCatalogTestAccess::Catalog(catalog.Origin(), catalog.ConnectorName(), catalog.Version(),
		                                    std::move(relations), catalog.NetworkPolicy());
	});
}

} // namespace

namespace duckdb_api_test {

void RunConnectorCatalogContractTests() {
	TestSafeImmutableService();
	TestOperationSelectorValidation();
	TestClosedValidation();
}

} // namespace duckdb_api_test
