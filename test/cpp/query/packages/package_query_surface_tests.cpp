#include <exception>
#include <iostream>

namespace duckdb_api_test {
void RunGeneratedRelationTests();
void RunPackageIntrospectionTests();
void RunPackageLifecycleTests();
void RunPackageManagementTests();
} // namespace duckdb_api_test

int main() {
	try {
		duckdb_api_test::RunPackageManagementTests();
		duckdb_api_test::RunPackageIntrospectionTests();
		duckdb_api_test::RunGeneratedRelationTests();
		duckdb_api_test::RunPackageLifecycleTests();
		std::cout << "duckdb api package Query surface tests passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "duckdb api package Query surface tests failed: " << error.what() << std::endl;
		return 1;
	}
}
