#include "connector/support/package_compiler_test_fixtures.hpp"

#include "duckdb_api/local_package_compiler.hpp"
#include "support/require.hpp"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

using duckdb_api::CompiledColumn;
using duckdb_api::CompiledConnector;
using duckdb_api::CompiledHttpMethod;
using duckdb_api::CompiledOperation;
using duckdb_api::CompiledOperationCardinality;
using duckdb_api::CompiledPaginationStrategy;
using duckdb_api::CompiledProtocol;
using duckdb_api::CompiledQueryEncoding;
using duckdb_api::CompiledQueryValueSource;
using duckdb_api::CompiledRelation;
using duckdb_api::CompiledReplaySafety;
using duckdb_api::CompiledResponseSource;
using duckdb_api::CompiledUrlScheme;
using duckdb_api_test::MigrationProfile;
using duckdb_api_test::MigrationReplacement;
using duckdb_api_test::Require;

// The one compiled field that legitimately varies with the package envelope:
// each profile's network policy admits a different operation origin host, so
// the canonical relation's origin host tracks its envelope. Everything else in
// the compiled relation must be byte-for-byte equivalent across envelopes.
bool EnvelopeHostForProfile(MigrationProfile profile, const std::string &host) {
	return host == (profile == MigrationProfile::GITHUB ? "api.github.com" : "rickandmortyapi.com");
}

void RequireWellFormedDigest(const std::string &digest, const std::string &label) {
	Require(digest.size() == 71 && digest.substr(0, 7) == "sha256.",
	        label + " package digest was not a well-formed sha256 digest");
	for (std::size_t index = 7; index < digest.size(); index++) {
		const auto c = digest[index];
		Require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'),
		        label + " package digest carried a non-hex character");
	}
}

void RequireEquivalentColumns(const std::vector<CompiledColumn> &github,
                              const std::vector<CompiledColumn> &rickandmorty) {
	Require(github.size() == 2 && rickandmorty.size() == 2,
	        "migration probe lost its two canonical columns across an envelope");
	for (std::size_t index = 0; index < github.size(); index++) {
		const auto &left = github[index];
		const auto &right = rickandmorty[index];
		Require(left.name == right.name && left.logical_type == right.logical_type && left.nullable == right.nullable &&
		            left.extractor == right.extractor && left.ScalarType() == right.ScalarType() &&
		            left.ExtractorSegments() == right.ExtractorSegments(),
		        "migration probe column shape drifted across package envelopes");
	}
	Require(github[0].name == "id" && github[0].ScalarType() == duckdb_api::CompiledScalarType::BIGINT &&
	            github[1].name == "name" && github[1].ScalarType() == duckdb_api::CompiledScalarType::VARCHAR,
	        "migration probe lost its canonical column identity");
}

void RequireEquivalentInputs(const CompiledRelation &github, const CompiledRelation &rickandmorty) {
	Require(github.Inputs().size() == 1 && rickandmorty.Inputs().size() == 1,
	        "migration probe lost its single relation input across an envelope");
	const auto &left = github.Inputs()[0];
	const auto &right = rickandmorty.Inputs()[0];
	Require(left.Name() == right.Name() && left.Name() == "status" && left.Type() == right.Type() &&
	            left.Type() == duckdb_api::CompiledScalarType::VARCHAR && left.Nullable() == right.Nullable() &&
	            left.Nullable() && left.Default().HasDefault() == right.Default().HasDefault() &&
	            !left.Default().HasDefault(),
	        "migration probe relation input drifted across package envelopes");
}

