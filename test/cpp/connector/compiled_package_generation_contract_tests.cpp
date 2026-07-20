#include "duckdb_api/compiled_package_generation.hpp"
#include "duckdb_api/connector.hpp"
#include "duckdb_api/internal/connector/compiled_model_builder.hpp"
#include "support/require.hpp"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using duckdb_api::CompiledScalarType;
using duckdb_api::internal::CompiledModelBuilder;
using duckdb_api_test::Require;

#define DEFINE_MEMBER_PROBE(PROBE_NAME, MEMBER_NAME)                                                                   \
	template <typename T>                                                                                              \
	class PROBE_NAME {                                                                                                 \
		template <typename U>                                                                                          \
		static char Test(decltype(&U::MEMBER_NAME));                                                                   \
		template <typename U>                                                                                          \
		static long Test(...);                                                                                         \
                                                                                                                       \
	public:                                                                                                            \
		static const bool VALUE = sizeof(Test<T>(0)) == sizeof(char);                                                  \
	}

DEFINE_MEMBER_PROBE(HasConnectorMember, Connector);
DEFINE_MEMBER_PROBE(HasOperationsMember, Operations);
DEFINE_MEMBER_PROBE(HasPredicateMappingsMember, PredicateMappings);
DEFINE_MEMBER_PROBE(HasNetworkPolicyMember, NetworkPolicy);
DEFINE_MEMBER_PROBE(HasExtractorMember, Extractor);
DEFINE_MEMBER_PROBE(HasSnapshotMember, Snapshot);
DEFINE_MEMBER_PROBE(HasLegacyOperationSelectorBuilder, OperationSelector);

#undef DEFINE_MEMBER_PROBE

static_assert(std::is_copy_constructible<duckdb_api::CompiledPackageGeneration>::value,
              "package generations must support immutable owner copies");
static_assert(!std::is_copy_assignable<duckdb_api::CompiledPackageGeneration>::value,
              "package generation assignment would replace immutable ownership");
static_assert(!std::is_default_constructible<duckdb_api::CompiledPackageGeneration>::value,
              "package generations must not admit empty ownership");
static_assert(!std::is_default_constructible<duckdb_api::CompiledGenerationHandle>::value,
              "opaque generation handles must originate from validated generations");
static_assert(!std::is_copy_assignable<duckdb_api::CompiledGenerationHandle>::value,
              "opaque generation assignment would replace pinned ownership");
static_assert(!HasConnectorMember<duckdb_api::CompiledQueryRegistrationView>::VALUE,
              "Query registration must not expose the generalized connector");
static_assert(!HasOperationsMember<duckdb_api::CompiledRegistrationRelation>::VALUE,
              "Query registration must not expose operation selection");
static_assert(!HasPredicateMappingsMember<duckdb_api::CompiledRegistrationRelation>::VALUE,
              "Query registration must not expose predicate declarations");
static_assert(!HasNetworkPolicyMember<duckdb_api::CompiledQueryRegistrationView>::VALUE,
              "Query registration must not expose network policy");
static_assert(!HasExtractorMember<duckdb_api::CompiledRegistrationColumn>::VALUE,
              "Query registration must not expose extractor syntax");
static_assert(!HasSnapshotMember<duckdb_api::CompiledGenerationHandle>::VALUE,
              "opaque generation handles must not become a metadata side channel");
static_assert(!HasLegacyOperationSelectorBuilder<CompiledModelBuilder>::VALUE,
              "package builder must not accept author priority, alternatives, or forbidden inputs");
static_assert(!std::is_default_constructible<duckdb_api::CompiledRequiredInputReference>::value,
              "tagged required inputs must originate from Connector's validated builder");
static_assert(!std::is_copy_assignable<duckdb_api::CompiledRequiredInputReference>::value,
              "tagged required input assignment would replace immutable authority");

template <class Exception, class Callable>
void RequireThrows(Callable callable, const std::string &message) {
	try {
		callable();
	} catch (const Exception &) {
		return;
	}
	throw std::runtime_error(message);
}

std::string Digest(char digit = 'a') {
	return "sha256." + std::string(64, digit);
}

