# Query Experience plan: stock Community install to accepted query

## Outcome and authority

Query Experience owns the `0.2.0` user journey from a stock latest-stable
DuckDB process to a correct query or an actionable installation diagnostic.
Delivery is governed by Accepted RFC 0004: only exact Community CI rows that
pass the complete lifecycle oracle become supported, every absent row remains
unclaimed, and ordinary guidance never enables unsigned extensions.

This plan is the team's implementation decomposition, not another source of
product policy. Engineering Enablement facilitates Community build,
provenance, dependency, signing, deployment, custody, and release-evidence
practice. Query consumes those results through bounded files and process
commands; it does not inspect or reproduce their implementation.

## User acceptance narrative

For every candidate support row, the same Query-owned oracle must:

1. Admit one exact `0.2.0` candidate, latest-stable DuckDB identity, Community
   platform identity, and Enablement-provided signed-deployment/custody record
   into a new empty host state.
2. Start stock DuckDB with default extension security, assert that unsigned
   loading was not enabled, and run `INSTALL duckdb_api FROM community`.
   Installation must identify the Community source and version `0.2.0` without
   loading or registering `duckdb_api_scan` as a side effect.
3. Repeat `INSTALL` in a separate process against the same extension directory
   and prove that name, version, source, destination, and installed bytes do
   not drift within the oracle run.
4. Exit and start another stock process, run `LOAD duckdb_api` by name, inspect
   the loaded extension and public inventory, and execute the unchanged SQL.
   The schema and ordered rows must equal `release/0.2.0/public_contract.json`.
5. In fresh state, run at least one evidence-backed incompatible Community
   artifact/host pairing. DuckDB must produce an actionable version or platform
   refusal before initialization and leave no `duckdb_api_scan` registration.
6. Emit a bounded, path-normalized result that binds the complete DuckDB,
   project, source, Community row, install channel, extension, contract, and
   process identities. A row is claimable only when this result and the
   Enablement evidence for the same identity both pass.

The oracle matches stable facts rather than entire upstream error strings.
Diagnostics may expose the safe version, platform, extension name, and failure
category, but not credentials, unrestricted environment values, or
machine-local paths.

## Responsibility and dependency map

| Query responsibility | Production or evidence home | Primary reason to change |
| --- | --- | --- |
| Project extension identity visible through DuckDB | `extension_config.cmake` and Query identity assertions | The immutable project/extension version changes; the internal example connector version does not change with `0.2.0` |
| DuckDB registration and lifecycle portability | `src/duckdb_api_extension.cpp`, `src/include/duckdb_api_extension.hpp`, and `test/cpp/duckdb_adapter_tests.cpp` | A passing Community row requires an evidence-backed adapter compatibility, callback, state, diagnostic, or exception-boundary correction; no speculative platform shim is allowed |
| Accepted public behavior | `release/0.2.0/public_contract.json`, `test/sql/duckdb_api.test`, and existing artifact/source-demo contract tests | Extension identity advances to `0.2.0` while the function, parameters, schema, rows, DuckDB-owned relational behavior, and diagnostics remain unchanged |
| Anchored deployment handoff | `test/python/community_installation/deployment_admission.py` | The exact native deployment v1 record, anchor, Community run/endpoint, transport/inner digests, or candidate/row binding changes |
| Community lifecycle composition | `test/python/community_installation/oracle.py` | The documented command, input admission, scenario ordering, and final success/failure outcome change |
| Stock host inventory, private launcher, and argv | `test/python/community_installation/launcher.py` | The explicit executable, virtual-environment configuration, DuckDB native module/package metadata inventory, private staging, or default signature-policy arguments change |
| Descriptor-stable host file admission | `test/python/community_installation/file_admission.py` | Bounded no-follow input reads, exact artifact size/digest admission, or O_EXCL staging changes |
| Caller roots and minimal environment | `test/python/community_installation/host_environment.py` | New real root admission or the child environment allowlist changes |
| Descriptor-bound persistent state | `test/python/community_installation/state_capability.py` | Named state admission, hidden child leaves, atomic restore, retained directory identity, or published-state containment changes |
| Bounded child-process lifecycle | `test/python/community_installation/process_lifecycle.py` | Output/time bounds, process-group cancellation, direct-child reaping, or teardown guarantees change |
| Stock-host process framing and safe observations | `test/python/community_installation/host_protocol.py` | Versioned JSON framing, exact row identity, bounded diagnostic vocabulary, or path redaction changes |
| One child-only DuckDB action | `test/python/community_installation/duckdb_action.py` | Stock DuckDB connection settings, Community install/load SQL, catalog observations, or exact query behavior changes |
| One stock-host process action | `test/python/community_installation/host_action.py` | Composition of an admitted launcher, state capability, bounded child action, and one framed observation changes |
| Install/repeat/restart/load/failure assertions | `test/python/community_installation/scenarios.py` | The user-visible lifecycle or required success and refusal observations change |
| Exact support-row admission | `test/python/community_installation/matrix.py` | The accepted candidate/signed-deployment/query identity join or the rule that only complete passing rows are claimable changes |
| Canonical Query evidence production | `test/python/community_installation/evidence.py` | The bounded, path-normalized v2 result construction or exclusive write boundary changes |
| Query evidence admission | `test/python/community_installation/evidence_admission.py` | The exact status-aware v2 shape, lifecycle completeness, digest admission, or matrix normalization changes |
| Responsibility-matched unit oracles | `test_state_capability.py`, `test_host_action.py`, the boundary `test_duckdb_action.py`, the install/refusal/query action suites, and other `test_*.py`, with explicit test-support modules limited to shared deterministic fixtures | The corresponding Query module's contract changes; provider build and custody behavior is never mocked as Query logic |
| Published compatibility statement | `release/0.2.0/support-matrix.json` | The final exact DuckDB commit and Community platform rows change after complete evidence, never from a build result alone |
| Ordinary-user narrative | `README.md`, `CHANGELOG.md`, `docs/releases/0.2.0-notes.md`, and `examples/community-installation.sql` | Installation, version, support, update, history, or diagnostic guidance changes |