void RequireEquivalentRestOperation(const CompiledOperation &github, const CompiledOperation &rickandmorty,
                                    MigrationProfile left_profile, MigrationProfile right_profile) {
	Require(github.name == "fetch_probe" && rickandmorty.name == github.name && github.fallback &&
	            rickandmorty.fallback == github.fallback &&
	            github.cardinality == CompiledOperationCardinality::ZERO_TO_MANY &&
	            rickandmorty.cardinality == github.cardinality && github.Protocol() == CompiledProtocol::REST &&
	            rickandmorty.Protocol() == github.Protocol(),
	        "migration probe operation identity drifted across package envelopes");

	const auto &left = github.Rest();
	const auto &right = rickandmorty.Rest();
	Require(left.method == CompiledHttpMethod::GET && right.method == left.method &&
	            left.replay_safety == CompiledReplaySafety::SAFE && right.replay_safety == left.replay_safety &&
	            left.retry_enabled == false && right.retry_enabled == left.retry_enabled &&
	            left.pagination.Strategy() == CompiledPaginationStrategy::DISABLED &&
	            right.pagination.Strategy() == left.pagination.Strategy(),
	        "migration probe REST method/safety/pagination drifted across package envelopes");
	// duckdb_api/v1 carries only tagged required references and never the legacy
	// compatibility bridge; the canonical relation declares none, so both
	// envelopes must expose an empty, non-legacy selector.
	Require(github.selector.IsLegacyCompatibilityBridge() == false &&
	            rickandmorty.selector.IsLegacyCompatibilityBridge() == false &&
	            github.selector.RequiredInputReferences().empty() &&
	            rickandmorty.selector.RequiredInputReferences().empty(),
	        "migration probe operation selector drifted across package envelopes");
	Require(left.response_source == CompiledResponseSource::JSON_PATH_MANY &&
	            right.response_source == left.response_source && left.records_extractor == "$.results[*]" &&
	            right.records_extractor == left.records_extractor &&
	            left.records_extractor_segments == right.records_extractor_segments &&
	            left.records_extractor_segments.size() == 1 && left.records_extractor_segments[0] == "results",
	        "migration probe REST response shape drifted across package envelopes");

	Require(left.request.path == "/api/probe" && right.request.path == left.request.path &&
	            left.request.origin.scheme == CompiledUrlScheme::HTTPS &&
	            right.request.origin.scheme == left.request.origin.scheme && left.request.origin.port == 443 &&
	            right.request.origin.port == left.request.origin.port,
	        "migration probe REST request envelope drifted across package envelopes");
	Require(EnvelopeHostForProfile(left_profile, left.request.origin.host.Value()) &&
	            EnvelopeHostForProfile(right_profile, right.request.origin.host.Value()) &&
	            left.request.origin.host.Value() != right.request.origin.host.Value(),
	        "migration probe origin host did not track its envelope policy");

	Require(left.request.headers.size() == 0 && right.request.headers.size() == left.request.headers.size(),
	        "migration probe gained envelope-specific REST headers");

	Require(left.request.query_parameters.size() == 1 &&
	            right.request.query_parameters.size() == left.request.query_parameters.size(),
	        "migration probe lost its single input-bound query field across an envelope");
	const auto &left_param = left.request.query_parameters[0];
	const auto &right_param = right.request.query_parameters[0];
	Require(left_param.name == "status" && right_param.name == left_param.name &&
	            left_param.source == CompiledQueryValueSource::RELATION_INPUT &&
	            right_param.source == left_param.source && left_param.source_id == "status" &&
	            right_param.source_id == left_param.source_id &&
	            left_param.encoding == CompiledQueryEncoding::FORM_URLENCODED &&
	            right_param.encoding == left_param.encoding && left_param.omit_when_unbound == true &&
	            right_param.omit_when_unbound == left_param.omit_when_unbound && left_param.omit_when_null == true &&
	            right_param.omit_when_null == left_param.omit_when_null && !left_param.HasDecodedValue() &&
	            !right_param.HasDecodedValue(),
	        "migration probe input-bound query field drifted across package envelopes");
}

