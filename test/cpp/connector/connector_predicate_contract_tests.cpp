#include "connector/support/catalog_test_access.hpp"
#include "connector/support/predicate_contract.hpp"
#include "support/require.hpp"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using duckdb_api_test::ConnectorCatalogTestAccess;
using duckdb_api_test::Require;

static_assert(std::is_copy_constructible<duckdb_api::CompiledPredicateMapping>::value,
              "immutable predicate mappings must support catalog copies");
static_assert(std::is_move_constructible<duckdb_api::CompiledPredicateMapping>::value,
              "immutable predicate mappings must support ownership transfer");
static_assert(!std::is_copy_assignable<duckdb_api::CompiledPredicateMapping>::value,
              "predicate mapping assignment would permit post-construction replacement");
static_assert(!std::is_move_assignable<duckdb_api::CompiledPredicateMapping>::value,
              "predicate mapping assignment would permit post-construction replacement");
static_assert(!std::is_default_constructible<duckdb_api::CompiledPredicateMapping>::value,
              "predicate mappings must not admit partial construction");
static_assert(
    !std::is_constructible<duckdb_api::CompiledPredicateMapping, std::string, duckdb_api::CompiledPredicateOperator,
                           duckdb_api::CompiledPredicateLiteral, std::string,
                           duckdb_api::CompiledPredicateInputPlacement, std::string, std::string,
                           duckdb_api::CompiledPredicateAccuracy, duckdb_api::CompiledPredicateEvidence>::value,
    "production callers must not construct predicate declarations");

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

duckdb_api::CompiledPredicateMapping
Mapping(std::string column_name = "visibility",
        duckdb_api::CompiledPredicateOperator predicate_operator = duckdb_api::CompiledPredicateOperator::EQUALS,
        duckdb_api::CompiledPredicateLiteral literal = duckdb_api::CompiledPredicateLiteral::VARCHAR_PRIVATE,
        std::string operation_name = "github_authenticated_repositories",
        duckdb_api::CompiledPredicateInputPlacement placement =
            duckdb_api::CompiledPredicateInputPlacement::REST_QUERY_PARAMETER,
        std::string input_name = "visibility", std::string encoded_value = "private",
        duckdb_api::CompiledPredicateAccuracy accuracy = duckdb_api::CompiledPredicateAccuracy::SUPERSET,
        duckdb_api::CompiledPredicateEvidence evidence =
            duckdb_api::CompiledPredicateEvidence::GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY) {
	return ConnectorCatalogTestAccess::PredicateMapping(std::move(column_name), predicate_operator, literal,
	                                                    std::move(operation_name), placement, std::move(input_name),
	                                                    std::move(encoded_value), accuracy, evidence);
}

duckdb_api::CompiledOperation
Operation(std::vector<duckdb_api::CompiledQueryParameter> extra_query = {},
          duckdb_api::CompiledPagination pagination = ConnectorCatalogTestAccess::SequentialLink("per_page", 100,
                                                                                                 "page", 1, 1, 32)) {
	const duckdb_api::CompiledRestOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledRestHost("api.github.com"), 443};
	std::vector<duckdb_api::CompiledQueryParameter> query = {{"per_page", "100"}, {"page", "1"}};
	query.insert(query.end(), extra_query.begin(), extra_query.end());
	return duckdb_api::CompiledOperation {"github_authenticated_repositories",
	                                      true,
	                                      duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	                                      duckdb_api::CompiledProtocol::REST,
	                                      duckdb_api::CompiledHttpMethod::GET,
	                                      duckdb_api::CompiledReplaySafety::SAFE,
	                                      false,
	                                      std::move(pagination),
	                                      {origin,
	                                       "/user/repos",
	                                       std::move(query),
	                                       {{"Accept", "application/vnd.github+json"},
	                                        {"User-Agent", "duckdb-api/0.6.0"},
	                                        {"X-GitHub-Api-Version", "2022-11-28"}}},
	                                      duckdb_api::CompiledResponseSource::ROOT_ARRAY,
	                                      "$"};
}

duckdb_api::CompiledRelation
Relation(std::vector<duckdb_api::CompiledColumn> columns, duckdb_api::CompiledOperation operation,
         std::vector<duckdb_api::CompiledPredicateMapping> mappings,
         duckdb_api::CompiledAuthenticationPolicy authentication = ConnectorCatalogTestAccess::RequiredBearer()) {
	return ConnectorCatalogTestAccess::Relation(
	    "authenticated_repositories", std::move(columns), std::move(operation), std::move(authentication),
	    ConnectorCatalogTestAccess::PaginatedResources(4096, 4096 * 32, 100, 3200, 512), std::move(mappings));
}

std::vector<duckdb_api::CompiledColumn> Columns() {
	return {{"id", "BIGINT", false, "$.id"}, {"visibility", "VARCHAR", false, "$.visibility"}};
}

