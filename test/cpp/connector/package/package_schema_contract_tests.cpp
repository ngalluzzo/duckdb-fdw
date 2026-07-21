#include "compiler_test_support.hpp"

#include <iostream>

namespace {

using duckdb_api::connector::PackageDiagnosticCode;
using duckdb_api::connector::PackageDiagnosticPhase;
using duckdb_api_test::NeverCancel;
using duckdb_api_test::Require;
using duckdb_api_test::TemporaryPackage;

std::string GithubRelation(const std::string &name) {
	return duckdb_api_test::ReadFile("connectors/github/relations/" + name + ".yaml");
}

void RequireFirstDiagnostic(const duckdb_api::connector::PackageCompileResult &result, PackageDiagnosticCode code,
                            PackageDiagnosticPhase phase, const std::string &message) {
	Require(!result.Succeeded() && result.Generation() == nullptr && !result.Diagnostics().empty() &&
	            result.Diagnostics()[0].Code() == code && result.Diagnostics()[0].Phase() == phase,
	        message);
}

void TestClosedSchemaAndAllOrNothing() {
	TemporaryPackage package;
	duckdb_api_test::WriteGithubPackage(package);
	package.Write("relations/authenticated_user.yaml",
	              duckdb_api_test::ReplaceOnce(GithubRelation("authenticated_user"), "schema: static\n",
	                                           "schema: static\nfuture_capability: true\n"));
	NeverCancel cancellation;
	const auto result = duckdb_api_test::CompileRoot(package.Root(), cancellation);
	RequireFirstDiagnostic(result, PackageDiagnosticCode::UNKNOWN_FIELD, PackageDiagnosticPhase::SCHEMA,
	                       "unknown stable source field did not fail the complete candidate");
	Require(result.Diagnostics()[0].Coordinate().file == "relations/authenticated_user.yaml" &&
	            result.Diagnostics()[0].Coordinate().yaml_path == "$.future_capability",
	        "closed-schema diagnostic lost its exact safe source coordinate");
}

void TestCrossFileAndPolicyReferences() {
	{
		TemporaryPackage package;
		duckdb_api_test::WriteGithubPackage(package);
		package.Write("relations/authenticated_user.yaml",
		              duckdb_api_test::ReplaceOnce(GithubRelation("authenticated_user"), "id: authenticated_user",
		                                           "id: another_user"));
		NeverCancel cancellation;
		RequireFirstDiagnostic(duckdb_api_test::CompileRoot(package.Root(), cancellation),
		                       PackageDiagnosticCode::INVALID_REFERENCE, PackageDiagnosticPhase::REFERENCE,
		                       "relation file identity mismatch escaped cross-file validation");
	}
	{
		TemporaryPackage package;
		duckdb_api_test::WriteGithubPackage(package);
		package.Write("relations/authenticated_user.yaml",
		              duckdb_api_test::ReplaceOnce(GithubRelation("authenticated_user"), "host: api.github.com",
		                                           "host: outside.example"));
		NeverCancel cancellation;
		const auto result = duckdb_api_test::CompileRoot(package.Root(), cancellation);
		Require(!result.Succeeded(), "operation origin widened manifest network authority");
		bool found = false;
		for (const auto &diagnostic : result.Diagnostics()) {
			found = found || (diagnostic.Code() == PackageDiagnosticCode::POLICY_WIDENING &&
			                  diagnostic.Phase() == PackageDiagnosticPhase::COMPILE);
		}
		Require(found, "network widening did not use the stable policy diagnostic");
	}
}

void TestTypedDefaults() {
	TemporaryPackage package;
	package.Write("connector.yaml", R"YAML(api_version: duckdb_api/v1
kind: connector
id: typed_defaults
version: 1.0.0
extractor_dialect: duckdb_api/json_path_v1
network_policy:
  origins:
    - {scheme: https, host: defaults.example, port: 443}
  redirects: deny
  private_addresses: deny
  link_local_addresses: deny
  loopback_addresses: deny
  max_response_bytes: 4096
relations: [records]
)YAML");
	package.Write("relations/records.yaml", R"YAML(api_version: duckdb_api/v1
kind: relation
id: records
schema: static
columns:
  - {id: value, type: VARCHAR, nullable: false, extract: $.value}
inputs:
  - {id: enabled, type: BOOLEAN, nullable: false, default: {kind: value, value: false}}
  - {id: count, type: BIGINT, nullable: false, default: {kind: value, value: -42}}
  - {id: label, type: VARCHAR, nullable: false, default: {kind: value, value: "private"}}
  - {id: cursor, type: VARCHAR, nullable: true, default: {kind: null}}
auth: {mode: anonymous}
resources:
  max_response_bytes_per_page: 4096
  max_response_bytes_per_scan: 4096
  max_records_per_page: 16
  max_records_per_scan: 16
  max_extracted_string_bytes: 256
operations:
  - id: all_records
    fallback: true
    cardinality: many
    replay_safety: safe
    request:
      protocol: rest
      method: GET
      origin: {scheme: https, host: defaults.example, port: 443}
      path: /records
      query: []
      headers: []
    response: {source: root_array}
    pagination: {strategy: disabled}
)YAML");
	NeverCancel cancellation;
	const auto result = duckdb_api_test::CompileRoot(package.Root(), cancellation);
	Require(result.Succeeded(), "valid typed defaults package did not compile");
	const auto &inputs = result.Generation()->Connector().Relations()[0].Inputs();
	Require(inputs.size() == 4 && !inputs[0].Default().Value().Boolean() &&
	            inputs[1].Default().Value().Bigint() == -42 && inputs[2].Default().Value().Varchar() == "private" &&
	            inputs[3].Nullable() && inputs[3].Default().Value().IsNull() &&
	            inputs[3].Default().Value().Type() == duckdb_api::CompiledScalarType::VARCHAR,
	        "compiler collapsed typed defaults, typed NULL, or source order");
}

void TestDiagnosticBudgetAndCancellation() {
	TemporaryPackage package;
	duckdb_api_test::WriteGithubPackage(package);
	package.Write("relations/authenticated_user.yaml",
	              duckdb_api_test::ReplaceOnce(GithubRelation("authenticated_user"), "schema: static\n",
	                                           "schema: static\nunknown_a: true\nunknown_b: true\nunknown_c: true\n"));
	NeverCancel cancellation;
	const auto snapshot = duckdb_api::connector::AcquirePackageSource(
	    package.Root(), duckdb_api::connector::PackageSourceLimits::V1(), cancellation);
	auto limits = duckdb_api::connector::PackageCompilerLimits::V1();
	limits.max_diagnostics = 2;
	const auto result = duckdb_api::connector::CompilePackage(snapshot, limits, cancellation);
	Require(!result.Succeeded() && result.Diagnostics().size() == 2 &&
	            result.Diagnostics()[1].Code() == PackageDiagnosticCode::RESOURCE_EXHAUSTED,
	        "diagnostic budget did not retain one detail plus its terminal resource record");
	duckdb_api_test::AlwaysCancel cancelled;
	try {
		(void)duckdb_api::connector::CompilePackage(snapshot, duckdb_api::connector::PackageCompilerLimits::V1(),
		                                            cancelled);
	} catch (const duckdb_api::connector::FailsafeYamlError &error) {
		Require(error.Code() == duckdb_api::connector::FailsafeYamlErrorCode::CANCELLED,
		        "compiler cancellation used another error boundary");
		return;
	}
	throw std::runtime_error("compiler ignored call-scoped cancellation");
}

void TestCompiledModelCounterexamplesStayDiagnostics() {
	for (const auto &source :
	     {duckdb_api_test::ReplaceOnce(GithubRelation("authenticated_repositories"), "literal: \"100\"",
	                                   "literal: \"99\""),
	      duckdb_api_test::ReplaceOnce(GithubRelation("authenticated_repositories"), "first_page: 1",
	                                   "first_page: 9223372036854775807"),
	      duckdb_api_test::ReplaceOnce(GithubRelation("authenticated_user"), "max_response_bytes_per_scan: 65536",
	                                   "max_response_bytes_per_scan: 65537")}) {
		TemporaryPackage package;
		duckdb_api_test::WriteGithubPackage(package);
		const bool user_relation = source.find("id: authenticated_user") != std::string::npos;
		package.Write(user_relation ? "relations/authenticated_user.yaml" : "relations/authenticated_repositories.yaml",
		              source);
		NeverCancel cancellation;
		const auto result = duckdb_api_test::CompileRoot(package.Root(), cancellation);
		Require(!result.Succeeded() && result.Generation() == nullptr && !result.Diagnostics().empty(),
		        "invalid compiled-model counterexample escaped as a generation or exception");
	}
}

void TestHeaderValueProfile() {
	{
		TemporaryPackage package;
		duckdb_api_test::WriteGithubPackage(package);
		package.Write("relations/authenticated_user.yaml",
		              duckdb_api_test::ReplaceOnce(GithubRelation("authenticated_user"),
		                                           "value: application/vnd.github+json", "value: \"\""));
		NeverCancel cancellation;
		const auto result = duckdb_api_test::CompileRoot(package.Root(), cancellation);
		Require(
		    result.Succeeded() &&
		        result.Generation()->Connector().Relations()[1].Operations()[0].Rest().request.headers[0].value.empty(),
		    "the accepted empty HTTP field-value was rejected or changed");
	}
	{
		TemporaryPackage package;
		duckdb_api_test::WriteGithubPackage(package);
		package.Write("relations/authenticated_user.yaml",
		              duckdb_api_test::ReplaceOnce(GithubRelation("authenticated_user"),
		                                           "value: application/vnd.github+json", "value: \" leading\""));
		NeverCancel cancellation;
		RequireFirstDiagnostic(duckdb_api_test::CompileRoot(package.Root(), cancellation),
		                       PackageDiagnosticCode::UNSUPPORTED_DECLARATION, PackageDiagnosticPhase::SCHEMA,
		                       "leading optional whitespace escaped the HTTP field-value profile");
	}
}

void TestExtractorByteLimit() {
	const auto at_limit = "$." + std::string(1022, 'a');
	const auto one_over = at_limit + "a";
	{
		TemporaryPackage package;
		duckdb_api_test::WriteGithubPackage(package);
		package.Write("relations/authenticated_user.yaml",
		              duckdb_api_test::ReplaceOnce(GithubRelation("authenticated_user"), "extract: $.id",
		                                           "extract: " + at_limit));
		NeverCancel cancellation;
		Require(duckdb_api_test::CompileRoot(package.Root(), cancellation).Succeeded(),
		        "the maximum 1024-byte field extractor was rejected");
	}
	{
		TemporaryPackage package;
		duckdb_api_test::WriteGithubPackage(package);
		package.Write("relations/authenticated_user.yaml",
		              duckdb_api_test::ReplaceOnce(GithubRelation("authenticated_user"), "extract: $.id",
		                                           "extract: " + one_over));
		NeverCancel cancellation;
		RequireFirstDiagnostic(duckdb_api_test::CompileRoot(package.Root(), cancellation),
		                       PackageDiagnosticCode::INVALID_EXTRACTOR, PackageDiagnosticPhase::SCHEMA,
		                       "a 1025-byte field extractor escaped the v1 byte limit");
	}
}

void TestInvalidIdentifiersStayDiagnosticOnly() {
	TemporaryPackage package;
	duckdb_api_test::WriteGithubPackage(package);
	package.Write("connector.yaml", duckdb_api_test::ReplaceOnce(
	                                    duckdb_api_test::ReadFile("connectors/github/connector.yaml"),
	                                    "id: github", "id: \"NOT SAFE!\""));
	package.Write("relations/authenticated_user.yaml",
	              duckdb_api_test::ReplaceOnce(GithubRelation("authenticated_user"), "id: authenticated_user",
	                                           "id: another_user"));
	NeverCancel cancellation;
	const auto result = duckdb_api_test::CompileRoot(package.Root(), cancellation);
	Require(!result.Succeeded() && !result.Diagnostics().empty(),
	        "invalid source identifiers escaped as a generation or exception");
	for (const auto &diagnostic : result.Diagnostics()) {
		Require(diagnostic.Connector().empty() || diagnostic.Connector() == "github",
		        "an unvalidated author value entered the safe diagnostic identity");
	}
}

void TestDiagnosticCodePhaseContract() {
	{
		TemporaryPackage package;
		duckdb_api_test::WriteGithubPackage(package);
		package.Write("relations/authenticated_user.yaml",
		              duckdb_api_test::ReplaceOnce(GithubRelation("authenticated_user"), "port: 443", "port: 0"));
		NeverCancel cancellation;
		RequireFirstDiagnostic(duckdb_api_test::CompileRoot(package.Root(), cancellation),
		                       PackageDiagnosticCode::INVALID_TYPE, PackageDiagnosticPhase::SCHEMA,
		                       "an invalid HTTP port used a policy code or non-schema phase");
	}
	{
		TemporaryPackage package;
		duckdb_api_test::WriteGithubPackage(package);
		package.Write("connector.yaml", duckdb_api_test::ReplaceOnce(
		                                    duckdb_api_test::ReadFile("connectors/github/connector.yaml"),
		                                    "redirects: deny", "redirects: allow"));
		NeverCancel cancellation;
		RequireFirstDiagnostic(duckdb_api_test::CompileRoot(package.Root(), cancellation),
		                       PackageDiagnosticCode::POLICY_WIDENING, PackageDiagnosticPhase::COMPILE,
		                       "network widening used a schema phase outside the closed phase vocabulary");
	}
}

} // namespace

int main() {
	try {
		TestClosedSchemaAndAllOrNothing();
		TestCrossFileAndPolicyReferences();
		TestTypedDefaults();
		TestDiagnosticBudgetAndCancellation();
		TestCompiledModelCounterexamplesStayDiagnostics();
		TestHeaderValueProfile();
		TestExtractorByteLimit();
		TestInvalidIdentifiersStayDiagnosticOnly();
		TestDiagnosticCodePhaseContract();
		std::cout << "package schema contract tests passed" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << std::endl;
		return 1;
	}
}
