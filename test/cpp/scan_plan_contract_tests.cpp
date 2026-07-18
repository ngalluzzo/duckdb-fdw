#include "duckdb_api/connector.hpp"
#include "duckdb_api/scan_plan.hpp"
#include "support/live_scan_request.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <locale>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using duckdb_api_test::BuildLiveScanRequest;
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

static_assert(!HasBaseUrlMember<duckdb_api::PlannedRestOperation>::VALUE,
              "PlannedRestOperation must not restore a parseable base URL");
static_assert(!HasPathMember<duckdb_api::PlannedRestOrigin>::VALUE,
              "PlannedRestOrigin must not carry a path component");
static_assert(!HasQueryMember<duckdb_api::PlannedRestOrigin>::VALUE,
              "PlannedRestOrigin must not carry a query component");
static_assert(!HasFragmentMember<duckdb_api::PlannedRestOrigin>::VALUE,
              "PlannedRestOrigin must not carry a fragment component");
static_assert(std::is_same<decltype(duckdb_api::PlannedRestOrigin::scheme), duckdb_api::PlannedUrlScheme>::value,
              "PlannedRestOrigin scheme must remain typed");
static_assert(std::is_same<decltype(duckdb_api::PlannedRestOrigin::port), std::uint16_t>::value,
              "PlannedRestOrigin port must remain an explicit uint16_t");

class GroupedDigits final : public std::numpunct<char> {
protected:
	char do_thousands_sep() const override {
		return '_';
	}

	std::string do_grouping() const override {
		return "\3";
	}
};

void RequireColumn(const duckdb_api::PlannedColumn &column, const std::string &name, const std::string &logical_type,
                   const std::string &extractor) {
	Require(column.name == name, "planned column name drifted for " + name);
	Require(column.logical_type == logical_type, "planned column type drifted for " + name);
	Require(!column.nullable, "planned column became nullable for " + name);
	Require(column.extractor == extractor, "planned column extractor drifted for " + name);
}

void TestTypedImmutableLivePlan() {
	Require(std::is_copy_constructible<duckdb_api::ScanPlan>::value,
	        "ScanPlan must be copy-constructible for bind state");
	Require(std::is_move_constructible<duckdb_api::ScanPlan>::value,
	        "ScanPlan must be move-constructible for bind state");
	Require(!std::is_copy_assignable<duckdb_api::ScanPlan>::value,
	        "ScanPlan must not allow post-construction assignment");
	Require(!std::is_move_assignable<duckdb_api::ScanPlan>::value,
	        "ScanPlan must not allow post-construction move assignment");

	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto plan = duckdb_api::BuildConservativeScanPlan(connector, BuildLiveScanRequest(connector));
	Require(plan.ConnectorName() == "github" && plan.ConnectorVersion() == "0.3.0" &&
	            plan.RelationName() == "duckdb_login_search_page",
	        "ScanPlan identity drifted");
	Require(plan.SourceSnapshot() == connector.Snapshot(), "ScanPlan is not bound to its connector snapshot");
	Require(plan.Domain() == duckdb_api::BaseDomain::SINGLE_RESPONSE_PAGE, "ScanPlan widened the bounded base domain");

	const auto &operation = plan.Operation();
	Require(operation.operation_name == "github_search_duckdb_login_page" &&
	            operation.protocol == duckdb_api::PlannedProtocol::REST &&
	            operation.method == duckdb_api::PlannedHttpMethod::GET &&
	            operation.cardinality == duckdb_api::PlannedCardinality::ZERO_TO_MANY &&
	            operation.replay_safety == duckdb_api::PlannedReplaySafety::SAFE,
	        "ScanPlan operation classification drifted");
	Require(operation.origin.scheme == duckdb_api::PlannedUrlScheme::HTTPS &&
	            operation.origin.host == "api.github.com" && operation.origin.port == 443 &&
	            operation.path == "/search/users" && operation.records_extractor == "$.items[*]",
	        "ScanPlan REST structure drifted");
	Require(operation.query_parameters.size() == 2 && operation.query_parameters[0].name == "q" &&
	            operation.query_parameters[0].encoded_value == "duckdb+in%3Alogin" &&
	            operation.query_parameters[1].name == "per_page" && operation.query_parameters[1].encoded_value == "3",
	        "ScanPlan source-definition query drifted");
	Require(operation.headers.size() == 3 && operation.headers[0].name == "Accept" &&
	            operation.headers[0].value == "application/vnd.github+json" &&
	            operation.headers[1].name == "User-Agent" && operation.headers[1].value == "duckdb-api/0.3.0" &&
	            operation.headers[2].name == "X-GitHub-Api-Version" && operation.headers[2].value == "2022-11-28",
	        "ScanPlan fixed headers drifted");

	Require(plan.OutputColumns().size() == 3, "ScanPlan projection closure width drifted");
	RequireColumn(plan.OutputColumns()[0], "id", "BIGINT", "$.id");
	RequireColumn(plan.OutputColumns()[1], "login", "VARCHAR", "$.login");
	RequireColumn(plan.OutputColumns()[2], "site_admin", "BOOLEAN", "$.site_admin");

	Require(plan.RemotePredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.ResidualPredicate() == duckdb_api::PlannedPredicate::TRUE_FOR_BASE_DOMAIN &&
	            plan.ResidualOwner() == duckdb_api::RelationalOwner::DUCKDB,
	        "ScanPlan changed the bounded-domain predicate classification");
	Require(plan.Ownership().filter == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().ordering == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().limit == duckdb_api::RelationalOwner::DUCKDB &&
	            plan.Ownership().offset == duckdb_api::RelationalOwner::DUCKDB,
	        "ScanPlan transferred a relational operator away from DuckDB");
	Require(plan.RemoteOrdering() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeOrdering() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RemoteLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RemoteOffset() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeLimit() == duckdb_api::RelationalDelegation::NONE &&
	            plan.RuntimeOffset() == duckdb_api::RelationalDelegation::NONE,
	        "ScanPlan claimed hidden ordering or bounds");
	Require(plan.Pagination() == duckdb_api::FeatureState::DISABLED &&
	            plan.Providers() == duckdb_api::FeatureState::DISABLED &&
	            plan.Retry() == duckdb_api::FeatureState::DISABLED &&
	            plan.Cache() == duckdb_api::FeatureState::DISABLED &&
	            plan.Authentication() == duckdb_api::FeatureState::DISABLED,
	        "ScanPlan enabled an excluded feature");
	Require(plan.Network().allowed_schemes == std::vector<std::string>({"https"}) &&
	            plan.Network().allowed_hosts == std::vector<std::string>({"api.github.com"}) &&
	            !plan.Network().redirects_enabled && !plan.Network().private_addresses_enabled &&
	            !plan.Network().link_local_addresses_enabled && !plan.Network().loopback_addresses_enabled,
	        "ScanPlan network capability drifted");
	Require(plan.Budgets().IsWithinLiveRestBounds() &&
	            plan.Budgets().decoded_records == duckdb_api::LIVE_RELATION_MAX_RECORDS,
	        "ScanPlan effective resource budget escaped fixed relation bounds");
	Require(
	    plan.ClassificationReason() ==
	        "fixed request defines the complete single-response base domain; DuckDB retains all relational operators",
	    "ScanPlan classification reason drifted");
}

