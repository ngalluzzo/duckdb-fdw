#include "duckdb_api/connector.hpp"
#include "connector/support/catalog_test_access.hpp"
#include "connector/support/predicate_contract.hpp"
#include "support/require.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using duckdb_api_test::ConnectorCatalogTestAccess;
using duckdb_api_test::Require;

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
Mapping(std::string operation_name, duckdb_api::CompiledPredicateAccuracy accuracy,
        duckdb_api::CompiledPredicateProofIdentity proof_identity, duckdb_api::CompiledPredicateBaseDomain base_domain,
        duckdb_api::CompiledPredicateOccurrencePreservation occurrence_preservation,
        std::string remote_input_name = "visibility", std::string encoded_value = "private",
        duckdb_api::CompiledPredicateEncodingCapability encoding_capability =
            duckdb_api::CompiledPredicateEncodingCapability::SINGLE_POSITIVE_REST_QUERY_INPUT) {
	return ConnectorCatalogTestAccess::PredicateMapping(
	    "visibility", duckdb_api::CompiledPredicateOperator::EQUALS,
	    duckdb_api::CompiledPredicateLiteral::VARCHAR_PRIVATE, std::move(operation_name),
	    duckdb_api::CompiledPredicateInputPlacement::REST_QUERY_PARAMETER, std::move(remote_input_name),
	    std::move(encoded_value), accuracy, proof_identity, base_domain, occurrence_preservation, encoding_capability);
}

duckdb_api::CompiledPredicateMapping
GithubMapping(duckdb_api::CompiledPredicateAccuracy accuracy = duckdb_api::CompiledPredicateAccuracy::SUPERSET,
              duckdb_api::CompiledPredicateProofIdentity proof_identity =
                  duckdb_api::CompiledPredicateProofIdentity::GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY,
              duckdb_api::CompiledPredicateBaseDomain base_domain =
                  duckdb_api::CompiledPredicateBaseDomain::GITHUB_AUTHENTICATED_REPOSITORY_OCCURRENCES,
              duckdb_api::CompiledPredicateOccurrencePreservation occurrence_preservation =
                  duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_ALL_MATCHING_BASE_OCCURRENCES,
              std::string remote_input_name = "visibility") {
	return Mapping("github_authenticated_repositories", accuracy, proof_identity, base_domain, occurrence_preservation,
	               std::move(remote_input_name));
}

duckdb_api::CompiledPredicateMapping ControlledExactMapping(
    duckdb_api::CompiledPredicateAccuracy accuracy = duckdb_api::CompiledPredicateAccuracy::EXACT,
    duckdb_api::CompiledPredicateProofIdentity proof_identity =
        duckdb_api::CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY,
    duckdb_api::CompiledPredicateBaseDomain base_domain =
        duckdb_api::CompiledPredicateBaseDomain::CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES,
    duckdb_api::CompiledPredicateOccurrencePreservation occurrence_preservation =
        duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES,
    std::string remote_input_name = "visibility") {
	return Mapping("controlled_exact_repositories", accuracy, proof_identity, base_domain, occurrence_preservation,
	               std::move(remote_input_name));
}

duckdb_api::CompiledOperation ControlledExactOperation() {
	const duckdb_api::CompiledRestOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledRestHost("predicate-proof.invalid"), 443};
	return duckdb_api::CompiledOperation {
	    "controlled_exact_repositories",
	    true,
	    duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	    duckdb_api::CompiledProtocol::REST,
	    duckdb_api::CompiledHttpMethod::GET,
	    duckdb_api::CompiledReplaySafety::SAFE,
	    false,
	    ConnectorCatalogTestAccess::DisabledPagination(),
	    {origin, "/fixtures/exact-repositories", {}, {{"X-Connector-Fixture", "exact-duplicate-repositories"}}},
	    duckdb_api::CompiledResponseSource::ROOT_ARRAY,
	    "$",
	    duckdb_api::CompiledOperationSelector()};
}

std::vector<duckdb_api::CompiledColumn> ControlledExactColumns() {
	return {{"occurrence_id", "BIGINT", false, "$.occurrence_id"}, {"visibility", "VARCHAR", false, "$.visibility"}};
}

duckdb_api::CompiledRelation ControlledExactRelation(duckdb_api::CompiledOperation operation,
                                                     duckdb_api::CompiledPredicateMapping mapping) {
	return ConnectorCatalogTestAccess::Relation("controlled_exact_repositories", ControlledExactColumns(),
	                                            std::move(operation), ConnectorCatalogTestAccess::Anonymous(),
	                                            ConnectorCatalogTestAccess::UnpaginatedResources(8, 128),
	                                            {std::move(mapping)});
}