duckdb_api::CompiledConnector BuildPackageConnector(std::string connector_id = "acme",
                                                    std::string package_version = "1.2.3",
                                                    bool legacy_selector = false) {
	const duckdb_api::CompiledHttpOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledHttpHost("service.example"), 443};
	std::vector<duckdb_api::CompiledColumn> columns;
	columns.push_back(CompiledModelBuilder::Column("id", CompiledScalarType::BIGINT, false, "$.id"));
	columns.push_back(CompiledModelBuilder::Column("label", CompiledScalarType::VARCHAR, true, "$.label"));

	std::vector<duckdb_api::CompiledRelationInput> inputs;
	inputs.push_back(
	    CompiledModelBuilder::Input("enabled", CompiledScalarType::BOOLEAN, false, CompiledModelBuilder::NoDefault()));
	inputs.push_back(CompiledModelBuilder::Input("page_size", CompiledScalarType::BIGINT, false,
	                                             CompiledModelBuilder::Default(CompiledModelBuilder::Bigint(10))));
	inputs.push_back(CompiledModelBuilder::Input(
	    "scope", CompiledScalarType::VARCHAR, true,
	    CompiledModelBuilder::Default(CompiledModelBuilder::Null(CompiledScalarType::VARCHAR))));

	std::vector<duckdb_api::CompiledOperation> operations;
	auto selector =
	    legacy_selector ? duckdb_api::CompiledOperationSelector() : CompiledModelBuilder::V1OperationSelector({});
	operations.push_back(duckdb_api::CompiledOperation {"list_rows",
	                                                    true,
	                                                    duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	                                                    duckdb_api::CompiledProtocol::REST,
	                                                    duckdb_api::CompiledHttpMethod::GET,
	                                                    duckdb_api::CompiledReplaySafety::SAFE,
	                                                    false,
	                                                    CompiledModelBuilder::DisabledPagination(),
	                                                    {origin, "/rows", {}, {{"Accept", "application/json"}}},
	                                                    duckdb_api::CompiledResponseSource::ROOT_ARRAY,
	                                                    "$",
	                                                    std::move(selector)});

	std::vector<duckdb_api::CompiledRelation> relations;
	relations.push_back(CompiledModelBuilder::Relation(
	    "rows", std::move(columns), std::move(inputs), {}, std::move(operations),
	    CompiledModelBuilder::AnonymousAuthentication(), CompiledModelBuilder::UnpaginatedResources(100, 1024)));
	return CompiledModelBuilder::Connector(
	    duckdb_api::CompiledConnectorOrigin::PACKAGE_COMPILED_METADATA, std::move(connector_id),
	    std::move(package_version), std::move(relations),
	    duckdb_api::CompiledNetworkPolicy {{"https"}, {"service.example"}, false, false, false, false, 65536});
}

duckdb_api::CompiledPackageGeneration BuildGeneration() {
	auto identity = CompiledModelBuilder::PackageIdentity("duckdb_api/v1", "acme", "1.2.3", Digest());
	return CompiledModelBuilder::PackageGeneration(std::move(identity), BuildPackageConnector());
}

void TestTypedValuesAndDefaultPresence() {
	const auto boolean_value = CompiledModelBuilder::Boolean(true);
	const auto bigint_value = CompiledModelBuilder::Bigint(std::numeric_limits<std::int64_t>::min());
	const auto varchar_value = CompiledModelBuilder::Varchar("");
	const auto null_value = CompiledModelBuilder::Null(CompiledScalarType::VARCHAR);
	auto structural_column = CompiledModelBuilder::Column("id", CompiledScalarType::BIGINT, false, "$.id");
	structural_column.logical_type = "VARCHAR";

	Require(boolean_value.Type() == CompiledScalarType::BOOLEAN && !boolean_value.IsNull() && boolean_value.Boolean(),
	        "compiled BOOLEAN value lost its structural payload");
	Require(bigint_value.Type() == CompiledScalarType::BIGINT && !bigint_value.IsNull() &&
	            bigint_value.Bigint() == std::numeric_limits<std::int64_t>::min(),
	        "compiled BIGINT value lost its structural payload");
	Require(varchar_value.Type() == CompiledScalarType::VARCHAR && !varchar_value.IsNull() &&
	            varchar_value.Varchar().empty(),
	        "compiled VARCHAR value confused an empty string with absence");
	Require(null_value.Type() == CompiledScalarType::VARCHAR && null_value.IsNull(),
	        "compiled typed NULL lost its declared type");
	Require(structural_column.ScalarType() == CompiledScalarType::BIGINT,
	        "package column recovered type authority from mutable legacy text");
	RequireThrows<std::logic_error>([&]() { (void)boolean_value.Bigint(); }, "BOOLEAN value exposed a BIGINT payload");
	RequireThrows<std::logic_error>([&]() { (void)null_value.Varchar(); }, "typed NULL exposed a VARCHAR payload");

	const auto absent = CompiledModelBuilder::NoDefault();
	const auto present_null = CompiledModelBuilder::Default(null_value);
	Require(!absent.HasDefault() && present_null.HasDefault() && present_null.Value().IsNull(),
	        "absent default and present typed NULL default collapsed");
	RequireThrows<std::logic_error>([&]() { (void)absent.Value(); }, "absent default exposed a value");
}

