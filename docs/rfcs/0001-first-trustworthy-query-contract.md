# RFC 0001: Establish the first trustworthy query contract

```yaml
rfc: "0001"
title: "Establish the first trustworthy query contract"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Query Experience"
technical_decision_owner: "Lead agent"
product_approver: "Product manager"
authors:
  - "Lead agent"
required_reviewers:
  - "Query Experience perspective"
  - "Connector Experience perspective"
  - "Remote Runtime perspective"
  - "Relational Semantics perspective"
  - "Engineering Enablement perspective"
affected_teams:
  - "Query Experience"
  - "Connector Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
linked_outcome_or_objective: "ROADMAP.md 0.1.0 — first trustworthy query"
supersedes: "none"
```

## Summary

For `0.1.0`, adopt a source-built native C++ DuckDB table-function extension
named `duckdb_api` that exposes one preview scan function over one embedded,
static, fixture-backed REST relation. The implementation must preserve the
protocol-neutral `CompiledConnector → ScanRequest → ScanPlan → BatchStream`
boundaries, use conservative planning, produce bounded typed chunks, and prove
offline bind, cancellation, cleanup, strict conversion, and redacted failure
behavior. This decision does not establish public connector authoring, live
network access, binary distribution, or the intended portable `1.0.0`
integration profile.

## Sponsorship and context

- **RFC type:** Product.
- **Sponsoring team:** Query Experience.
- **Linked outcome or objective:** `0.1.0 — first trustworthy query` from
  `ROADMAP.md`.
- **Why now:** The native boundary trial proves that the project can build,
  load, scan in bounded chunks, observe cancellation, and release state on one
  target. Delivery now needs an explicit user-visible SQL boundary and shared
  contract before implementation can become a product surface.

The customer is a DuckDB user evaluating whether an API-backed relation can
behave like trustworthy relational input. Connector authors are affected by
the internal example package, but a general authoring workflow remains the
`0.3.0` outcome.

## Problem

The repository currently contains design proposals and a successful non-public
native boundary trial, but no accepted first-query contract. The architecture
illustrates Rust C Extension API table functions, generated per-relation names,
an optional catalog shell, and a Rust core without deciding which combination
is the first supported executable profile. The connector and runtime documents
describe far more capability than `0.1.0` can responsibly promise.

A user therefore cannot tell which artifact to load, which SQL to run, which
relation exists, what compatibility cell is supported, or which failure and
lifecycle behavior is guaranteed. Implementing any one of those choices
without this RFC would create public and cross-team contracts implicitly.

Observed facts:

- The pinned native extension trial builds and directly loads on DuckDB 1.5.4
  for macOS arm64.
- It produces deterministic typed multi-chunk output, checks exact artifact
  identity, propagates an interrupt through a table-function callback, and
  releases global state after success, failure, and cancellation.
- The official Rust template inspected by the trial still requires DuckDB's
  unstable C Extension API and exact-version coupling.
- The native C++ profile is also DuckDB-version-coupled and the local macOS
  debug build lacks sanitizer evidence.

Assumptions requiring product approval are the preview extension name, public
SQL function, compatibility claim, and explicit product exclusions below.

## Decision drivers and invariants

- **Must preserve:** Ordinary bind and planning perform no network I/O;
  immutable plans; strict typed conversion; bounded pull execution;
  cancellation and teardown; redacted failures; conservative behavior for
  unavailable DuckDB metadata; and exactly one owner for every relational
  operation.
- **Must enable:** A user can build, locally load, identify, query, cancel, and
  diagnose one static REST-shaped relation through ordinary DuckDB SQL.
- **Must not introduce:** A stable connector specification, live-service or
  credential promise, public native ABI, deep catalog dependency, hidden SQL
  reconstruction, unbounded buffering, published binary-distribution promise,
  or claim that the native preview proves the intended `1.0.0` portable
  profile.

## Proposed decision

### Public behavior

The preview extension identity is `duckdb_api` and its first product version is
`0.1.0`. The supported installation mode is a clean source build followed by
direct unsigned loading of the resulting local artifact. DuckDB must receive
that permission at database startup; the CLI narrative is:

```sh
duckdb -unsigned
```

```sql
LOAD '/absolute/path/to/duckdb_api.duckdb_extension';
```

The only project-defined query surface is:

```sql
SELECT id, name, active
FROM duckdb_api_scan(
    connector := 'example',
    relation := 'items'
)
ORDER BY id;
```