void RequireEquivalentRelation(const CompiledRelation &github, const CompiledRelation &rickandmorty,
                               MigrationProfile left_profile, MigrationProfile right_profile) {
	Require(github.Name() == "migration_probe" && rickandmorty.Name() == github.Name(),
	        "migration probe relation name drifted across package envelopes");
	RequireEquivalentColumns(github.Columns(), rickandmorty.Columns());
	RequireEquivalentInputs(github, rickandmorty);
	// The canonical relation declares no predicate mappings; both envelopes must
	// expose that empty set so a future envelope-specific predicate cannot pass
	// undetected.
	Require(github.PredicateMappings().empty() && rickandmorty.PredicateMappings().empty() &&
	            rickandmorty.PredicateMappings().size() == github.PredicateMappings().size(),
	        "migration probe predicate mappings drifted across package envelopes");
	Require(github.Operations().size() == 1 && rickandmorty.Operations().size() == 1 && github.HasSingleOperation() &&
	            rickandmorty.HasSingleOperation(),
	        "migration probe lost its single operation across an envelope");
	RequireEquivalentRestOperation(github.Operations()[0], rickandmorty.Operations()[0], left_profile, right_profile);

	// The auth-independence proof: an anonymous relation compiles to an
	// identical authentication obligation whether or not the surrounding
	// package declares bearer credentials for other relations. The github
	// profile carries GitHub's real credential; the rickandmorty profile has
	// none, yet both anonymous relations carry obligation NONE.
	Require(github.Authentication().Requirement() == duckdb_api::CompiledCredentialRequirement::NONE &&
	            rickandmorty.Authentication().Requirement() == github.Authentication().Requirement() &&
	            github.Authentication().Authenticator() == duckdb_api::CompiledAuthenticator::NONE &&
	            rickandmorty.Authentication().Authenticator() == github.Authentication().Authenticator() &&
	            github.Authentication().Placement() == duckdb_api::CompiledCredentialPlacement::NONE &&
	            rickandmorty.Authentication().Placement() == github.Authentication().Placement(),
	        "anonymous migration probe gained an envelope-specific authentication obligation");

	const auto &github_resources = github.ResourceCeilings();
	const auto &rickandmorty_resources = rickandmorty.ResourceCeilings();
	Require(github_resources.HasResponseByteNarrowing() == rickandmorty_resources.HasResponseByteNarrowing() &&
	            github_resources.HasResponseByteNarrowing() && github_resources.MaxResponseBytesPerPage() == 65536 &&
	            rickandmorty_resources.MaxResponseBytesPerPage() == github_resources.MaxResponseBytesPerPage() &&
	            github_resources.MaxResponseBytesPerScan() == 65536 &&
	            rickandmorty_resources.MaxResponseBytesPerScan() == github_resources.MaxResponseBytesPerScan() &&
	            github_resources.MaxRecordsPerPage() == 20 &&
	            rickandmorty_resources.MaxRecordsPerPage() == github_resources.MaxRecordsPerPage() &&
	            github_resources.MaxRecordsPerScan() == 20 &&
	            rickandmorty_resources.MaxRecordsPerScan() == github_resources.MaxRecordsPerScan() &&
	            github_resources.MaxExtractedStringBytes() == 256 &&
	            rickandmorty_resources.MaxExtractedStringBytes() == github_resources.MaxExtractedStringBytes(),
	        "migration probe resource ceilings drifted across package envelopes");
}

void RequireEquivalentRegistrationView(const duckdb_api::CompiledQueryRegistrationView &github,
                                       const duckdb_api::CompiledQueryRegistrationView &rickandmorty) {
	Require(github.Relations().size() == 1 && rickandmorty.Relations().size() == 1 &&
	            github.Relations()[0].Name() == "migration_probe" &&
	            rickandmorty.Relations()[0].Name() == github.Relations()[0].Name() &&
	            github.Relations()[0].Authentication() == duckdb_api::CompiledRegistrationAuthentication::ANONYMOUS &&
	            rickandmorty.Relations()[0].Authentication() == github.Relations()[0].Authentication() &&
	            github.Relations()[0].Columns().size() == 2 && rickandmorty.Relations()[0].Columns().size() == 2 &&
	            github.Relations()[0].Inputs().size() == 1 && rickandmorty.Relations()[0].Inputs().size() == 1 &&
	            github.GenerationHandle().IsValid() && rickandmorty.GenerationHandle().IsValid(),
	        "migration probe Query registration projection drifted across package envelopes");
}

