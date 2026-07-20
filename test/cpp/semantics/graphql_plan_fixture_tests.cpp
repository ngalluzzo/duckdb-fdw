#include "semantics/support/graphql_semantics_test_cases.hpp"

#include "duckdb_api/content_digest.hpp"
#include "semantics/support/graphql_plan_equality.hpp"
#include "semantics/support/graphql_plan_test_variations.hpp"
#include "semantics/support/graphql_protocol_test_inspection.hpp"
#include "semantics/support/graphql_scan_plan_test_fixtures.hpp"
#include "support/require.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace duckdb_api_test {
namespace graphql_semantics {

void TestFixtureBoundary() {
	const auto production = BuildProductionPlan();
	const auto fixture = BuildValidGraphqlScanPlanFixture("graphql_semantics_secret");
	Require(CountGraphqlPlanDifferences(fixture, production) == 0,
	        "Semantics GraphQL fixture drifted from a public production plan fact");
	Require(fixture.SourceSnapshot() != production.SourceSnapshot() && fixture.Snapshot() == production.Snapshot(),
	        "typed GraphQL fixture agreement improperly depends on Connector source prose");

	const auto profile_count = static_cast<std::size_t>(GraphqlLocalResidualProfile::COUNT);
	Require(profile_count == 4, "closed valid GraphQL local-residual catalog changed without self-test review");
	for (std::size_t value = 0; value < profile_count; value++) {
		const auto profile = static_cast<GraphqlLocalResidualProfile>(value);
		const auto profiled_fixture = BuildValidGraphqlScanPlanFixture("graphql_semantics_secret", profile);
		const auto profiled_production = BuildProductionPlan(profile);
		Require(CountGraphqlPlanDifferences(profiled_fixture, profiled_production) == 0,
		        "valid GraphQL local-residual fixture " + std::to_string(value) +
		            " drifted from its production planner result");
		Require(profiled_fixture.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
		            profiled_fixture.RemoteAccuracy() == duckdb_api::RemotePredicateAccuracy::UNSUPPORTED &&
		            profiled_fixture.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB &&
		            profiled_fixture.ConditionalInput() == duckdb_api::PlannedConditionalInput::NONE &&
		            profiled_fixture.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
		            profiled_fixture.Ownership().projection == duckdb_api::RelationalOwner::DUCKDB &&
		            profiled_fixture.Ownership().ordering == duckdb_api::RelationalOwner::DUCKDB &&
		            profiled_fixture.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB &&
		            profiled_fixture.Ownership().offset == duckdb_api::RelationalOwner::DUCKDB &&
		            profiled_fixture.RemoteOrdering() == duckdb_api::RelationalDelegation::NONE &&
		            profiled_fixture.RuntimeOrdering() == duckdb_api::RelationalDelegation::NONE &&
		            profiled_fixture.RemoteLimit() == duckdb_api::RelationalDelegation::NONE &&
		            profiled_fixture.RemoteOffset() == duckdb_api::RelationalDelegation::NONE &&
		            profiled_fixture.RuntimeLimit() == duckdb_api::RelationalDelegation::NONE &&
		            profiled_fixture.RuntimeOffset() == duckdb_api::RelationalDelegation::NONE,
		        "valid GraphQL local-residual fixture conferred executable remote or Runtime authority");
	}
	bool profile_sentinel_rejected = false;
	try {
		(void)BuildValidGraphqlScanPlanFixture("graphql_semantics_secret", GraphqlLocalResidualProfile::COUNT);
	} catch (const std::invalid_argument &) {
		profile_sentinel_rejected = true;
	}
	Require(profile_sentinel_rejected, "GraphQL fixture accepted a local-residual profile outside its closed enum");

	const auto distinct_provenance = BuildDistinctGraphqlProvenanceScanPlanFixture("distinct_graphql_secret");
	Require(distinct_provenance.ConnectorName() == "package_graphql_fixture" &&
	            distinct_provenance.ConnectorVersion() == "1.2.3" &&
	            distinct_provenance.RelationName() == "repository_activity" &&
	            distinct_provenance.Domain() == duckdb_api::BaseDomain::GRAPHQL_RELAY_CONNECTION_NODE_OCCURRENCES &&
	            distinct_provenance.Operation().Graphql().operation_name == "package_repository_activity_graphql" &&
	            distinct_provenance.Operation().Graphql().document_identity ==
	                duckdb_api::PlannedGraphqlDocumentIdentity::PACKAGE_GENERATED_V1 &&
	            distinct_provenance.SourceSnapshot() != fixture.SourceSnapshot() &&
	            distinct_provenance.SourceSnapshot().find("package_graphql_fixture@1.2.3") != std::string::npos &&
	            distinct_provenance.SourceSnapshot().find("repository_activity") != std::string::npos &&
	            distinct_provenance.SecretReference().Name() == "distinct_graphql_secret" &&
	            distinct_provenance.SourceSnapshot().find("distinct_graphql_secret") == std::string::npos,
	        "distinct GraphQL provenance fixture lost its coherent package identity or exact logical secret handle");
	Require(fixture.Domain() == duckdb_api::BaseDomain::GRAPHQL_VIEWER_REPOSITORY_OCCURRENCES &&
	            fixture.Operation().Graphql().document_identity ==
	                duckdb_api::PlannedGraphqlDocumentIdentity::GITHUB_VIEWER_REPOSITORY_METRICS_V1 &&
	            CountGraphqlPlanDifferences(fixture, distinct_provenance) == 7,
	        "package/native GraphQL fixture differential changed beyond closed identity, domain, names, and secret");

	const auto admission_count = static_cast<std::size_t>(GraphqlRuntimeAdmissionCounterexample::COUNT);
	Require(admission_count == 141, "closed Runtime-facing GraphQL admission catalog changed without self-test review");
	std::vector<duckdb_api::ScanPlan> admission_candidates;
	admission_candidates.reserve(admission_count);
	for (std::size_t value = 0; value < admission_count; value++) {
		const auto counterexample = static_cast<GraphqlRuntimeAdmissionCounterexample>(value);
		const auto candidate = BuildGraphqlRuntimeAdmissionCounterexample("graphql_semantics_secret", counterexample);
		const auto differences = CountGraphqlPlanDifferences(fixture, candidate);
		const std::size_t expected_differences =
		    counterexample == GraphqlRuntimeAdmissionCounterexample::CHANGED_DOCUMENT_WITH_RECOMPUTED_DIGEST ? 2 : 1;
		Require(differences == expected_differences, "GraphQL Runtime admission counterexample " +
		                                                 std::to_string(value) + " changed " +
		                                                 std::to_string(differences) + " structured facts instead of " +
		                                                 std::to_string(expected_differences));
		if (counterexample == GraphqlRuntimeAdmissionCounterexample::CHANGED_DOCUMENT_WITH_RECOMPUTED_DIGEST) {
			const auto &canonical = fixture.Operation().Graphql();
			const auto &changed = candidate.Operation().Graphql();
			Require(changed.document != canonical.document && changed.document_digest != canonical.document_digest &&
			            changed.document_digest == duckdb_api::ComputeSha256Hex(changed.document),
			        "changed GraphQL document fixture did not carry its correctly recomputed non-canonical digest");
		}
		admission_candidates.push_back(candidate);
	}
	for (std::size_t left = 0; left < admission_candidates.size(); left++) {
		for (std::size_t right = left + 1; right < admission_candidates.size(); right++) {
			const auto left_shape = InspectGraphqlProtocolEnvelope(admission_candidates[left]);
			const auto right_shape = InspectGraphqlProtocolEnvelope(admission_candidates[right]);
			const bool distinct =
			    CountGraphqlPlanDifferences(admission_candidates[left], admission_candidates[right]) > 0 ||
			    left_shape.protocol != right_shape.protocol || left_shape.rest_present != right_shape.rest_present ||
			    left_shape.graphql_present != right_shape.graphql_present;
			Require(distinct, "GraphQL Runtime admission counterexamples " + std::to_string(left) + " and " +
			                      std::to_string(right) + " collapsed to the same structured plan");
		}
	}
	bool sentinel_rejected = false;
	try {
		(void)BuildGraphqlRuntimeAdmissionCounterexample("graphql_semantics_secret",
		                                                 GraphqlRuntimeAdmissionCounterexample::COUNT);
	} catch (const std::invalid_argument &) {
		sentinel_rejected = true;
	}
	Require(sentinel_rejected, "GraphQL Runtime admission fixture accepted a value outside its closed enum");

	struct ExpectedMalformedProtocol {
		GraphqlRuntimeAdmissionCounterexample counterexample;
		GraphqlProtocolEnvelopeShape shape;
	};
	const ExpectedMalformedProtocol malformed_protocols[] = {
	    {GraphqlRuntimeAdmissionCounterexample::UNKNOWN_PROTOCOL,
	     {static_cast<duckdb_api::PlannedProtocol>(127), false, true}},
	    {GraphqlRuntimeAdmissionCounterexample::GRAPHQL_CONTRADICTORY_PROTOCOL_PAYLOADS,
	     {duckdb_api::PlannedProtocol::GRAPHQL, true, true}},
	    {GraphqlRuntimeAdmissionCounterexample::GRAPHQL_MISSING_ACTIVE_PROTOCOL_PAYLOAD,
	     {duckdb_api::PlannedProtocol::GRAPHQL, false, false}},
	    {GraphqlRuntimeAdmissionCounterexample::GRAPHQL_WRONG_PROTOCOL_PAYLOAD,
	     {duckdb_api::PlannedProtocol::GRAPHQL, true, false}},
	    {GraphqlRuntimeAdmissionCounterexample::REST_CONTRADICTORY_PROTOCOL_PAYLOADS,
	     {duckdb_api::PlannedProtocol::REST, true, true}},
	    {GraphqlRuntimeAdmissionCounterexample::REST_MISSING_ACTIVE_PROTOCOL_PAYLOAD,
	     {duckdb_api::PlannedProtocol::REST, false, false}},
	    {GraphqlRuntimeAdmissionCounterexample::REST_WRONG_PROTOCOL_PAYLOAD,
	     {duckdb_api::PlannedProtocol::REST, false, true}}};
	for (const auto &malformed_protocol : malformed_protocols) {
		const auto candidate =
		    BuildGraphqlRuntimeAdmissionCounterexample("graphql_semantics_secret", malformed_protocol.counterexample);
		const auto actual_shape = InspectGraphqlProtocolEnvelope(candidate);
		Require(actual_shape.protocol == malformed_protocol.shape.protocol &&
		            actual_shape.rest_present == malformed_protocol.shape.rest_present &&
		            actual_shape.graphql_present == malformed_protocol.shape.graphql_present,
		        "malformed protocol fixture did not preserve its distinct closed-envelope shape");
		bool graphql_failed = false;
		bool rest_failed = false;
		try {
			(void)candidate.Operation().Graphql();
		} catch (const std::logic_error &) {
			graphql_failed = true;
		}
		try {
			(void)candidate.Operation().Rest();
		} catch (const std::logic_error &) {
			rest_failed = true;
		}
		Require(graphql_failed && rest_failed,
		        "malformed protocol envelope exposed an executable payload through the public sum API");
	}

	const auto non_authority_count = static_cast<std::size_t>(GraphqlRuntimeNonAuthorityVariation::COUNT);
	Require(non_authority_count == 3,
	        "closed GraphQL Runtime non-authority variation catalog changed without self-test review");
	std::vector<duckdb_api::ScanPlan> non_authority_candidates;
	non_authority_candidates.reserve(non_authority_count);
	for (std::size_t value = 0; value < non_authority_count; value++) {
		const auto variation = static_cast<GraphqlRuntimeNonAuthorityVariation>(value);
		const auto candidate = BuildGraphqlRuntimeNonAuthorityVariation("graphql_semantics_secret", variation);
		Require(CountGraphqlPlanDifferences(fixture, candidate) == 1,
		        "GraphQL Runtime non-authority variation did not isolate one structured fact");
		Require(candidate.RemotePredicate() == fixture.RemotePredicate() &&
		            candidate.RemoteAccuracy() == fixture.RemoteAccuracy() &&
		            candidate.ResidualOwner() == fixture.ResidualOwner() &&
		            candidate.ConditionalInput() == fixture.ConditionalInput() &&
		            candidate.Ownership().filter == fixture.Ownership().filter &&
		            candidate.Ownership().projection == fixture.Ownership().projection &&
		            candidate.Ownership().ordering == fixture.Ownership().ordering &&
		            candidate.Ownership().limit == fixture.Ownership().limit &&
		            candidate.Ownership().offset == fixture.Ownership().offset &&
		            candidate.RemoteOrdering() == fixture.RemoteOrdering() &&
		            candidate.RuntimeOrdering() == fixture.RuntimeOrdering() &&
		            candidate.RemoteLimit() == fixture.RemoteLimit() &&
		            candidate.RemoteOffset() == fixture.RemoteOffset() &&
		            candidate.RuntimeLimit() == fixture.RuntimeLimit() &&
		            candidate.RuntimeOffset() == fixture.RuntimeOffset(),
		        "GraphQL Runtime non-authority variation changed the executable relational envelope");
		non_authority_candidates.push_back(candidate);
	}
	for (std::size_t left = 0; left < non_authority_candidates.size(); left++) {
		for (std::size_t right = left + 1; right < non_authority_candidates.size(); right++) {
			Require(CountGraphqlPlanDifferences(non_authority_candidates[left], non_authority_candidates[right]) > 0,
			        "GraphQL Runtime non-authority variations " + std::to_string(left) + " and " +
			            std::to_string(right) + " collapsed to the same structured plan");
		}
	}
	Require(non_authority_candidates[static_cast<std::size_t>(
	                                     GraphqlRuntimeNonAuthorityVariation::OTHER_RESIDUAL_PREDICATE)]
	                    .ResidualPredicate() == duckdb_api::PlannedPredicate::COMPLETE_DUCKDB_FILTER &&
	            non_authority_candidates[static_cast<std::size_t>(
	                                         GraphqlRuntimeNonAuthorityVariation::OTHER_PREDICATE_CATEGORY)]
	                    .PredicateCategory() == duckdb_api::PredicateDecisionCategory::SUPERSET &&
	            non_authority_candidates[static_cast<std::size_t>(
	                                         GraphqlRuntimeNonAuthorityVariation::OTHER_PREDICATE_REASON)]
	                    .PredicateReason() == duckdb_api::PredicateDecisionReason::SELECTED_SUPERSET_MAPPING,
	        "GraphQL Runtime non-authority variations changed their named structured facts");
	bool non_authority_sentinel_rejected = false;
	try {
		(void)BuildGraphqlRuntimeNonAuthorityVariation("graphql_semantics_secret",
		                                               GraphqlRuntimeNonAuthorityVariation::COUNT);
	} catch (const std::invalid_argument &) {
		non_authority_sentinel_rejected = true;
	}
	Require(non_authority_sentinel_rejected,
	        "GraphQL Runtime non-authority fixture accepted a value outside its closed enum");

	const auto variation_count = static_cast<std::size_t>(GraphqlPlanVariation::COUNT);
	Require(variation_count == 5, "internal GraphQL equality variation catalog changed without self-test review");
	std::vector<duckdb_api::ScanPlan> variation_candidates;
	variation_candidates.reserve(variation_count);
	for (std::size_t value = 0; value < variation_count; value++) {
		const auto variation = static_cast<GraphqlPlanVariation>(value);
		const auto candidate = BuildGraphqlPlanVariation("graphql_semantics_secret", variation);
		Require(CountGraphqlPlanDifferences(fixture, candidate) == 1,
		        "internal GraphQL equality variation did not isolate one non-admission fact");
		variation_candidates.push_back(candidate);
	}
	for (std::size_t left = 0; left < variation_candidates.size(); left++) {
		for (std::size_t right = left + 1; right < variation_candidates.size(); right++) {
			Require(CountGraphqlPlanDifferences(variation_candidates[left], variation_candidates[right]) > 0,
			        "internal GraphQL equality variations " + std::to_string(left) + " and " + std::to_string(right) +
			            " collapsed to the same structured plan");
		}
	}
	const auto safe = fixture.Snapshot();
	Require(safe.find(fixture.Operation().Graphql().document) == std::string::npos &&
	            safe.find("graphql_semantics_secret") == std::string::npos,
	        "GraphQL fixture explanation exposed document or logical secret bytes");
	const auto copied = fixture;
	Require(copied.Snapshot() == fixture.Snapshot(), "GraphQL fixture copy changed immutable plan facts");
}

} // namespace graphql_semantics
} // namespace duckdb_api_test
