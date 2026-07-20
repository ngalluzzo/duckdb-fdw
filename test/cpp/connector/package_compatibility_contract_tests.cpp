#include "connector/support/package_generation_test_fixtures.hpp"
#include "duckdb_api/package_compatibility.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using duckdb_api::PackageReloadClassification;
using duckdb_api_test::PackageCompatibilityFixture;
using duckdb_api_test::Require;

template <class Exception, class Callable>
void RequireThrows(Callable callable, const std::string &message) {
	try {
		callable();
	} catch (const Exception &) {
		return;
	}
	throw std::runtime_error(message);
}

void RequireClassification(const duckdb_api::CompiledPackageGeneration &active,
                           const duckdb_api::CompiledPackageGeneration &candidate, PackageReloadClassification expected,
                           const std::string &message) {
	const auto decision = duckdb_api::ClassifyPackageReload(active, candidate);
	Require(decision.Classification() == expected, message);
	const bool expected_success = expected != PackageReloadClassification::REJECTED_PACKAGE_IDENTITY &&
	                              expected != PackageReloadClassification::INCOMPATIBLE_RELOAD;
	Require(decision.IsCompatible() == expected_success, message + " (success disposition)");
	Require(decision.HasDiagnostic() != expected_success, message + " (diagnostic presence)");
	Require(decision.ConnectorId() == candidate.Identity().ConnectorId() ||
	            decision.ConnectorId() == active.Identity().ConnectorId(),
	        message + " (safe connector identity)");
	if (expected == PackageReloadClassification::REJECTED_PACKAGE_IDENTITY) {
		Require(std::string(decision.DiagnosticCode()) == "DUCKDB_API_PACKAGE_IDENTITY" &&
		            std::string(decision.DiagnosticPhase()) == "compatibility",
		        message + " (immutable-identity diagnostic)");
	} else if (expected == PackageReloadClassification::INCOMPATIBLE_RELOAD) {
		Require(std::string(decision.DiagnosticCode()) == "DUCKDB_API_INCOMPATIBLE_RELOAD" &&
		            std::string(decision.DiagnosticPhase()) == "compatibility",
		        message + " (incompatible diagnostic)");
	} else {
		Require(std::string(decision.DiagnosticCode()).empty() && std::string(decision.DiagnosticPhase()).empty(),
		        message + " (successful diagnostic absence)");
	}
}

void TestPackageSemVer() {
	static_assert(!std::is_default_constructible<duckdb_api::PackageSemVer>::value,
	              "package SemVer must originate from its canonical parser");
	static_assert(!std::is_copy_assignable<duckdb_api::PackageSemVer>::value,
	              "package SemVer assignment would replace immutable identity");

	const auto zero = duckdb_api::PackageSemVer::Parse("0.0.0");
	const auto patch = duckdb_api::PackageSemVer::Parse("1.2.10");
	const auto minor = duckdb_api::PackageSemVer::Parse("1.10.0");
	const auto maximum = duckdb_api::PackageSemVer::Parse("4294967295.4294967295.4294967295");
	Require(zero.Major() == 0 && zero.Minor() == 0 && zero.Patch() == 0 && zero.Canonical() == "0.0.0",
	        "zero package version lost canonical numeric identity");
	Require(patch.Compare(minor) < 0 && minor.Compare(patch) > 0 && patch.Compare(patch) == 0,
	        "package version comparison used textual rather than numeric ordering");
	Require(maximum.Major() == std::numeric_limits<std::uint32_t>::max() &&
	            maximum.Minor() == std::numeric_limits<std::uint32_t>::max() &&
	            maximum.Patch() == std::numeric_limits<std::uint32_t>::max(),
	        "package version rejected or truncated the uint32 boundary");

	const std::vector<std::string> invalid = {
	    "",       "1",           "1.2",     "1.2.3.4", "01.2.3", "1.02.3",         "1.2.03",         "+1.2.3",
	    "1.-2.3", "1.2.3-alpha", "1.2.3+1", " 1.2.3",  "1.2.3 ", "4294967296.0.0", "0.4294967296.0", "0.0.4294967296"};
	for (const auto &value : invalid) {
		RequireThrows<std::invalid_argument>([&]() { (void)duckdb_api::PackageSemVer::Parse(value); },
		                                     "invalid package SemVer was accepted: " + value);
	}
}