void TestDistinctControlledExactProfile() {
	const auto relation = ControlledExactRelation(ControlledExactOperation(), ControlledExactMapping());
	Require(relation.PredicateMappings().size() == 1, "controlled exact profile did not pass production validation");
	const auto &mapping = relation.PredicateMappings()[0];
	Require(mapping.Accuracy() == duckdb_api::CompiledPredicateAccuracy::EXACT &&
	            mapping.ProofIdentity() ==
	                duckdb_api::CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY &&
	            mapping.BaseDomain() ==
	                duckdb_api::CompiledPredicateBaseDomain::CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES &&
	            mapping.OccurrencePreservation() ==
	                duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES,
	        "controlled exact profile lost its distinct proof or occurrence contract");
	const auto alternate = ControlledExactRelation(
	    ControlledExactOperation(),
	    ControlledExactMapping(
	        duckdb_api::CompiledPredicateAccuracy::EXACT,
	        duckdb_api::CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY,
	        duckdb_api::CompiledPredicateBaseDomain::CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES,
	        duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES,
	        "repository_visibility"));
	Require(alternate.PredicateMappings()[0].RemoteInputName() == "repository_visibility",
	        "controlled exact profile rejected its second closed safe encoding");
}

void TestGithubAccuracyCannotBeRelabeled() {
	const auto catalog = duckdb_api::BuildNativeGithubConnector();
	const auto *relation = catalog.FindRelation("authenticated_repositories");
	Require(relation != nullptr, "native repository relation disappeared");
	RequireInvalid("GitHub proof identity accepted relabeled exact accuracy", [relation]() {
		ConnectorCatalogTestAccess::Relation(relation->Name(), relation->Columns(), relation->Operation(),
		                                     relation->Authentication(), relation->ResourceCeilings(),
		                                     {GithubMapping(duckdb_api::CompiledPredicateAccuracy::EXACT)});
	});
	RequireInvalid("GitHub operation accepted the controlled exact proof identity", [relation]() {
		ConnectorCatalogTestAccess::Relation(
		    relation->Name(), relation->Columns(), relation->Operation(), relation->Authentication(),
		    relation->ResourceCeilings(),
		    {GithubMapping(
		        duckdb_api::CompiledPredicateAccuracy::SUPERSET,
		        duckdb_api::CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY)});
	});
	RequireInvalid("GitHub proof escaped to a controlled base domain", [relation]() {
		ConnectorCatalogTestAccess::Relation(
		    relation->Name(), relation->Columns(), relation->Operation(), relation->Authentication(),
		    relation->ResourceCeilings(),
		    {GithubMapping(duckdb_api::CompiledPredicateAccuracy::SUPERSET,
		                   duckdb_api::CompiledPredicateProofIdentity::GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY,
		                   duckdb_api::CompiledPredicateBaseDomain::CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES)});
	});
	RequireInvalid("GitHub proof claimed exact selected-occurrence preservation", [relation]() {
		ConnectorCatalogTestAccess::Relation(
		    relation->Name(), relation->Columns(), relation->Operation(), relation->Authentication(),
		    relation->ResourceCeilings(),
		    {GithubMapping(
		        duckdb_api::CompiledPredicateAccuracy::SUPERSET,
		        duckdb_api::CompiledPredicateProofIdentity::GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY,
		        duckdb_api::CompiledPredicateBaseDomain::GITHUB_AUTHENTICATED_REPOSITORY_OCCURRENCES,
		        duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES)});
	});
	RequireInvalid("installed GitHub proof accepted the controlled alternate input encoding", [relation]() {
		ConnectorCatalogTestAccess::Relation(
		    relation->Name(), relation->Columns(), relation->Operation(), relation->Authentication(),
		    relation->ResourceCeilings(),
		    {GithubMapping(duckdb_api::CompiledPredicateAccuracy::SUPERSET,
		                   duckdb_api::CompiledPredicateProofIdentity::GITHUB_REST_2022_11_28_REPOSITORY_VISIBILITY,
		                   duckdb_api::CompiledPredicateBaseDomain::GITHUB_AUTHENTICATED_REPOSITORY_OCCURRENCES,
		                   duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_ALL_MATCHING_BASE_OCCURRENCES,
		                   "repository_visibility")});
	});
}