void TestInputValidationAndOrdering() {
	RequireThrows<std::invalid_argument>(
	    []() {
		    (void)CompiledModelBuilder::Input("secret", CompiledScalarType::VARCHAR, true,
		                                      CompiledModelBuilder::NoDefault());
	    },
	    "reserved Query input name was accepted");
	RequireThrows<std::invalid_argument>(
	    []() {
		    (void)CompiledModelBuilder::Input(std::string(64, 'a'), CompiledScalarType::VARCHAR, true,
		                                      CompiledModelBuilder::NoDefault());
	    },
	    "overlong v1 input identifier was accepted");
	RequireThrows<std::invalid_argument>(
	    []() {
		    (void)CompiledModelBuilder::Input(
		        "count", CompiledScalarType::BIGINT, false,
		        CompiledModelBuilder::Default(CompiledModelBuilder::Null(CompiledScalarType::BIGINT)));
	    },
	    "non-nullable input accepted a NULL default");
	RequireThrows<std::invalid_argument>(
	    []() {
		    (void)CompiledModelBuilder::Input("count", CompiledScalarType::BIGINT, true,
		                                      CompiledModelBuilder::Default(CompiledModelBuilder::Varchar("10")));
	    },
	    "input accepted a default of another structural type");

	const auto generation = BuildGeneration();
	const auto &inputs = generation.Connector().Relations().at(0).Inputs();
	Require(inputs.size() == 3 && inputs[0].Name() == "enabled" && inputs[1].Name() == "page_size" &&
	            inputs[2].Name() == "scope",
	        "compiled relation did not preserve declared input order");
	Require(!inputs[0].Default().HasDefault() && inputs[1].Default().Value().Bigint() == 10 && inputs[2].Nullable() &&
	            inputs[2].Default().Value().IsNull(),
	        "compiled relation lost default presence, value, or nullability");
}

duckdb_api::CompiledOperation SelectorTestOperation(duckdb_api::CompiledOperationSelector selector) {
	const duckdb_api::CompiledHttpOrigin origin = {duckdb_api::CompiledUrlScheme::HTTPS,
	                                               duckdb_api::CompiledHttpHost("service.example"), 443};
	return duckdb_api::CompiledOperation {"selected_rows",
	                                      false,
	                                      duckdb_api::CompiledOperationCardinality::ZERO_TO_MANY,
	                                      duckdb_api::CompiledProtocol::REST,
	                                      duckdb_api::CompiledHttpMethod::GET,
	                                      duckdb_api::CompiledReplaySafety::SAFE,
	                                      false,
	                                      CompiledModelBuilder::DisabledPagination(),
	                                      {origin, "/selected", {}, {}},
	                                      duckdb_api::CompiledResponseSource::ROOT_ARRAY,
	                                      "$",
	                                      std::move(selector)};
}