`connector` and `relation` are required constant `VARCHAR` bind arguments. The
accepted `0.1.0` artifact recognizes only the `example` connector and its
`items` relation. Their static schema is:

| Column | DuckDB type | Nullable |
| --- | --- | --- |
| `id` | `BIGINT` | no |
| `name` | `VARCHAR` | no |
| `active` | `BOOLEAN` | no |

The success fixture and its row order are immutable acceptance inputs. The
query above returns exactly:

| `id` | `name` | `active` |
| ---: | --- | --- |
| 1 | `alpha` | `true` |
| 2 | `beta` | `false` |
| 3 | `gamma` | `true` |

Users inspect identity through DuckDB's `duckdb_extensions()` output. No
project-specific version function is added.

Unknown connector or relation names fail during bind. Malformed JSON and
well-formed data that violates the static schema remain distinct execution
stages with stable categories, connector, relation, and safe field context:

```text
[duckdb_api][decode] connector=example relation=items: response is not valid JSON
[duckdb_api][schema] connector=example relation=items: field id cannot be converted to BIGINT
```

The `decode` category owns malformed source representation. The `schema`
category owns extraction, nullability, and declared-type conversion failures.
Diagnostics never include the rejected scalar, response body, credentials, or
an unrestricted local path.

The `0.1.0` public inventory is limited to the extension identity, version,
`duckdb_api_scan` signature, the two accepted identifiers, the relation schema,
and the observable diagnostic and cancellation behavior above. Under the
project's documented pre-`1.0.0` SemVer policy, an incompatible change to that
inventory requires a new `0.Y.0` release with migration guidance.

Explicit `0.1.0` exclusions are:

- connector installation, registration, reload, or arbitrary package loading;
- live HTTP, authentication, secrets, user-supplied URLs, redirects, proxies,
  pagination, retries, caching, providers, partitions, or GraphQL;
- predicate, ordering, limit, or offset pushdown and custom explain output;
- `ATTACH`, generated schemas, generated relation functions, or catalog and
  optimizer integration;
- public binary distribution, signing, automatic installation, or update
  support; and
- compatibility beyond the declared cell or any stable public C++, Rust, C,
  FFI, plugin, or connector-package ABI.

### Shared interfaces

The implementation is native C++ for this vertical slice and introduces no
Rust runtime or cross-language FFI. Its internal types are not public ABI, but
the following semantic team interfaces are mandatory:

1. An immutable `CompiledConnector` snapshot identifies `example`, version
   `0.1.0`, the `items` static schema, one fallback many-row REST operation,
   strict extractors, and a content-addressed fixture reference.
2. The DuckDB adapter creates a protocol-neutral `ScanRequest`. Projection is
   represented when the native adapter exposes it; filters, ordering, limit,
   and offset use their conservative unavailable values. The request contains
   no SQL text and cannot initiate I/O.
3. The relational planner returns an immutable `ScanPlan` selecting the one
   operation, assigning all query filtering, ordering, and limiting to DuckDB,
   listing the output columns, and recording the fixture executor plus applied
   budgets. Planning is deterministic and side-effect free.
4. A fixture-backed REST protocol executor validates the planned method, path,
   and response extractor, then yields lossless JSON source batches from the
   recorded response without network access.
5. A synchronous pull-oriented `BatchStream` converts source records strictly
   into bounded DuckDB chunks and exposes cancel and close behavior. The
   DuckDB adapter consumes only this interface rather than fixture or protocol
   internals.

The example source metadata may use the repository's `duckdb_api/draft`
connector shape as an internal acceptance fixture, but `0.1.0` exposes no
general parser or authoring compatibility claim. The compiled snapshot and
oracles, not acceptance of arbitrary YAML, are the interface for this release.

### Operational behavior

- The extension uses DuckDB's native C++ extension interface and is coupled to
  the declared DuckDB build identity.
- Extension load creates immutable example metadata. Bind resolves only that
  metadata and constant arguments; a network sentinel must prove no bind or
  planning I/O.
- The scan uses one DuckDB source task. Output chunks never exceed DuckDB's
  standard vector size; fixture bytes, decoded records, output rows, and wall
  time have explicit hard ceilings enforced below the connector declaration.
- Execution is synchronous for the fixture profile. No worker runtime,
  prefetch thread, or output queue is introduced merely to imitate the future
  remote runtime.