void TestClosedValueAndExplanation() {
	const auto mapping = Mapping();
	Require(mapping.ColumnName() == "visibility", "predicate column drifted");
	Require(mapping.Operator() == duckdb_api::CompiledPredicateOperator::EQUALS, "predicate operator drifted");
	Require(mapping.Literal() == duckdb_api::CompiledPredicateLiteral::VARCHAR_PRIVATE,
	        "predicate typed literal drifted");
	Require(mapping.OperationName() == "github_authenticated_repositories", "predicate operation drifted");
	Require(mapping.InputPlacement() == duckdb_api::CompiledPredicateInputPlacement::REST_QUERY_PARAMETER,
	        "predicate input placement drifted");
	Require(mapping.RemoteInputName() == "visibility" && mapping.EncodedRemoteValue() == "private",
	        "predicate remote input drifted");
	Require(mapping.Accuracy() == duckdb_api::CompiledPredicateAccuracy::SUPERSET, "predicate accuracy drifted");
	Require(mapping.Evidence() == duckdb_api::CompiledPredicateEvidence::GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY,
	        "predicate evidence identity drifted");

	auto relation = Relation(Columns(), Operation(), {mapping});
	Require(relation.PredicateMappings().size() == 1, "validated relation lost its predicate mapping");
	const auto snapshot = relation.Snapshot();
	Require(snapshot.find("predicate_mappings=[{column:visibility,operator:equals,literal:varchar:private,") !=
	            std::string::npos,
	        "predicate explanation lost its typed shape");
	Require(snapshot.find("input:rest_query:visibility=private,accuracy:superset,") != std::string::npos,
	        "predicate explanation lost its remote input or conservative accuracy");
	Require(snapshot.find("evidence:github_rest_2022_11_28_repository_visibility}") != std::string::npos,
	        "predicate explanation lost its accepted evidence identity");
	for (const auto &prohibited : {"SELECT ", "secret_name=", "credential_value=", "Authorization=", "Link="}) {
		Require(snapshot.find(prohibited) == std::string::npos,
		        "predicate explanation contains prohibited state: " + std::string(prohibited));
	}
}

void TestInvalidValuesAndBindings() {
	RequireInvalid("predicate mapping accepted an unknown operator",
	               []() { Mapping("visibility", static_cast<duckdb_api::CompiledPredicateOperator>(255)); });
	RequireInvalid("predicate mapping accepted an unknown literal", []() {
		Mapping("visibility", duckdb_api::CompiledPredicateOperator::EQUALS,
		        static_cast<duckdb_api::CompiledPredicateLiteral>(255));
	});
	RequireInvalid("predicate mapping accepted an unknown placement", []() {
		Mapping("visibility", duckdb_api::CompiledPredicateOperator::EQUALS,
		        duckdb_api::CompiledPredicateLiteral::VARCHAR_PRIVATE, "github_authenticated_repositories",
		        static_cast<duckdb_api::CompiledPredicateInputPlacement>(255));
	});
	RequireInvalid("predicate mapping accepted an unknown accuracy", []() {
		Mapping("visibility", duckdb_api::CompiledPredicateOperator::EQUALS,
		        duckdb_api::CompiledPredicateLiteral::VARCHAR_PRIVATE, "github_authenticated_repositories",
		        duckdb_api::CompiledPredicateInputPlacement::REST_QUERY_PARAMETER, "visibility", "private",
		        static_cast<duckdb_api::CompiledPredicateAccuracy>(255));
	});
	RequireInvalid("predicate mapping accepted an unknown evidence identity", []() {
		Mapping("visibility", duckdb_api::CompiledPredicateOperator::EQUALS,
		        duckdb_api::CompiledPredicateLiteral::VARCHAR_PRIVATE, "github_authenticated_repositories",
		        duckdb_api::CompiledPredicateInputPlacement::REST_QUERY_PARAMETER, "visibility", "private",
		        duckdb_api::CompiledPredicateAccuracy::SUPERSET,
		        static_cast<duckdb_api::CompiledPredicateEvidence>(255));
	});
	RequireInvalid("predicate mapping accepted encoded query injection", []() {
		Mapping("visibility", duckdb_api::CompiledPredicateOperator::EQUALS,
		        duckdb_api::CompiledPredicateLiteral::VARCHAR_PRIVATE, "github_authenticated_repositories",
		        duckdb_api::CompiledPredicateInputPlacement::REST_QUERY_PARAMETER, "visibility", "private&x=1");
	});

	RequireInvalid("relation accepted a predicate mapping without its column",
	               []() { Relation({{"id", "BIGINT", false, "$.id"}}, Operation(), {Mapping()}); });
	RequireInvalid("relation accepted a nullable predicate column",
	               []() { Relation({{"visibility", "VARCHAR", true, "$.visibility"}}, Operation(), {Mapping()}); });
	RequireInvalid("relation accepted a predicate column with the wrong type",
	               []() { Relation({{"visibility", "BOOLEAN", false, "$.visibility"}}, Operation(), {Mapping()}); });
	RequireInvalid("relation accepted a predicate column with the wrong extractor",
	               []() { Relation({{"visibility", "VARCHAR", false, "$.private"}}, Operation(), {Mapping()}); });
	RequireInvalid("relation accepted an exact decoy for the conservative profile", []() {
		Relation(Columns(), Operation(),
		         {Mapping("visibility", duckdb_api::CompiledPredicateOperator::EQUALS,
		                  duckdb_api::CompiledPredicateLiteral::VARCHAR_PRIVATE, "github_authenticated_repositories",
		                  duckdb_api::CompiledPredicateInputPlacement::REST_QUERY_PARAMETER, "visibility", "private",
		                  duckdb_api::CompiledPredicateAccuracy::EXACT)});
	});
	RequireInvalid("relation accepted a broader-boolean column decoy", []() {
		auto columns = Columns();
		columns.push_back({"private", "BOOLEAN", false, "$.private"});
		Relation(std::move(columns), Operation(), {Mapping("private")});
	});
	RequireInvalid("relation accepted a legacy-type input decoy", []() {
		Relation(Columns(), Operation(),
		         {Mapping("visibility", duckdb_api::CompiledPredicateOperator::EQUALS,
		                  duckdb_api::CompiledPredicateLiteral::VARCHAR_PRIVATE, "github_authenticated_repositories",
		                  duckdb_api::CompiledPredicateInputPlacement::REST_QUERY_PARAMETER, "type")});
	});
	RequireInvalid("relation accepted a mismatched operation mapping", []() {
		Relation(Columns(), Operation(),
		         {Mapping("visibility", duckdb_api::CompiledPredicateOperator::EQUALS,
		                  duckdb_api::CompiledPredicateLiteral::VARCHAR_PRIVATE, "other_operation")});
	});
	RequireInvalid("relation accepted a fixed visibility collision",
	               []() { Relation(Columns(), Operation({{"visibility", "all"}}), {Mapping()}); });
	RequireInvalid("relation accepted a fixed legacy-type conflict",
	               []() { Relation(Columns(), Operation({{"type", "all"}}), {Mapping()}); });
	RequireInvalid("relation accepted a pagination-field collision", []() {
		auto pagination = ConnectorCatalogTestAccess::SequentialLink("per_page", 100, "visibility", 1, 1, 2);
		Relation(Columns(), Operation({{"visibility", "1"}}, std::move(pagination)), {Mapping()});
	});
	RequireInvalid("relation accepted duplicate predicate mappings",
	               []() { Relation(Columns(), Operation(), {Mapping(), Mapping()}); });
}