void TestControlledProfileRejectsDomainOccurrenceAndEncodingDrift() {
	RequireInvalid("controlled exact proof accepted superset accuracy", []() {
		ControlledExactRelation(ControlledExactOperation(),
		                        ControlledExactMapping(duckdb_api::CompiledPredicateAccuracy::SUPERSET));
	});
	RequireInvalid("controlled exact proof escaped to the GitHub base domain", []() {
		ControlledExactRelation(
		    ControlledExactOperation(),
		    ControlledExactMapping(
		        duckdb_api::CompiledPredicateAccuracy::EXACT,
		        duckdb_api::CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY,
		        duckdb_api::CompiledPredicateBaseDomain::GITHUB_AUTHENTICATED_REPOSITORY_OCCURRENCES));
	});
	RequireInvalid("controlled exact proof accepted only superset occurrence preservation", []() {
		ControlledExactRelation(
		    ControlledExactOperation(),
		    ControlledExactMapping(
		        duckdb_api::CompiledPredicateAccuracy::EXACT,
		        duckdb_api::CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY,
		        duckdb_api::CompiledPredicateBaseDomain::CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES,
		        duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_ALL_MATCHING_BASE_OCCURRENCES));
	});
	RequireInvalid("controlled exact proof accepted another base-domain path", []() {
		auto operation = ControlledExactOperation();
		auto rest = operation.Rest();
		rest.request.path = "/fixtures/other-repositories";
		auto changed = ConnectorCatalogTestAccess::RestOperation(operation, std::move(rest), operation.selector);
		ControlledExactRelation(std::move(changed), ControlledExactMapping());
	});
	RequireInvalid("controlled exact proof accepted a multiplicity-changing operation identity", []() {
		auto operation = ControlledExactOperation();
		operation.name = "controlled_distinct_repositories";
		ControlledExactRelation(
		    std::move(operation),
		    Mapping("controlled_distinct_repositories", duckdb_api::CompiledPredicateAccuracy::EXACT,
		            duckdb_api::CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY,
		            duckdb_api::CompiledPredicateBaseDomain::CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES,
		            duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES));
	});
	RequireInvalid("controlled exact proof accepted an occurrence-multiplying operation identity", []() {
		auto operation = ControlledExactOperation();
		operation.name = "controlled_duplicating_repositories";
		ControlledExactRelation(
		    std::move(operation),
		    Mapping("controlled_duplicating_repositories", duckdb_api::CompiledPredicateAccuracy::EXACT,
		            duckdb_api::CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY,
		            duckdb_api::CompiledPredicateBaseDomain::CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES,
		            duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES));
	});
	RequireInvalid("controlled exact proof accepted a different response occurrence source", []() {
		auto operation = ControlledExactOperation();
		auto rest = operation.Rest();
		rest.response_source = duckdb_api::CompiledResponseSource::JSON_PATH_MANY;
		rest.records_extractor = "$.distinct[*]";
		auto changed = ConnectorCatalogTestAccess::RestOperation(operation, std::move(rest), operation.selector);
		ControlledExactRelation(std::move(changed), ControlledExactMapping());
	});
	RequireInvalid("controlled exact proof accepted an incompatible encoded input", []() {
		ControlledExactRelation(
		    ControlledExactOperation(),
		    Mapping("controlled_exact_repositories", duckdb_api::CompiledPredicateAccuracy::EXACT,
		            duckdb_api::CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY,
		            duckdb_api::CompiledPredicateBaseDomain::CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES,
		            duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES,
		            "visibility", "public"));
	});
	RequireInvalid("controlled exact proof accepted an unreviewed input encoding", []() {
		ControlledExactRelation(
		    ControlledExactOperation(),
		    ControlledExactMapping(
		        duckdb_api::CompiledPredicateAccuracy::EXACT,
		        duckdb_api::CompiledPredicateProofIdentity::CONTROLLED_EXACT_DUPLICATE_REPOSITORY_VISIBILITY,
		        duckdb_api::CompiledPredicateBaseDomain::CONTROLLED_DUPLICATE_REPOSITORY_OCCURRENCES,
		        duckdb_api::CompiledPredicateOccurrencePreservation::PRESERVES_EXACT_MATCHING_BASE_OCCURRENCES,
		        "visibility_alias"));
	});
	RequireInvalid("controlled exact proof accepted a fixed-field encoding collision", []() {
		auto operation = ControlledExactOperation();
		auto rest = operation.Rest();
		rest.request.query_parameters.push_back({"visibility", "all"});
		auto changed = ConnectorCatalogTestAccess::RestOperation(operation, std::move(rest), operation.selector);
		ControlledExactRelation(std::move(changed), ControlledExactMapping());
	});
}

} // namespace

namespace duckdb_api_test {

void RunConnectorPredicateProofContractTests() {
	TestDistinctControlledExactProfile();
	TestGithubAccuracyCannotBeRelabeled();
	TestControlledProfileRejectsDomainOccurrenceAndEncodingDrift();
}

} // namespace duckdb_api_test
