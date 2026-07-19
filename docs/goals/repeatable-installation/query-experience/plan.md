# Query Experience plan: install, restart, load, and query

## Outcome and status

Prove, from a DuckDB user's perspective, that the immutable `v0.1.0` artifact
can cross an installation boundary in an empty host state, survive a process
restart, load as `duckdb_api` by name, identify itself as version `0.1.0`, and
return the unchanged accepted query result. The same executable narrative must
make incompatible or untrusted inputs fail before extension code can run.

This is a bounded decision-evidence package. It does not publish an artifact,
add a supported installation command to the product surface, expand the
compatibility cell, or establish an ordinary-user trust posture.

## User-facing narrative

The oracle will exercise the installation lifecycle as distinct user-visible
states rather than treating a successful direct `LOAD` as installation proof:

1. Start with an empty DuckDB database and extension directory, verify the
   supplied artifact bundle, and install the artifact from an explicit local
   download path under the deliberately enabled unsigned-development policy.
2. Record the installed extension name, version, source, mode, and bytes; run a
   repeated install and prove that the installed bytes remain identical to the
   verified input.
3. End the host process, start a separate process with the same database and
   extension directory, `LOAD duckdb_api` by name, and verify version `0.1.0`.
4. Execute the accepted SQL and require its exact columns, types, ordering, and
   three rows. No repository build directory or source tree may be needed by
   either host process.

The output will distinguish artifact integrity from DuckDB extension trust.
A matching checksum binds the tested bytes to the evidence bundle; it does not
turn a self-built unsigned extension into an ordinarily trusted distribution.

## Query-owned artifacts

| Artifact | Primary reason to change |
| --- | --- |
| `experiments/repeatable-installation/README.md` | Summarize trial status and outcome and route a reproducer to the integrated command, results, and runbook |
| `experiments/repeatable-installation/RUNBOOK.md` | Document the exact trial invocation, user journey, input boundary, and non-product limitations |
| `experiments/repeatable-installation/install_oracle.py` | Preserve the public CLI and compose admitted inputs, temporary state, scenario execution, and retained evidence |
| `experiments/repeatable-installation/trial_inputs.py` | Preserve explicit path admission and compose provider, manifest, and fixture verification into the immutable `VerifiedBundle` consumed by scenarios |
| `experiments/repeatable-installation/trial_snapshot.py` | Freeze byte-bearing provider inputs, the self-contained verifier, and the Query host into private read-only trial storage before verification and use |
| `experiments/repeatable-installation/verifier_process.py` | Run the provider verifier with isolated Python, a minimal environment, hard output and time limits, and bounded path-normalized diagnostics |
| `experiments/repeatable-installation/verified_manifest.py` | Validate release artifact identity, the embedded public behavior contract, complete DuckDB identities, compatibility, and source platform |
| `experiments/repeatable-installation/negative_fixture_admission.py` | Validate the canonical inventory and independently reproduce the exact footer-field and body-byte transformations |
| `experiments/repeatable-installation/host_process.py` | Construct the minimal clean environment and hard-bounded, whole-process-group subprocess exchange with the DuckDB host edge |
| `experiments/repeatable-installation/installation_scenarios.py` | Own install/restart/load assertions and the four deterministic refusal scenarios |
| `experiments/repeatable-installation/evidence_output.py` | Build the versioned evidence object and normalize machine-local paths only after assertions finish |
| `experiments/repeatable-installation/query_host.py` | Run one isolated DuckDB host action and return structured installation, load, registration, diagnostic, and query observations |
| `experiments/repeatable-installation/test_query_oracle.py` | Preserve one documented focused-test entry point over the responsibility-matched test modules |
| `experiments/repeatable-installation/query_oracle_test_support.py` | Build the shared deterministic miniature manifest, artifact, inventory, and fixture inputs used by focused tests |
| `experiments/repeatable-installation/test_trial_inputs.py` | Prove virtual-environment path preservation and composed immutable input admission |
| `experiments/repeatable-installation/test_trial_snapshot.py` | Prove source replacement cannot change verified data or the frozen host harness |
| `experiments/repeatable-installation/test_verifier_process.py` | Prove Python-path and secret isolation plus verifier time and output bounds |
| `experiments/repeatable-installation/test_negative_fixture_admission.py` | Prove same-size counterfeit and wrong-inventory rejection |
| `experiments/repeatable-installation/test_host_process.py` | Prove minimal host environment, time and output bounds, and detached descendant process-group teardown |
| `experiments/repeatable-installation/test_installation_scenarios.py` | Execute the complete ordered matrix through a deterministic fake host and prove rejection state, identities, install source, containment, and corrupted-before-host behavior |
| `experiments/repeatable-installation/test_evidence_output.py` | Prove recursive longest-root evidence normalization |
| `experiments/repeatable-installation/test_install_oracle.py` | Prove the complete explicit provider-path CLI |
| `experiments/repeatable-installation/RESULTS.md` | Record commands, immutable input identities, observed outcomes, diagnostic excerpts, limitations, and unresolved product choices |