void TestEvidenceIsBoundToCanonicalOperation() {
	RequireInvalid("accepted evidence escaped its repository path", []() {
		auto operation = Operation();
		operation.request.path = "/other/repos";
		Relation(Columns(), std::move(operation), {Mapping()});
	});
	RequireInvalid("accepted evidence escaped its HTTPS origin", []() {
		auto operation = Operation();
		operation.request.origin.scheme = duckdb_api::CompiledUrlScheme::HTTP;
		Relation(Columns(), std::move(operation), {Mapping()});
	});
	RequireInvalid("accepted evidence escaped its GitHub host", []() {
		auto operation = Operation();
		operation.request.origin.host = duckdb_api::CompiledRestHost("uploads.github.com");
		Relation(Columns(), std::move(operation), {Mapping()});
	});
	RequireInvalid("accepted evidence escaped its GitHub port", []() {
		auto operation = Operation();
		operation.request.origin.port = 8443;
		Relation(Columns(), std::move(operation), {Mapping()});
	});
	RequireInvalid("accepted evidence escaped its API version", []() {
		auto operation = Operation();
		operation.request.headers.back().value = "2023-01-01";
		Relation(Columns(), std::move(operation), {Mapping()});
	});
	RequireInvalid("accepted evidence escaped its response source", []() {
		auto operation = Operation();
		operation.response_source = duckdb_api::CompiledResponseSource::JSON_PATH_MANY;
		operation.records_extractor = "$.items";
		Relation(Columns(), std::move(operation), {Mapping()});
	});
	RequireInvalid("accepted evidence escaped its Link pagination fields", []() {
		auto pagination = ConnectorCatalogTestAccess::SequentialLink("per_page", 100, "next_page", 1, 1, 32);
		Relation(Columns(), Operation({}, std::move(pagination)), {Mapping()});
	});
	RequireInvalid("accepted evidence escaped required bearer policy",
	               []() { Relation(Columns(), Operation(), {Mapping()}, ConnectorCatalogTestAccess::Anonymous()); });
}

void TestAbsentMappingIsExplicit() {
	const auto relation = Relation(Columns(), Operation(), {});
	Require(relation.PredicateMappings().empty(),
	        "absent predicate mapping was fabricated from names or request shape");
	Require(relation.Snapshot().find("predicate_mappings=[]") != std::string::npos,
	        "safe explanation did not distinguish an absent mapping");
}

} // namespace

namespace duckdb_api_test {

void RunConnectorPredicateContractTests() {
	TestClosedValueAndExplanation();
	TestInvalidValuesAndBindings();
	TestEvidenceIsBoundToCanonicalOperation();
	TestAbsentMappingIsExplicit();
}

} // namespace duckdb_api_test