void TestPackageGenerationFixtureBoundary() {
	const auto fallback = duckdb_api_test::BuildTypedFallbackPackageGenerationFixture();
	const auto *typed = fallback.Connector().FindRelation(duckdb_api_test::PACKAGE_TYPED_RELATION);
	Require(typed != nullptr && typed->Inputs().size() == 4 && typed->Operations().size() == 2,
	        "typed package fixture lost its bounded relation shape");
	Require(typed->Inputs()[0].Name() == "query" && !typed->Inputs()[0].Default().HasDefault() &&
	            typed->Inputs()[1].Type() == duckdb_api::CompiledScalarType::BIGINT &&
	            typed->Inputs()[1].Default().Value().Bigint() == 25 &&
	            typed->Inputs()[2].Type() == duckdb_api::CompiledScalarType::BOOLEAN &&
	            !typed->Inputs()[2].Default().Value().Boolean() && typed->Inputs()[3].Nullable() &&
	            typed->Inputs()[3].Default().Value().IsNull(),
	        "typed package fixture collapsed order, scalar types, defaults, or typed NULL");
	Require(!typed->Operations()[0].fallback && typed->Operations()[0].selector.RequiredInputs().size() == 1 &&
	            typed->Operations()[0].selector.RequiredInputs()[0] == "query" && typed->Operations()[1].fallback,
	        "fallback fixture lost its input-selected and fallback operations");
	Require(fallback.Connector().FindRelation(duckdb_api_test::PACKAGE_DISTINCT_RELATION) != nullptr,
	        "typed package fixture lost its structurally distinct relation");

	const auto tie = duckdb_api_test::BuildTypedTiePackageGenerationFixture();
	const auto *tied = tie.Connector().FindRelation(duckdb_api_test::PACKAGE_TYPED_RELATION);
	Require(tied != nullptr && tied->Operations().size() == 2 && !tied->Operations()[0].fallback &&
	            !tied->Operations()[1].fallback &&
	            tied->Operations()[0].selector.RequiredInputs() == tied->Operations()[1].selector.RequiredInputs() &&
	            tied->Operations()[0].selector.Priority() == tied->Operations()[1].selector.Priority(),
	        "tie fixture did not preserve two equally ranked valid operations");

	const auto distinct = duckdb_api_test::BuildDistinctPackageGenerationFixture();
	Require(distinct.Connector().Relations().size() == 1 &&
	            distinct.Connector().Relations()[0].Name() == duckdb_api_test::PACKAGE_DISTINCT_RELATION &&
	            distinct.Connector().Relations()[0].Columns().size() == 1,
	        "distinct package fixture exposed the controlled relation or private construction surface");
}

void TestCompatibleTransitions() {
	const auto active =
	    duckdb_api_test::BuildPackageCompatibilityFixture(PackageCompatibilityFixture::BASELINE, "1.2.3", 'a');
	RequireClassification(
	    active, duckdb_api_test::BuildPackageCompatibilityFixture(PackageCompatibilityFixture::BASELINE, "1.2.3", 'a'),
	    PackageReloadClassification::EXACT_NO_OP, "exact generation was not a no-op");
	RequireClassification(
	    active, duckdb_api_test::BuildPackageCompatibilityFixture(PackageCompatibilityFixture::BASELINE, "1.2.4", 'b'),
	    PackageReloadClassification::COMPATIBLE_PROVENANCE_PATCH, "descriptor-identical PATCH provenance was rejected");
	RequireClassification(
	    active, duckdb_api_test::BuildPackageCompatibilityFixture(PackageCompatibilityFixture::BASELINE, "1.3.0", 'c'),
	    PackageReloadClassification::COMPATIBLE_PROVENANCE_MINOR, "descriptor-identical MINOR provenance was rejected");
	RequireClassification(
	    active,
	    duckdb_api_test::BuildPackageCompatibilityFixture(PackageCompatibilityFixture::APPEND_RELATION, "1.3.0", 'd'),
	    PackageReloadClassification::COMPATIBLE_APPEND_ONLY_MINOR, "append-only relation MINOR was rejected");
	const auto no_op = duckdb_api::ClassifyPackageReload(
	    active, duckdb_api_test::BuildPackageCompatibilityFixture(PackageCompatibilityFixture::BASELINE, "1.2.3", 'a'));
	const auto changed = duckdb_api::ClassifyPackageReload(
	    active, duckdb_api_test::BuildPackageCompatibilityFixture(PackageCompatibilityFixture::BASELINE, "1.2.4", 'b'));
	Require(!no_op.Changed() && changed.Changed(), "reload changed flag disagreed with exact and published outcomes");
}