`install_oracle.py` is a thin CLI and composition root. Dependencies flow from
it to composed input admission, scenario execution, and evidence output.
`trial_inputs.py` depends on bounded verifier execution, verified manifest
semantics, and negative-fixture admission as separate Query responsibilities.
The snapshot seam freezes data, verifier authority, and the self-contained host
before verification; source-path changes cannot alter the later native load or
switch authority between the two verifier calls. Scenarios consume
`HostRunner`; only `HostRunner` invokes the frozen host, which owns DuckDB-facing
process behavior and does not locate or build artifacts, rewrite extension
metadata, or decide distribution policy. Evidence output receives completed
observations and cannot authorize a host action. No Query module imports
Engineering Enablement build, verifier, fixture-generation, or CI internals.
Focused tests follow the same boundaries, sharing only a miniature input
builder. `RESULTS.md` reports evidence and limitations without making the
technical or product decision reserved for RFC 0004.

No production source, public SQL contract, release pin, artifact generator,
CI workflow, or RFC file belongs to this package.

## Engineering Enablement input contract

Query consumes explicit filesystem paths and versioned data, not knowledge of
release-gate directory layout. The provider package supplies:

- a DuckDB 1.5.4 Python executable for the accepted `osx_arm64` cell;
- a DuckDB 1.5.3 Python executable for the mismatched-host refusal;
- the immutable `v0.1.0` extension artifact, release manifest, and anchored
  manifest digest;
- a self-contained verifier whose content identity authorizes the exact
  manifest and artifact before installation; the manifest in turn binds source
  commit and tree, DuckDB target, platform, extension name and version, and the
  public contract;
- a documented synthetic wrong-platform fixture whose own test-fixture
  identity is stable but which is never represented as a release artifact;
- a deterministic body-corrupted copy paired with the original manifest so
  pre-install verification must reject it;
- a canonical `duckdb_api/installability-negative-fixtures/v1` inventory that
  binds the source and both outputs by filename, size, digest, and one exact
  recorded transformation each; and
- retained CI evidence proving that the intended artifact bundle, rather than
  only a log file, survives the custody path.

The Query invocation accepts all paths, including the negative-fixture
inventory, as command-line arguments and uses only temporary database and
extension directories. The verifier must be callable as a documented command
or exchange a versioned result; Query must not import its implementation. Paths
may point at local release evidence for this trial, but the oracle must not
encode those local paths.

The wrong-platform fixture is intentionally a host-compatibility probe. Query
requires a canonical inventory, verifies all source and output identities, and
independently reproduces the recorded zero-padded footer-field replacement from
the original artifact before passing the fixture to DuckDB. It likewise
reproduces the corrupted fixture as exactly one recorded XOR body-byte mutation;
the original bundle verifier must then stop that copy before `INSTALL`.

## Deterministic acceptance and failure UX

The Query-owned oracle requires all of the following:

- **Supported install:** an empty host installs the verified artifact, exposes
  `duckdb_api` as installed version `0.1.0`, and records a custom-path install
  source equal to the admitted artifact without claiming a public repository.
- **Restart and load:** a new process loads `duckdb_api` by name and returns the
  exact accepted schema and three rows.
- **Host identity:** every supported, signature, mismatched-version, and
  wrong-platform observation reports the complete manifest-derived DuckDB
  version and ten-character source commit.
- **Repeated install:** a second install is deterministic; the installed-file
  checksum remains the verified artifact checksum before restart and load.