`oracle.py` is a thin composition root. `matrix.py` decides whether already
admitted signed-deployment and Query results name the same claimable row; it
cannot receive an unsigned build digest as the deployed artifact identity and
never builds an extension or validates a toolchain. `host_action.py` composes the
separate launcher, file-admission, state-capability, environment,
process-lifecycle, protocol, and child-action responsibilities for one bounded
action and contains no support-policy decision. `state_capability.py` alone
owns state-directory descriptors, private execution leaves, restoration, and
inode revalidation; its mirrored tests contain the filesystem-race oracles.
The child-action tests separate protocol/configuration, install, refusal, and
catalog/query responsibilities around one shared deterministic connection
fixture. `scenarios.py` owns state transitions and requires an
independent, content-identified initialization probe before a refusal can
pass; empty DuckDB catalogs alone do not prove that native initialization was
absent. `evidence.py` receives completed observations and cannot authorize
execution; `evidence_admission.py` independently re-admits the exact v2 result
before matrix use. Production code never imports these test modules.

No Query work may reinterpret connector metadata, relational plans, remote
runtime behavior, or the `0.1.0` release record. If Community evidence exposes
a change outside the adapter boundary, the lead routes it to the accountable
provider and reassesses RFC impact rather than placing a workaround here.

## Engineering Enablement input contract

Query accepts only explicit, content-identified inputs:

- immutable `candidate.json` containing the source tag, commit, tree, project
  version, latest-stable DuckDB identity, toolchain identity, and dependency
  audit identity;
- one exact anchored Community deployment v1 record per native row, binding the
  Actions archive, unsigned extension, shared pre-signature payload, served
  gzip, signed extension, exact build/deploy identities, endpoint, and
  downloaded signed artifact path;
- a canonical hosted-custody inventory and anchor that bind the supplied
  records and artifacts; and
- an explicit stock DuckDB executable or launcher plus its full content digest
  for each scenario, the incompatible artifact's exact size and digest, an
  artifact-specific independent initialization probe, and a caller-owned
  output path for the opaque Query result. Each launcher input also carries a
  canonical stock-host inventory digest; Query independently remeasures and
  privately stages the executable, `pyvenv.cfg`, DuckDB native module, Python
  package, and package metadata before a passing observation can be composed.

Query validates the complete deployment record/anchor, required shape,
identity agreement, uniqueness, containment, exact Query v2 result, and the
accepted `0.2.0` public contract. Enablement owns source admission,
toolchain setup, dependency discovery, descriptor generation, Community CI
orchestration, artifact download, signing/deployment/custody provenance, hosted
transfer, and their provider-side tests. Query must not import
`scripts/community/`, read CI workflow structure, infer paths under build
roots, or inspect `release/0.2.0/enablement/` internals. Enablement may bind the
opaque Query result into its envelope but must not interpret SQL, rows,
lifecycle assertions, diagnostics, or support-row eligibility.

## Exact oracle families

- **Contract identity:** deterministic tests require project version `0.2.0`
  while comparing the function inventory, named parameters, types, schema,
  rows, settings, and DuckDB-owned filter/order/limit behavior with the accepted
  `0.1.0` behavior.
- **Host boundary:** focused tests prove minimal environment construction,
  absence of unsigned-policy flags, one action per process, timeout and output
  bounds, unconditional process-group cleanup, retained-directory state
  isolation through an explicit child-protocol directory descriptor without
  parent-side pre-exec callbacks, JSON framing, and diagnostic path
  normalization.