void TestIdentityAndVersionRejections() {
	const auto active =
	    duckdb_api_test::BuildPackageCompatibilityFixture(PackageCompatibilityFixture::BASELINE, "1.2.3", 'a');
	for (const auto &version : {"1.2.3", "1.2.2", "1.1.99", "0.99.99"}) {
		RequireClassification(
		    active,
		    duckdb_api_test::BuildPackageCompatibilityFixture(PackageCompatibilityFixture::BASELINE, version, 'b'),
		    PackageReloadClassification::REJECTED_PACKAGE_IDENTITY,
		    "version reuse or downgrade escaped immutable identity");
	}
	RequireClassification(active,
	                      duckdb_api_test::BuildPackageCompatibilityFixture(
	                          PackageCompatibilityFixture::CONNECTOR_ID_CHANGED, "1.3.0", 'c'),
	                      PackageReloadClassification::INCOMPATIBLE_RELOAD,
	                      "another connector identity was treated as this connector's reload");
	RequireClassification(
	    active, duckdb_api_test::BuildPackageCompatibilityFixture(PackageCompatibilityFixture::BASELINE, "2.0.0", 'd'),
	    PackageReloadClassification::INCOMPATIBLE_RELOAD, "next-major package was accepted into the active instance");
	RequireClassification(
	    active,
	    duckdb_api_test::BuildPackageCompatibilityFixture(PackageCompatibilityFixture::APPEND_RELATION, "1.2.4", 'e'),
	    PackageReloadClassification::INCOMPATIBLE_RELOAD, "append-only structural change was mislabeled as PATCH");
}

void TestStructuralRejections() {
	const auto active =
	    duckdb_api_test::BuildPackageCompatibilityFixture(PackageCompatibilityFixture::BASELINE, "1.2.3", 'a');
	const std::vector<PackageCompatibilityFixture> incompatible = {
	    PackageCompatibilityFixture::RELATION_REMOVED,         PackageCompatibilityFixture::RELATION_REORDERED,
	    PackageCompatibilityFixture::RELATION_INSERTED_BEFORE, PackageCompatibilityFixture::RELATION_CHANGED,
	    PackageCompatibilityFixture::COLUMN_CHANGED,           PackageCompatibilityFixture::INPUT_CHANGED,
	    PackageCompatibilityFixture::OPERATION_CHANGED,        PackageCompatibilityFixture::PREDICATE_CHANGED,
	    PackageCompatibilityFixture::AUTHENTICATION_CHANGED,   PackageCompatibilityFixture::RESOURCE_CHANGED,
	    PackageCompatibilityFixture::OPERATION_ORIGIN_CHANGED, PackageCompatibilityFixture::NETWORK_POLICY_CHANGED};
	for (std::size_t index = 0; index < incompatible.size(); index++) {
		RequireClassification(
		    active, duckdb_api_test::BuildPackageCompatibilityFixture(incompatible[index], "1.3.0", 'f'),
		    PackageReloadClassification::INCOMPATIBLE_RELOAD,
		    "normalized structural mutation escaped fail-closed classification at index " + std::to_string(index));
	}
}

} // namespace

int main() {
	try {
		TestPackageSemVer();
		TestPackageGenerationFixtureBoundary();
		TestCompatibleTransitions();
		TestIdentityAndVersionRejections();
		TestStructuralRejections();
		std::cout << "package compatibility contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "package compatibility contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
