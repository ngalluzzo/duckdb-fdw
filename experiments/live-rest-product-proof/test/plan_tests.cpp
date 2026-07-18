#include "live_rest/plan.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Require(bool condition, const std::string &message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

void RequireColumn(const live_rest::Column &column, const std::string &name, live_rest::ColumnType type,
	               const std::string &json_field) {
	Require(column.name == name, "plan column name drifted for " + name);
	Require(column.type == type, "plan column type drifted for " + name);
	Require(column.json_field == json_field, "plan JSON field drifted for " + name);
}

void RequireRejected(const std::string &authority) {
	bool rejected = false;
	try {
		live_rest::BuildLiveScanPlan(authority);
	} catch (const std::invalid_argument &) {
		rejected = true;
	}
	Require(rejected, "planner accepted unsupported authority: " + authority);
}

void TestPublicPlan() {
	const auto plan = live_rest::BuildLiveScanPlan("https://api.github.com");
	Require(plan.connector_name == "github" && plan.relation_name == "users", "plan identity drifted");
	Require(plan.method == "GET", "plan method drifted");
	Require(plan.url == "https://api.github.com/search/users?q=duckdb+in%3Alogin&per_page=3",
	        "plan URL drifted");
	Require(plan.response_array_field == "items", "response array field drifted");
	Require(plan.columns.size() == 3, "static schema width drifted");
	RequireColumn(plan.columns[0], "id", live_rest::ColumnType::BIGINT, "id");
	RequireColumn(plan.columns[1], "login", live_rest::ColumnType::VARCHAR, "login");
	RequireColumn(plan.columns[2], "site_admin", live_rest::ColumnType::BOOLEAN, "site_admin");
}

void TestFixedSemanticAndResourceInvariants() {
	const auto plan = live_rest::BuildLiveScanPlan("https://api.github.com");
	Require(!plan.redirects_enabled && !plan.retries_enabled && !plan.authentication_enabled &&
	            !plan.pagination_enabled,
	        "plan enabled an excluded execution capability");
	Require(plan.max_response_bytes == 65536 && plan.max_records == 3 && plan.max_string_bytes == 256 &&
	            plan.wall_milliseconds == 5000 && plan.batch_rows == 2,
	        "plan resource envelope drifted");
	const std::string expected =
	    "connector=github;relation=users;schema=id:BIGINT!:$.id,login:VARCHAR!:$.login,"
	    "site_admin:BOOLEAN!:$.site_admin;method=GET;"
	    "url=https://api.github.com/search/users?q=duckdb+in%3Alogin&per_page=3;response_array=items;"
	    "remote_predicate=TRUE;runtime_residual=TRUE;duckdb_owns=filter,ordering,limit,offset;"
	    "redirects=disabled;retries=disabled;authentication=disabled;pagination=disabled;"
	    "budgets=response_bytes:65536,records:3,string_bytes:256,wall_ms:5000,batch_rows:2";
	Require(plan.Snapshot() == expected, "plan snapshot drifted");
	Require(live_rest::BuildLiveScanPlan("https://api.github.com").Snapshot() == plan.Snapshot(),
	        "offline planning is not deterministic");
}

void TestLoopbackPlan() {
	const auto plan = live_rest::BuildLiveScanPlan("http://127.0.0.1:1");
	Require(plan.url == "http://127.0.0.1:1/search/users?q=duckdb+in%3Alogin&per_page=3",
	        "loopback request target drifted");
	Require(live_rest::BuildLiveScanPlan("http://127.0.0.1:65535").url ==
	            "http://127.0.0.1:65535/search/users?q=duckdb+in%3Alogin&per_page=3",
	        "maximum loopback port was not accepted");
}

void TestAuthorityRejection() {
	RequireRejected("");
	RequireRejected("https://example.com");
	RequireRejected("http://api.github.com");
	RequireRejected("https://api.github.com/");
	RequireRejected("https://api.github.com:443");
	RequireRejected("https://api.github.com?redirect=example.com");
	RequireRejected("http://localhost:8080");
	RequireRejected("http://127.0.0.2:8080");
	RequireRejected("https://127.0.0.1:8080");
	RequireRejected("http://127.0.0.1");
	RequireRejected("http://127.0.0.1:");
	RequireRejected("http://127.0.0.1:0");
	RequireRejected("http://127.0.0.1:01");
	RequireRejected("http://127.0.0.1:65536");
	RequireRejected("http://127.0.0.1:-1");
	RequireRejected("http://127.0.0.1:+1");
	RequireRejected("http://127.0.0.1:abc");
	RequireRejected("http://127.0.0.1:8080/path");
	RequireRejected("http://127.0.0.1:8080?query");
}

} // namespace

int main() {
	try {
		TestPublicPlan();
		TestFixedSemanticAndResourceInvariants();
		TestLoopbackPlan();
		TestAuthorityRejection();
		std::cout << "live REST plan tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "live REST plan tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