- **Lifecycle:** deterministic fake-host tests prove install is distinct from
  load, repeated install cannot satisfy restart/load assertions, each process
  observes the required DuckDB and row identity, and a rejected input leaves
  neither an installed artifact nor a registered function.
- **Matrix law:** property-style cases reject duplicate rows, mixed candidate
  or DuckDB identities, failed or missing Community deployments, unsigned
  build digests presented as deployed artifact identities, missing custody,
  failed Query results, non-Community modes, and untested rows. The emitted
  matrix equals exactly the set of fully passing joined rows.
- **Live Community confirmation:** after upstream publication, the lifecycle
  oracle runs on every prospective row through the stock Community endpoint.
  Live success confirms upstream compatibility; deterministic focused tests
  remain the primary correctness oracle.
- **Representative failure:** a signed artifact presented to an incompatible
  host or platform fails before extension initialization with safe actionable
  facts and an empty function inventory. A missing/failed Community build or
  deployment is recorded as an unclaimed row, never converted into a passing
  diagnostic by the Query harness.

## Parallel workstreams and sequencing gates

1. **Freeze Query contracts.** Establish `0.2.0` version assertions, the
   preserved public contract, deterministic matrix law, lifecycle scenarios,
   and result schema before consuming live Community output.
2. **Prove one thin row.** In parallel with Enablement's candidate and
   descriptor work, finish the host/scenario unit oracles. Once one admitted
   row exists, run the complete stock-host journey before scaling the matrix.
3. **Scale without broadening claims.** Enablement may build, audit, and retain
   rows concurrently. Query runs each row independently and records failures;
   a portability change lands only with its focused adapter regression and a
   rerun of the thin row before the remaining rows.
4. **Seal the matrix.** Only after Community signing/publication, hosted
   custody, and all prospective Query results exist may Query write
   `support-matrix.json`. Failed or absent rows stay visible in evidence but
   outside the support matrix.
5. **Publish the user narrative.** Installation examples and `0.2.0` notes
   become ordinary-user guidance only after the sealed matrix passes. They
   state latest-stable-only support, all unclaimed cells, forward-only fixes,
   immutable project releases, no guaranteed Community rollback/history,
   best-effort GitHub Issues support, and that signing is not a code audit.
6. **Review and gate.** Run focused Query tests, the full claimed-row oracle,
   native product and source-identity gates, hosted custody verification,
   repository documentation checks, and independent Query, Enablement, and
   adversarial review before the release tag or publication claim.

Query and Enablement can work in parallel because their integration seam is
versioned data and process commands. Shared top-level documentation,
`extension_config.cmake`, and final release identities are serialized by the
lead. Query owns no file under `scripts/community/`,
`release/0.2.0/enablement/`, or `.github/workflows/`; Enablement owns no file
under `test/python/community_installation/` and does not edit Query's public
contract, support matrix, or user assertions.

## Code documentation and review lens

Adjacent documentation for the stock-host boundary must state input/output
ownership, exact DuckDB coupling, default security assumptions, process and
extension-directory lifetime, concurrency, timeout/cancellation, close and
kill behavior, error ownership, redaction, and why load success proves
Community signature admission rather than source safety. Matrix code documents
the identity join and the reason failed, absent, older, nightly, or
non-Community rows cannot enter the support set. Any adapter change preserves
the existing registration-to-bind-to-init-to-scan trace and updates callback
ownership or compatibility rationale beside the affected declaration.

Final Query review must establish that:

- every claimed row uses stock latest-stable DuckDB and default signature
  policy through `FROM community`;
- install, repeated install, restart, load, identity, query, and refusal are
  separate observable states rather than one process satisfying another;
- exact public behavior is preserved and safe failures prove absence of native
  initialization/registration;
- support documentation contains no implied row, backport, rollback, history,
  SLA, or source-audit promise; and
- Query depends only on the bounded Enablement records and commands, with no
  build, dependency, CI, signing, or custody internals in its modules or tests.

## Interaction exit

The Engineering Enablement facilitation remains **Open** until all of the
following are observable in the final source and evidence:

- Query can add, run, diagnose, and remove a candidate Community row using only
  the documented candidate, row, custody, host-command, and output interfaces;
- Query independently maintains the lifecycle oracle, matrix law, exact
  support matrix, and user guidance, while Enablement can change its internal
  build/signing/deployment/custody implementation without a Query edit;
- every claimed row passes the complete stock-host oracle and the hosted
  custody round trip binds the same opaque Query result without interpreting
  it;
- final module imports, test fixtures, workflow calls, and release evidence
  preserve the consumer-provider dependency direction; and
- the independent topology-exit audit finds no routine need for Enablement to
  inspect Query SQL, catalog assertions, diagnostics, or support decisions.

If any row or workflow still requires bespoke cross-boundary debugging, keep
the interaction Open and keep that row unclaimed. Passing one end-to-end test
or creating a named evidence file is not sufficient exit evidence.
