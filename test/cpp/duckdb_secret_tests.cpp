#include "support/duckdb_secret_test_support.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>

int main() {
	using namespace duckdb_api_test::duckdb_secret;
	try {
		RunTest("registered surface", TestRegisteredSurfaceIsTemporaryConfigAndRedacted);
		RunTest("creation validation", TestCreationRejectsImplicitPersistenceAndMalformedOptions);
		RunTest("resolution validation", TestResolutionValidatesTypeProviderShapeAndToken);
		RunTest("resolution lifecycle", TestResolutionObservesReplacementDropStorageAndAmbiguity);
		RunTest("current transaction", TestResolutionUsesCurrentTransaction);
		RunTest("independent storage validation", TestResolutionRequiresTemporaryMemoryIndependently);
		RunTest("active transaction", TestResolutionRequiresAnActiveTransaction);
		RunTest("host interruption", TestResolutionPreservesHostInterruption);
		RunTest("host failure", TestResolutionClassifiesOtherHostFailuresAsInternal);
		RunTest("registration failure", TestFailedProviderRegistrationNeverPublishesScan);
		std::cout << "duckdb secret tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "duckdb secret tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