- **Default trust policy:** without the explicit unsigned-development setting,
  DuckDB refuses installation with an invalid-signature diagnostic and no
  `duckdb_api` function becomes registered.
- **Wrong DuckDB version:** DuckDB 1.5.3 reports that the extension targets
  1.5.4, refuses before registration, and leaves no `duckdb_api` function.
- **Wrong platform:** the accepted host reports the synthetic platform mismatch
  before registration and leaves no `duckdb_api` function.
- **Corrupted bytes:** bundle verification reports an artifact checksum or size
  mismatch before DuckDB starts, creates no installed extension, and therefore
  cannot register extension functions.

Assertions will match stable facts and required diagnostic content rather than
entire upstream error strings. Each rejected-host probe uses a new database and
extension directory so state from a successful scenario cannot satisfy it.
The process boundary, empty state, installed checksum, extension inventory, and
function inventory are part of the oracle rather than narrative-only claims.
Verifier and host processes inherit no caller environment: they receive only
clean HOME, temporary, cache, and configuration roots plus a fixed PATH and
locale. Every action has hard time and output bounds whose failure kills the
isolated process group and reaps the direct child before the oracle reports an
actionable lifecycle assertion. TERM grace is always followed by group KILL,
including when a descendant ignores TERM and closes inherited pipes. Verifier
diagnostics are bounded and path-normalized.
The process-group policy is not a hostile-code sandbox: code that deliberately
creates a new session can escape the group. The trial relies on exact trusted
bytes and does not claim malicious native-code containment.

## Dependencies, parallelism, and overlap risks

- Frozen product base: `f855dfb5f5de0be7cb8ffd6a58d54552aeaada8d`
  (`v0.1.0`). Query does not rebuild or alter that product artifact.
- Engineering Enablement owns artifact acquisition or reproduction, manifest
  verification mechanics, negative-fixture construction, CI evidence custody,
  and any provider-side tests.
- Query Experience owns only the experiment files listed above and this
  Query-local plan. The lead agent owns
  `docs/goals/repeatable-installation.md`, RFC 0004, integration, review
  disposition, and Git history.
- The packages can proceed in parallel once the provider publishes the input
  paths and verifier invocation. The integration dependency is data and a CLI,
  not a shared Python module.
- The only directory-level overlap risk is
  `experiments/repeatable-installation/`. Parallel writers must not edit the
  Query-owned files in the artifact table; provider files remain under
  `enablement/` or use separately assigned names.
- Any need for Query to inspect build roots, CI artifact layout, footer-writing
  code, or verifier internals is a failed interface and keeps the facilitation
  interaction open.

## Interaction exit and verification

Engineering Enablement facilitates artifact provenance and compatibility-test
practice. The interaction exits when Query Experience can run and maintain the
documented oracle from the stable path-and-data interface, reproduce every
positive and negative result in clean temporary state, and diagnose an input
failure without understanding build, CI, or custody internals. At that point,
the oracle and its results remain Query-owned while the provider independently
maintains artifact production and verification.

Focused evidence starts with
`python3 -I experiments/repeatable-installation/test_query_oracle.py`, including
special-file and oversize rejection, same-size counterfeit, wrong-inventory,
source-replacement, full scenario composition, Python-path injection,
inherited-secret, output-flood, sleeping-process, and detached descendant
regressions. It then runs the
documented oracle against every supplied input,
repository asset validation, source-identity verification, the applicable fresh
product gate, and Git whitespace checks. Final review must cover clean-host
state, compatibility diagnostics, process lifecycle, supply-chain boundaries,
and whether each negative path proves absence of registration rather than
merely observing a nonzero exit.

## RFC boundary

The bounded experiment is exempt from an RFC only while its outputs remain
decision evidence and no ordinary-user channel or compatibility promise is
published. Query Experience sponsors product RFC 0004 because the eventual
decision changes the DuckDB user's installation, trust, compatibility, update,
and support experience. Engineering Enablement participates as facilitator and
reviews the delivery and custody consequences.

During the bounded trial, RFC 0004 remained Draft and this plan did not
authorize presenting process-wide unsigned extension loading as the normal
product path. On 2026-07-17, the product manager approved the remaining policy,
both required teams approved the decision, and RFC 0004 was Accepted. Community
proof and the Query–Enablement exit now belong to the full `0.2.0` delivery
goal.