void TestTaggedRequiredInputReferences() {
	const auto relation_reference = CompiledModelBuilder::RelationInputReference("query");
	const auto conditional_reference = CompiledModelBuilder::ConditionalInputReference("visibility");
	Require(relation_reference.Kind() == duckdb_api::CompiledRequiredInputKind::RELATION_INPUT &&
	            relation_reference.Id() == "query" &&
	            conditional_reference.Kind() == duckdb_api::CompiledRequiredInputKind::CONDITIONAL_INPUT &&
	            conditional_reference.Id() == "visibility",
	        "compiled required-input tags or exact identifiers drifted");
	RequireThrows<std::invalid_argument>(
	    []() { (void)CompiledModelBuilder::RelationInputReference("input.query"); },
	    "compiled reference parsed an author prefix instead of accepting an exact identifier");
	RequireThrows<std::invalid_argument>(
	    []() { (void)CompiledModelBuilder::ConditionalInputReference(std::string(64, 'a')); },
	    "compiled reference accepted an overlong identifier");
	RequireThrows<std::invalid_argument>(
	    []() {
		    (void)CompiledModelBuilder::V1OperationSelector({CompiledModelBuilder::RelationInputReference("query"),
		                                                     CompiledModelBuilder::RelationInputReference("query")});
	    },
	    "v1 selector accepted a duplicate tagged reference");

	const auto canonical = CompiledModelBuilder::V1OperationSelector(
	    {CompiledModelBuilder::ConditionalInputReference("zeta"), CompiledModelBuilder::RelationInputReference("zeta"),
	     CompiledModelBuilder::RelationInputReference("alpha")});
	Require(
	    !canonical.IsLegacyCompatibilityBridge() && canonical.RequiredInputReferences().size() == 3 &&
	        canonical.RequiredInputReferences()[0].Kind() == duckdb_api::CompiledRequiredInputKind::RELATION_INPUT &&
	        canonical.RequiredInputReferences()[0].Id() == "alpha" &&
	        canonical.RequiredInputReferences()[1].Kind() == duckdb_api::CompiledRequiredInputKind::RELATION_INPUT &&
	        canonical.RequiredInputReferences()[1].Id() == "zeta" &&
	        canonical.RequiredInputReferences()[2].Kind() == duckdb_api::CompiledRequiredInputKind::CONDITIONAL_INPUT &&
	        canonical.RequiredInputReferences()[2].Id() == "zeta" && canonical.RequiredInputs().empty() &&
	        canonical.AnyInputSets().empty() && canonical.ForbiddenInputs().empty() && canonical.Priority() == 0,
	    "v1 selector failed canonical tagged ordering or exposed legacy policy");

	const auto make_relation = [](duckdb_api::CompiledOperationSelector selector) {
		std::vector<duckdb_api::CompiledColumn> columns;
		columns.push_back(CompiledModelBuilder::Column("id", CompiledScalarType::BIGINT, false, "$.id"));
		std::vector<duckdb_api::CompiledRelationInput> inputs;
		inputs.push_back(CompiledModelBuilder::Input("query", CompiledScalarType::VARCHAR, false,
		                                             CompiledModelBuilder::NoDefault()));
		std::vector<duckdb_api::CompiledOperation> operations;
		operations.push_back(SelectorTestOperation(std::move(selector)));
		return CompiledModelBuilder::Relation("selected", std::move(columns), std::move(inputs), {},
		                                      std::move(operations), CompiledModelBuilder::AnonymousAuthentication(),
		                                      CompiledModelBuilder::UnpaginatedResources(8, 64));
	};
	const auto relation = make_relation(
	    CompiledModelBuilder::V1OperationSelector({CompiledModelBuilder::RelationInputReference("query")}));
	Require(relation.Operations()[0].selector.RequiredInputReferences()[0].Id() == "query",
	        "exact tagged relation input did not survive relation validation");
	RequireThrows<std::invalid_argument>(
	    [&]() {
		    (void)make_relation(
		        CompiledModelBuilder::V1OperationSelector({CompiledModelBuilder::ConditionalInputReference("query")}));
	    },
	    "wrong-kind conditional reference matched a relation input with the same identifier");
	RequireThrows<std::invalid_argument>(
	    [&]() {
		    (void)make_relation(
		        CompiledModelBuilder::V1OperationSelector({CompiledModelBuilder::RelationInputReference("missing")}));
	    },
	    "missing tagged relation input escaped validation");

	RequireThrows<std::invalid_argument>(
	    []() {
		    auto identity = CompiledModelBuilder::PackageIdentity("duckdb_api/v1", "acme", "1.2.3", Digest());
		    (void)CompiledModelBuilder::PackageGeneration(std::move(identity),
		                                                  BuildPackageConnector("acme", "1.2.3", true));
	    },
	    "package generation accepted the temporary legacy selector bridge");
}

