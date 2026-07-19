#include "semantics/support/scan_plan_test_fixture_test_support.hpp"

#include "support/require.hpp"

namespace duckdb_api_test {
namespace scan_plan_fixture_contract {

void RequireCanaryAbsent(const duckdb_api::ScanPlan &plan, const std::string &canary) {
	Require(plan.ConnectorName().find(canary) == std::string::npos &&
	            plan.ConnectorVersion().find(canary) == std::string::npos &&
	            plan.RelationName().find(canary) == std::string::npos &&
	            plan.SourceSnapshot().find(canary) == std::string::npos &&
	            plan.ClassificationReason().find(canary) == std::string::npos &&
	            plan.Operation().operation_name.find(canary) == std::string::npos &&
	            plan.Operation().origin.host.find(canary) == std::string::npos &&
	            plan.Operation().path.find(canary) == std::string::npos &&
	            plan.Operation().records_extractor.find(canary) == std::string::npos &&
	            plan.AuthenticationObligation().LogicalCredential().find(canary) == std::string::npos,
	        "runtime-built credential canary entered scalar fixture plan state");
	if (plan.SecretReference().IsPresent()) {
		Require(plan.SecretReference().Name().find(canary) == std::string::npos,
		        "runtime-built credential canary entered fixture logical reference");
	}
	if (plan.AuthenticationObligation().Destination() != nullptr) {
		Require(plan.AuthenticationObligation().Destination()->host.find(canary) == std::string::npos,
		        "runtime-built credential canary entered fixture authorization destination");
	}
	for (const auto &query : plan.Operation().query_parameters) {
		Require(query.name.find(canary) == std::string::npos && query.encoded_value.find(canary) == std::string::npos,
		        "runtime-built credential canary entered fixture query fields");
	}
	for (const auto &header : plan.Operation().headers) {
		Require(header.name.find(canary) == std::string::npos && header.value.find(canary) == std::string::npos,
		        "runtime-built credential canary entered fixture fixed headers");
	}
	for (const auto &column : plan.OutputColumns()) {
		Require(column.name.find(canary) == std::string::npos &&
		            column.logical_type.find(canary) == std::string::npos &&
		            column.extractor.find(canary) == std::string::npos,
		        "runtime-built credential canary entered fixture schema");
	}
	for (const auto &scheme : plan.Network().allowed_schemes) {
		Require(scheme.find(canary) == std::string::npos,
		        "runtime-built credential canary entered fixture network schemes");
	}
	for (const auto &host : plan.Network().allowed_hosts) {
		Require(host.find(canary) == std::string::npos,
		        "runtime-built credential canary entered fixture network hosts");
	}
}

} // namespace scan_plan_fixture_contract
} // namespace duckdb_api_test