- Cancellation is checked during fixture reading, decoding, conversion, and
  between output chunks. Scan destruction invokes the same idempotent close
  path after success, failure, or cancellation.
- The runtime reports structured stage categories internally and converts them
  once at the DuckDB boundary. Unknown exceptions become a redacted internal
  error; no exception escapes outside DuckDB's native extension error model.
- No network, secret, retry, cache, or replay capability exists in this
  profile. Those paths fail closed rather than silently becoming no-ops.

The acceptance harness may dependency-inject a private `FixtureScenario` when
constructing the statically linked test extension. The scenarios provide the
success response, malformed JSON, well-formed type-incompatible JSON, and a
controllable blocking point inside fixture execution. After source bytes and
the blocking point are supplied, every scenario traverses the production
`CompiledConnector → ScanRequest → ScanPlan → BatchStream → DuckDB` path.

`FixtureScenario` is a C++ test dependency, not a SQL function or runtime
setting. It is unavailable through SQL arguments, environment variables,
database configuration, connector metadata, or filesystem paths, and is not
compiled or exported in the loadable release artifact. A release-artifact
inventory test must prove that the accepted function signature and identifiers
are the only project-defined public surface. This seam supplies failure and
lifecycle evidence without adding a product fault-injection mechanism.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsor; accountable for the DuckDB user outcome | Owns native load, bind, scan, SQL, diagnostics, version, and lifecycle evidence | Collaboration | The accepted SQL succeeds and its bind, failure, cancellation, and compatibility oracles pass |
| Connector Experience | Provider of immutable example metadata | Ensures the internal fixture maps cleanly to `CompiledConnector` without implying public authoring support | Collaboration | The example snapshot is deterministic, source-explainable, and consumed without authoring/runtime leakage |
| Remote Runtime | Provider of fixture execution and `BatchStream` behavior | Supplies a bounded, cancelable, low-friction execution interface without live transport capability | Collaboration | Query Experience consumes the stream without fixture or protocol internals and lifecycle tests pass |
| Relational Semantics | Provider of conservative planning | Owns the minimal `ScanRequest → ScanPlan` oracle and prevents accidental pushdown claims | Collaboration | Deterministic plans contain conservative defaults and DuckDB owns all unsupported relational work |
| Engineering Enablement | Build and evidence facilitator | Transfers clean native build, compatibility, sanitizer, and fixture-gate practice | Facilitation | Query Experience can maintain the declared build and acceptance gates without Enablement approval |

No team accountability or durable interface moves. The temporary collaboration
establishes the first executable seams; later low-friction service interaction
requires independent evidence rather than an assumption in this RFC.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** The scan supplies typed
  base rows only. DuckDB owns filters, ordering, limits, and offsets. The
  adapter passes no unavailable query structure and the plan claims no remote
  optimization.
- **Authentication, credentials, network policy, and privacy:** No credential
  or network capability exists. Fixture provenance excludes personal data and
  secrets. Any attempt to select a live transport fails closed.
- **Resource budgets, backpressure, and cancellation:** Input and decoded data
  are bounded by host constants, output is pulled one bounded chunk at a time,
  and cancellation plus idempotent close are verified at each lifecycle exit.
- **Replay units, retries, caching, and duplicate prevention:** Not enabled.
  The single immutable fixture is read once per scan and rows are emitted once
  in recorded order.
- **Concurrency, immutability, and state ownership:** Connector and plan state
  are immutable. Mutable scan state is owned by one DuckDB source task and is
  not shared across scans.
- **FFI, initialization, reload, shutdown, and failure containment:** There is
  no cross-language FFI or reload. Native extension initialization and scan
  teardown use RAII; repeated scans and connection shutdown must release all
  state. The exact native DuckDB coupling is part of the compatibility cell.
- **Diagnostics, redaction, metrics, and progress reporting:** Malformed JSON
  is `decode`; extraction, nullability, and type conversion are `schema`.
  Connector, relation, and safe field name are observable. Raw values and
  bodies are not. Public progress and custom explain are excluded.

## Compatibility and migration

The sole supported product cell is the exact verified native build cell:

| DuckDB | DuckDB platform | Verified host | Compiler and language mode | Build tools | Installation mode |
| --- | --- | --- | --- | --- | --- |
| 1.5.4 (`08e34c447b`) | `osx_arm64` | macOS 26.5.1 on Apple Silicon arm64 | Apple clang 17.0.0, C++11 | CMake 4.1.2, Ninja 1.13.0 | Clean source build and unsigned direct local load |