void TestEquivalentInputsCompileToEquivalentOutput(const std::string &repository_root) {
	const auto fixture = duckdb_api_test::BuildRepositoryCrossPackageMigrationFixture(repository_root);
	const auto &github_generation = fixture.github_profile.Generation();
	const auto &rickandmorty_generation = fixture.rickandmorty_profile.Generation();

	// Identity: spec, kind-bearing version, and digest format are shared; the
	// connector id and the content-addressed digest differ exactly as the two
	// distinct envelopes require, and neither envelope accidentally reproduces
	// a real repository package.
	Require(github_generation.Identity().SpecIdentifier() == "duckdb_api/v1" &&
	            rickandmorty_generation.Identity().SpecIdentifier() == github_generation.Identity().SpecIdentifier(),
	        "migration envelopes lost their shared spec identifier");
	Require(github_generation.Identity().PackageVersion() == "1.0.0" &&
	            rickandmorty_generation.Identity().PackageVersion() == "2.0.0",
	        "migration envelopes lost their real package versions");
	Require(github_generation.Identity().ConnectorId() == "github_migration" &&
	            rickandmorty_generation.Identity().ConnectorId() == "rickandmorty_migration",
	        "migration envelopes lost their distinct connector identities");
	const auto github_digest = github_generation.Identity().PackageDigest();
	const auto rickandmorty_digest = rickandmorty_generation.Identity().PackageDigest();
	RequireWellFormedDigest(github_digest, "github-profile");
	RequireWellFormedDigest(rickandmorty_digest, "rickandmorty-profile");
	Require(github_digest != rickandmorty_digest, "distinct migration envelopes produced an identical package digest");
	const std::string real_github_digest = "sha256.b286e6f7481b437b243dfe2ce017a59d601d909272b9d2b35788fb78753ff23b";
	const std::string real_rickandmorty_digest =
	    "sha256.e4ba9694e6f622a9f7b4024ef95efc5b9bd73e441680b1e94b8c947e764df31c";
	Require(github_digest != real_github_digest && github_digest != real_rickandmorty_digest &&
	            rickandmorty_digest != real_github_digest && rickandmorty_digest != real_rickandmorty_digest,
	        "migration envelope reproduced a real repository package digest");
	Require(github_generation.QueryRegistration().Identity().PackageDigest() == github_digest &&
	            rickandmorty_generation.QueryRegistration().Identity().PackageDigest() == rickandmorty_digest,
	        "migration Query registration view disagreed with its generation identity");

	const auto &github_connector = github_generation.Connector();
	const auto &rickandmorty_connector = rickandmorty_generation.Connector();
	Require(github_connector.Origin() == duckdb_api::CompiledConnectorOrigin::PACKAGE_COMPILED_METADATA &&
	            rickandmorty_connector.Origin() == github_connector.Origin() &&
	            github_connector.Relations().size() == 1 &&
	            rickandmorty_connector.Relations().size() == github_connector.Relations().size(),
	        "migration envelopes lost their shared package-compiled provenance and relation count");

	RequireEquivalentRelation(github_connector.Relations()[0], rickandmorty_connector.Relations()[0],
	                          MigrationProfile::GITHUB, MigrationProfile::RICK_AND_MORTY);
	RequireEquivalentRegistrationView(github_generation.QueryRegistration(),
	                                  rickandmorty_generation.QueryRegistration());
}

// Asserts the two compile results describe the same diagnostic set: same
// count, and pairwise-identical code, phase, package-relative coordinate,
// relation, and operation. The connector identifier is the one field that
// legitimately varies with the envelope (and may be empty for manifest-level
// diagnostics emitted before the connector id is validated), so it is the
// expected difference rather than part of the comparison. Diagnostics are
// emitted in a deterministic order driven by the compiler's validation passes,
// so pairwise index comparison proves set equivalence without sorting.
void RequireEquivalentFailure(const duckdb_api::connector::PackageCompileResult &github,
                              const duckdb_api::connector::PackageCompileResult &rickandmorty,
                              duckdb_api::connector::PackageDiagnosticCode expected_code,
                              duckdb_api::connector::PackageDiagnosticPhase expected_phase, const std::string &label) {
	Require(!github.Succeeded() && github.Package() == nullptr && github.Generation() == nullptr &&
	            !github.Diagnostics().empty(),
	        "github-profile " + label + " did not fail with diagnostics");
	Require(!rickandmorty.Succeeded() && rickandmorty.Package() == nullptr && rickandmorty.Generation() == nullptr &&
	            !rickandmorty.Diagnostics().empty(),
	        "rickandmorty-profile " + label + " did not fail with diagnostics");
	const auto &left = github.Diagnostics();
	const auto &right = rickandmorty.Diagnostics();
	Require(left.size() == right.size(), label + " diagnostic count drifted across package envelopes");
	for (std::size_t index = 0; index < left.size(); index++) {
		Require(left[index].Code() == expected_code && right[index].Code() == expected_code &&
		            left[index].Phase() == expected_phase && right[index].Phase() == expected_phase,
		        label + " diagnostic code/phase drifted across package envelopes");
		Require(left[index].Coordinate().file == right[index].Coordinate().file &&
		            left[index].Coordinate().line == right[index].Coordinate().line &&
		            left[index].Coordinate().column == right[index].Coordinate().column &&
		            left[index].Coordinate().yaml_path == right[index].Coordinate().yaml_path,
		        label + " package-relative coordinate drifted across package envelopes");
		Require(left[index].Relation() == right[index].Relation() &&
		            left[index].Operation() == right[index].Operation(),
		        label + " relation/operation identifier drifted across package envelopes");
	}
}
void TestEquivalentMalformedInputProducesEquivalentDiagnostics(const std::string &repository_root) {
	// The same one-over-the-connector-ceiling resource widening in the canonical
	// relation produces the same POLICY_WIDENING/COMPILE diagnostic in both
	// profiles: both real packages declare the same 8 MiB response ceiling, so
	// the relation's 8388609-byte narrowing widens policy identically.
	const std::vector<MigrationReplacement> widening {
	    {"max_response_bytes_per_page: 65536", "max_response_bytes_per_page: 8388609"}};
	const auto github =
	    duckdb_api_test::CompileMigrationEnvelopeWithMutation(repository_root, MigrationProfile::GITHUB, {}, widening);
	const auto rickandmorty = duckdb_api_test::CompileMigrationEnvelopeWithMutation(
	    repository_root, MigrationProfile::RICK_AND_MORTY, {}, widening);
	RequireEquivalentFailure(github, rickandmorty, duckdb_api::connector::PackageDiagnosticCode::POLICY_WIDENING,
	                         duckdb_api::connector::PackageDiagnosticPhase::COMPILE, "policy widening");
}