void TestStableIdentityValidation() {
	const auto identity = CompiledModelBuilder::PackageIdentity("duckdb_api/v1", "acme", "1.2.3", Digest());
	Require(identity.SpecIdentifier() == "duckdb_api/v1" && identity.ConnectorId() == "acme" &&
	            identity.PackageVersion() == "1.2.3" && identity.PackageDigest() == Digest(),
	        "compiled package identity changed accepted bytes");

	for (const auto &spec : {"", "duckdb_api/draft", "duckdb_api/v2"}) {
		RequireThrows<std::invalid_argument>(
		    [spec]() { (void)CompiledModelBuilder::PackageIdentity(spec, "acme", "1.2.3", Digest()); },
		    "unsupported spec identifier entered a package generation");
	}
	for (const auto &version : {"1", "1.2", "01.2.3", "1.2.3-alpha", "1.2.3+build", "4294967296.0.0"}) {
		RequireThrows<std::invalid_argument>(
		    [version]() { (void)CompiledModelBuilder::PackageIdentity("duckdb_api/v1", "acme", version, Digest()); },
		    "non-canonical package version entered a package generation");
	}
	const std::vector<std::string> invalid_digests = {"", "sha256.abc", "sha256." + std::string(64, 'A'),
	                                                  "sha512." + std::string(64, 'a')};
	for (const auto &digest : invalid_digests) {
		RequireThrows<std::invalid_argument>(
		    [digest]() { (void)CompiledModelBuilder::PackageIdentity("duckdb_api/v1", "acme", "1.2.3", digest); },
		    "invalid package digest entered a package generation");
	}

	RequireThrows<std::invalid_argument>(
	    []() {
		    auto identity = CompiledModelBuilder::PackageIdentity("duckdb_api/v1", "other", "1.2.3", Digest());
		    (void)CompiledModelBuilder::PackageGeneration(std::move(identity), BuildPackageConnector());
	    },
	    "generation accepted a connector-identity mismatch");
	RequireThrows<std::invalid_argument>(
	    []() {
		    auto identity = CompiledModelBuilder::PackageIdentity("duckdb_api/v1", "acme", "1.2.4", Digest());
		    (void)CompiledModelBuilder::PackageGeneration(std::move(identity), BuildPackageConnector());
	    },
	    "generation accepted a package-version mismatch");
	RequireThrows<std::invalid_argument>(
	    []() {
		    auto identity = CompiledModelBuilder::PackageIdentity("duckdb_api/v1", "github", "0.7.0", Digest());
		    (void)CompiledModelBuilder::PackageGeneration(std::move(identity),
		                                                  duckdb_api::BuildNativeGithubConnector());
	    },
	    "native preview metadata was relabeled as a package generation");
}

void TestQueryProjectionAndLifetime() {
	const auto generation = BuildGeneration();
	const auto generation_copy = generation;
	const auto view = generation.QueryRegistration();
	Require(&generation.Connector() == &generation_copy.Connector(),
	        "generation copy duplicated rather than shared immutable ownership");
	Require(view.Identity().ConnectorId() == "acme" && view.Relations().size() == 1,
	        "Query registration lost package identity or relation order");
	const auto &relation = view.Relations().at(0);
	Require(relation.Name() == "rows" &&
	            relation.Authentication() == duckdb_api::CompiledRegistrationAuthentication::ANONYMOUS,
	        "Query registration changed relation identity or auth shape");
	Require(relation.Columns().size() == 2 && relation.Columns()[0].Name() == "id" &&
	            relation.Columns()[0].Type() == CompiledScalarType::BIGINT && !relation.Columns()[0].Nullable() &&
	            relation.Columns()[1].Name() == "label" &&
	            relation.Columns()[1].Type() == CompiledScalarType::VARCHAR && relation.Columns()[1].Nullable(),
	        "Query registration did not preserve structural output order and nullability");
	Require(relation.Inputs().size() == 3 && relation.Inputs()[1].Default().Value().Bigint() == 10,
	        "Query registration did not preserve structural input defaults");
	Require(view.GenerationHandle().IsValid() && view.GenerationHandle().IsSameGeneration(generation.OpaqueHandle()) &&
	            !view.GenerationHandle().IsSameGeneration(BuildGeneration().OpaqueHandle()),
	        "opaque handle did not distinguish exact immutable generations");

	const auto retained_view = []() {
		return BuildGeneration().QueryRegistration();
	}();
	Require(retained_view.GenerationHandle().IsValid() && retained_view.Relations().at(0).Name() == "rows",
	        "Query view did not retain its generation after the provider owner ended");
}

void TestNativeCompatibility() {
	const auto native = duckdb_api::BuildNativeGithubConnector();
	Require(native.Origin() == duckdb_api::CompiledConnectorOrigin::NATIVE_PRODUCT_METADATA &&
	            native.Version() == "0.7.0" && native.Relations().size() == 4,
	        "package model changed native 0.7 identity or catalog width");
	for (const auto &relation : native.Relations()) {
		Require(relation.Inputs().empty(), "native preview unexpectedly acquired package relation inputs");
		for (const auto &column : relation.Columns()) {
			Require(std::string(duckdb_api::CompiledScalarTypeName(column.ScalarType())) == column.logical_type,
			        "native column string and structural type diverged");
		}
	}
	Require(native.Snapshot().find("origin=native_product_metadata;connector=github;version=0.7.0") == 0,
	        "package origin support changed native safe provenance");
}

} // namespace

int main() {
	try {
		TestTypedValuesAndDefaultPresence();
		TestInputValidationAndOrdering();
		TestTaggedRequiredInputReferences();
		TestStableIdentityValidation();
		TestQueryProjectionAndLifetime();
		TestNativeCompatibility();
		std::cout << "compiled package generation contract tests passed" << std::endl;
		return EXIT_SUCCESS;
	} catch (const std::exception &error) {
		std::cerr << "compiled package generation contract tests failed: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