`osx_arm64` is the artifact platform identifier, not a promise for every macOS
or Apple Silicon version. Expanding any host, compiler, build-tool, DuckDB,
architecture, or loading dimension requires its own green compatibility cell.

The authoritative `0.1.0` release gate is distinct from the boundary-trial
runner. It starts only from a clean worktree at `v0.1.0`, verifies that the tag
and `HEAD` are identical and that every version source says `0.1.0`, fetches
only pinned dependencies into a new build root, and rejects reused build state.
It emits a machine-readable manifest containing:

- project version, source commit, and tag;
- DuckDB, native-template, and build-tool source identities;
- host OS, DuckDB platform, architecture, compiler path and version, C++ mode,
  CMake, and Ninja versions;
- build profile and sanitizer flags;
- extension name, version, load mode, and public SQL inventory;
- connector snapshot and fixture content digests; and
- artifact path, size, and SHA-256 checksum.

The manifest and artifact are verified together after loading into a pristine
DuckDB 1.5.4 host. The selected release artifact and checksum are immutable.
`0.1.0` does not claim byte-identical local artifacts across different
workspace paths; repeatability means two cache-empty builds produce the same
source, dependency, toolchain, extension, connector, fixture, SQL-surface, and
behavior identities. Byte-reproducible distributed artifacts remain part of
the `0.2.0` installation outcome.

A separate named engineering evidence cell, `linux_amd64-sanitized`, builds the
same production sources with Clang C++11, AddressSanitizer, and
UndefinedBehaviorSanitizer in a container image pinned by digest. Its exact OS
image, compiler, DuckDB source, flags, and artifact identity appear in the
anchored inner manifest and outer Docker-launcher envelope. The retained compile
database permits the sanitizer-flags oracle to be re-run from custody. This is a
safety-evidence cell, not a supported product cell.

Other DuckDB versions, operating systems, architectures, compilers, loading
modes, and reused build trees are unsupported even if they happen to work. A
negative compatibility test loads the artifact into a pristine host built from
a deliberately mismatched DuckDB source identity and requires rejection before
extension initialization.

No user or package migration exists before `0.1.0`. Rollback means loading an
artifact reproduced from the preceding immutable source commit; it does not
permit relabeling or replacing a `0.1.0` artifact. The native profile is a
preview compatibility choice, not proof or abandonment of the roadmap's
intended portable stable-C-API boundary for `1.0.0`.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Can the native extension boundary build and load? | Clean pinned build and exact identity | `scripts/run-native-extension-trial.sh` | Passed for DuckDB 1.5.4 on macOS arm64; unsigned local load only |
| Can a native table function emit bounded typed chunks? | Exact multi-chunk row oracle | `experiments/native-extension/test/sql/fdw_boundary_probe.test` | Passed 29 SQLLogic assertions; experimental SQL is not the product contract |
| Can cancellation occur inside a callback and release state? | Synchronized interruption and independent lifecycle observation | `experiments/native-extension/test/cancellation.py` | Passed with one observed interrupt, no active waiter, and matched open/close counters |
| Is local sanitizer evidence clean? | Sanitized native build | Official debug build on the recorded macOS host | Not established: upstream platform helper recurses in ASan initialization before extension code; sanitizer coverage is required on a working CI target during delivery |
| Does the proposed product pipeline preserve the contract? | End-to-end success, offline-bind, distinct decode/schema failures, cancellation, teardown, and public-inventory oracles | Repository-owned scenarios injected only into the statically linked test host | Implemented in the product source and private static host; pre-tag debug and release-profile product cells pass, while tagged release evidence remains outstanding |
| Can the release evidence reject stale or misidentified artifacts? | Clean-room gate plus deliberate failure canaries | Two cache-empty workspaces, `linux_amd64-sanitized`, pristine supported and mismatched DuckDB hosts, and manifest tampering | Gate and canaries implemented; native sanitizer execution, clean tag, two-workspace run, and durable custody remain required before release |

The complete trial record is in
`experiments/native-extension/RESULTS.md`. Acceptance of this RFC authorizes the
direction; it does not treat the boundary probe as `0.1.0` implementation.

## Alternatives considered

### Use the Rust C Extension API profile immediately