void TestUnsupportedSpecFailsIdenticallyAcrossPackages(const std::string &repository_root) {
	// duckdb_api/v3 is not an accepted spec identifier. The manifest schema
	// rejects it at the SCHEMA phase regardless of which package profile the
	// envelope was derived from.
	const std::vector<MigrationReplacement> unsupported_spec {
	    {"api_version: duckdb_api/v1", "api_version: duckdb_api/v3"}};
	const auto github = duckdb_api_test::CompileMigrationEnvelopeWithMutation(repository_root, MigrationProfile::GITHUB,
	                                                                          unsupported_spec, {});
	const auto rickandmorty = duckdb_api_test::CompileMigrationEnvelopeWithMutation(
	    repository_root, MigrationProfile::RICK_AND_MORTY, unsupported_spec, {});
	RequireEquivalentFailure(github, rickandmorty, duckdb_api::connector::PackageDiagnosticCode::UNSUPPORTED_SPEC,
	                         duckdb_api::connector::PackageDiagnosticPhase::SCHEMA, "unsupported spec");
}

void TestUnsupportedDialectFailsIdenticallyAcrossPackages(const std::string &repository_root) {
	// duckdb_api/unsupported is not an accepted extractor dialect. It is
	// rejected at the SCHEMA phase identically across both package profiles.
	const std::vector<MigrationReplacement> unsupported_dialect {
	    {"extractor_dialect: duckdb_api/json_path_v1", "extractor_dialect: duckdb_api/unsupported"}};
	const auto github = duckdb_api_test::CompileMigrationEnvelopeWithMutation(repository_root, MigrationProfile::GITHUB,
	                                                                          unsupported_dialect, {});
	const auto rickandmorty = duckdb_api_test::CompileMigrationEnvelopeWithMutation(
	    repository_root, MigrationProfile::RICK_AND_MORTY, unsupported_dialect, {});
	RequireEquivalentFailure(github, rickandmorty, duckdb_api::connector::PackageDiagnosticCode::UNSUPPORTED_DIALECT,
	                         duckdb_api::connector::PackageDiagnosticPhase::SCHEMA, "unsupported dialect");
}

} // namespace

int main(int argc, char **argv) {
	if (argc != 2) {
		std::cerr << "usage: cross_package_migration_tests ABSOLUTE_REPOSITORY_ROOT" << std::endl;
		return 1;
	}
	try {
		TestEquivalentInputsCompileToEquivalentOutput(argv[1]);
		TestEquivalentMalformedInputProducesEquivalentDiagnostics(argv[1]);
		TestUnsupportedSpecFailsIdenticallyAcrossPackages(argv[1]);
		TestUnsupportedDialectFailsIdenticallyAcrossPackages(argv[1]);
		std::cout << "cross-package migration tests passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "cross-package migration tests failed: " << error.what() << std::endl;
		return 1;
	}
}