void TestGoldenSnapshotAndDeterminism() {
	const auto connector = duckdb_api::BuildNativeGithubConnector();
	const auto request = BuildLiveScanRequest(connector);
	const auto first = duckdb_api::BuildConservativeScanPlan(connector, request);
	const auto second = duckdb_api::BuildConservativeScanPlan(connector, request);
	const std::string expected =
	    "connector=github;version=0.3.0;relation=duckdb_login_search_page;source_snapshot=[" + connector.Snapshot() +
	    "];domain=single_response_page;operation=github_search_duckdb_login_page:zero_to_many:REST:GET:safe;"
	    "request=origin:[scheme:https,host:api.github.com,port:443],path:/search/users,"
	    "query:[q=duckdb+in%3Alogin,per_page=3],"
	    "headers:[Accept=application/vnd.github+json,User-Agent=duckdb-api/0.3.0,"
	    "X-GitHub-Api-Version=2022-11-28];response_records=$.items[*];"
	    "projection=id:BIGINT!:$.id,login:VARCHAR!:$.login,site_admin:BOOLEAN!:$.site_admin;"
	    "remote_predicate=TRUE@single_response_page;residual_predicate=TRUE@single_response_page;"
	    "residual_owner=duckdb;owners=filter:duckdb,ordering:duckdb,limit:duckdb,offset:duckdb;"
	    "delegation=remote_ordering:none,runtime_ordering:none,remote_limit:none,remote_offset:none,"
	    "runtime_limit:none,runtime_offset:none;features=pagination:disabled,providers:disabled,retry:disabled,"
	    "cache:disabled,authentication:disabled;network=schemes:[https],hosts:[api.github.com],redirects:denied,"
	    "private:denied,link_local:denied,loopback:denied;budgets=request_attempts:1,response_bytes:65536,"
	    "header_bytes:16384,decompressed_bytes:65536,records:3,string_bytes:256,json_nesting:16,"
	    "decoded_memory_bytes:131072,batch_rows:2,wall_ms:5000,concurrency:1;reason=fixed request defines the "
	    "complete single-response base domain; DuckDB retains all relational operators";
	Require(first.Snapshot() == expected, "ScanPlan golden snapshot drifted");
	Require(second.Snapshot() == first.Snapshot(), "offline planning is not deterministic");

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
	Require(localized_snapshot == expected, "ScanPlan snapshot depends on the process-global locale");
}

} // namespace

int main() {
	try {
		TestTypedImmutableLivePlan();
		TestGoldenSnapshotAndDeterminism();
		std::cout << "scan plan contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "scan plan contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