This aligns with the architecture's intended portable direction and avoids a
native implementation. The inspected official template still uses the
unstable C Extension API and exact DuckDB coupling, so it does not currently
deliver the intended portability advantage. Selecting it would add wrapper and
toolchain uncertainty before proving the first user query.

### Combine a native C++ shell with a Rust core now

This preserves the proposed Rust runtime while using the proven native load
boundary. It introduces a cross-language ABI, panic containment, ownership,
batch-writing, cancellation, and shutdown surface before the first REST-shaped
fixture. That risk is not required for `0.1.0`; a later RFC may introduce a
coarse FFI after both sides have executable contracts.

### Start with deep catalog and optimizer integration

`ATTACH`, generated tables, and optimizer hooks can provide more natural SQL
and pushdown metadata. They increase DuckDB version coupling and semantic scope
without improving the first fixture-backed proof. The selected table function
keeps the native surface narrow.

### Generate a relation-specific function

`example_items()` is shorter for one relation and matches architecture
examples. It commits early to generated-name collision, registration, reload,
and package naming behavior. The explicit dispatcher keeps `0.1.0` to one
auditable function and makes unsupported identifiers fail clearly.

### Keep the boundary probe as the product demonstration

This is the smallest implementation, but it emits synthetic rows without a
compiled relation, protocol plan, lossless JSON decoding, or structured
failure path. It proves mechanics rather than the roadmap's user value.

## Drawbacks and failure modes

The native profile couples `0.1.0` to one DuckDB build and may require later
replacement. Query Experience owns making that limitation obvious; Engineering
Enablement owns making stale-build and identity mistakes difficult. Native-only
delivery may create migration work if the portable stable C API becomes viable.

The dispatcher SQL is less natural than a generated relation function and the
embedded example is not a useful live data source. Those costs are deliberate:
the release demonstrates trustworthy mechanics without pretending that
authoring, installation, or live service policy is ready.

An internal fixture path can accidentally become a public package loader, or
fixture execution can bypass the planned protocol and stream seams. Connector
Experience and Remote Runtime own rejecting those shortcuts through snapshot
and interface oracles. Relational Semantics owns rejecting hidden pushdown or
premature limit behavior. Query Experience owns conversion of every failure at
the DuckDB boundary and must not expose internal values.

The macOS sanitizer gap remains a safety limitation. Delivery cannot claim
sanitizer evidence until a supported engineering target runs the extension
code under a working sanitizer runtime.

## Acceptance and verification

- **End-to-end demonstration:** From a clean checkout on the supported cell, a
  user builds and directly loads `duckdb_api` 0.1.0, runs the accepted SQL,
  receives the three exact typed rows, and observes the exact version. The
  acceptance harness runs the unchanged SQL through the same extension
  pipeline with private malformed, type-incompatible, and blocking scenarios
  to prove redacted failures, synchronized cancellation, and teardown.
- **Automated oracle:** Golden `CompiledConnector`, `ScanRequest`, and
  `ScanPlan` snapshots; generated REST-plan and fixture-sequence checks;
  multi-chunk typed output; a bind network sentinel; distinct malformed-JSON
  and strict-conversion redaction tests; synchronized cancellation; repeated
  scan, failure, and shutdown lifecycle counters; and a loadable-artifact
  inventory proving that test controls and extra public identifiers are absent.
- **Quality gates:** A clean pinned native build with a fresh build tree,
  DuckDB SQLLogicTests with the extension required, native formatting and
  warnings, repository validation, exact dependency and wheel hashes where
  applicable, the pinned `linux_amd64-sanitized` production-path run, artifact
  checksum and manifest verification, and `git diff --check` plus
  `git diff --cached --check`. Negative canaries must reject dirty source,
  source/tag/version mismatch, stale build state, disabled sanitizer flags in
  the sanitizer cell, a mismatched DuckDB host, fixture mutation, manifest or
  checksum tampering, and any test-control symbol, SQL function, configuration,
  environment, metadata, or path exposed by the loadable artifact.
- **Independent review:** Query boundary, connector snapshot, relational
  correctness, runtime resource and cancellation, native ABI/lifecycle,
  failure-oracle, and reproducibility perspectives.
