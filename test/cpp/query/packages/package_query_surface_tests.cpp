#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace duckdb_api_test {
void RunGeneratedRelationTests();
void RunGithubPackageSurfaceTests(const std::string &absolute_repository_root);
void RunRickAndMortyPackageSurfaceTests(const std::string &absolute_repository_root);
void RunPackageIntrospectionTests();
void RunPackageLifecycleTests();
void RunPackageManagementTests();
void RunPackagePublicationCancellationTests();
} // namespace duckdb_api_test

int main(int argc, char **argv) {
	try {
		if (argc != 2 || argv[1][0] != '/') {
			throw std::invalid_argument("usage: package_query_surface_tests ABSOLUTE_REPOSITORY_ROOT");
		}
		duckdb_api_test::RunPackageManagementTests();
		duckdb_api_test::RunPackageIntrospectionTests();
		duckdb_api_test::RunGeneratedRelationTests();
		duckdb_api_test::RunPackageLifecycleTests();
		duckdb_api_test::RunPackagePublicationCancellationTests();
		duckdb_api_test::RunGithubPackageSurfaceTests(argv[1]);
		duckdb_api_test::RunRickAndMortyPackageSurfaceTests(argv[1]);
		std::cout << "duckdb api package Query surface tests passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "duckdb api package Query surface tests failed: " << error.what() << std::endl;
		return 1;
	}
}