- **Interaction exit:** Every topology row's observable exit condition passes
  and Query Experience independently runs the documented gates from two
  cache-empty workspaces, obtains matching semantic identity and behavior
  manifests, observes every deliberate canary fail, and owns the runbook and
  ongoing maintenance without permanent facilitation.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Add the native C++ preview profile, accepted SQL, conservative capability cell, and distinction from the intended portable profile | Propagated in revision 0.4 |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Clarification required | Identify the repository example as internal `0.1.0` evidence, not general package compatibility or public loading support | Propagated in the repository preview evidence boundary |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Add the native preview mapping for the minimal `CompiledConnector → ScanRequest → ScanPlan → BatchStream` slice without declaring a public ABI | Propagated in the accepted native mapping and lifecycle/error contracts |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | No accountability or team-interface movement | Team reviews recorded below |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing goal, RFC, contract-change, delivery, and review rules apply | No update required unless delivery exposes process drift |
| Examples, diagnostics, fixtures, and tests | Affected | Add the accepted SQL, example success and malformed fixtures, public-surface inventory, lifecycle oracles, and clean-build compatibility evidence | Implemented; authoritative tagged and sanitizer evidence remains a release gate |

The RFC records rationale; propagated contracts and executable behavior become
the sources of current truth.

## Unresolved questions

- Which native JSON and fixture-storage implementation best satisfies the
  accepted lossless and bounded contracts is delegated to delivery.
- Whether `0.2.0` retains the dispatcher or introduces registration and
  generated relation functions depends on installation and authoring evidence.
- The portable stable-C-API migration path remains a later compatibility
  decision; `0.1.0` exposes no implementation ABI that constrains it.

These questions do not alter the proposed `0.1.0` public inventory or
acceptance behavior.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Query Experience perspective | Query Experience | Approved | Focused re-review confirmed that private fixture scenarios exercise unchanged SQL through the production pipeline and are excluded by the release public-inventory gate | Accepted; initial test-seam objection resolved |
| Connector Experience perspective | Connector Experience | Approved | Internal fixture metadata, immutable snapshot, and golden oracles preserve the `0.3.0` public authoring boundary | Accepted; no objection |
| Remote Runtime perspective | Remote Runtime | Approved | Focused re-review confirmed separate malformed-source `decode` and extraction/nullability/conversion `schema` stages with redacted production-pipeline fixtures | Accepted; initial taxonomy objection resolved |
| Relational Semantics perspective | Relational Semantics | Approved | One fallback operation, conservative unavailable metadata, DuckDB residual ownership, and golden plans preserve every relational invariant | Accepted; delivery oracle must assert every conservative field and projection closure |
| Engineering Enablement perspective | Engineering Enablement | Approved | Focused re-review confirmed that the distinct clean-tag gate, exact product and named sanitizer cells, manifest, pristine-host checks, tamper canaries, and two-workspace handoff close the identified bypasses | Accepted; initial release-evidence objection resolved |

All required team perspectives approved the proposal. On 2026-07-17, the
product manager approved the reserved public choices and authorized delivery.

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Approved by the product manager on 2026-07-17 for the
  `duckdb_api` extension identity, `duckdb_api_scan(...)` SQL surface, exact
  compatibility cell, source-built unsigned local loading, and explicit
  exclusions.
- **Rationale:** Accepted. The decision converts the proven native mechanism
  into the narrowest honest user outcome, preserves conservative semantic and
  lifecycle boundaries, and defines release evidence that cannot confuse the
  experiment with a product artifact.
- **Material objections:** Query Experience identified that the fixed public
  success scan could not itself prove malformed failure or synchronized
  cancellation. The proposal now injects private test scenarios at extension
  construction, exercises the unchanged SQL through the production pipeline,
  and requires proof that the seam is absent from the release artifact. Remote
  Runtime identified a stage-ownership error: valid JSON with an incompatible
  declared type is `schema`, not `decode`. The proposal now preserves separate
  deterministic and redacted oracles for both categories. Engineering
  Enablement identified that the bounded trial runner could not substantiate a
  release gate. The proposal now defines a separate clean-tag gate, exact
  product and sanitizer evidence cells, a machine-readable manifest,
  supported and mismatched pristine hosts, deliberate tamper and bypass
  canaries, and a two-workspace self-service handoff. Focused reviews from
  Query Experience, Remote Runtime, and Engineering Enablement approved the
  resulting dispositions; all required team perspectives now approve.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Deliver `0.1.0 — first trustworthy query` | Query Experience | Connector Experience, Remote Runtime, and Relational Semantics through bounded Collaboration; Engineering Enablement through Facilitation, with the exit conditions above | RFC accepted, product approval recorded, and required contract propagation included in delivery |
