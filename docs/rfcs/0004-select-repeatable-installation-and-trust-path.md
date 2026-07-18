# RFC 0004: Select the repeatable installation and trust path

```yaml
rfc: "0004"
title: "Select the repeatable installation and trust path"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Query Experience"
technical_decision_owner: "Lead agent"
product_approver: "Product manager"
authors:
  - "Lead agent"
required_reviewers:
  - "Query Experience perspective"
  - "Engineering Enablement perspective"
affected_teams:
  - "Query Experience"
  - "Engineering Enablement"
linked_outcome_or_objective: "ROADMAP.md 0.2.0 — repeatable installation"
supersedes: "none"
```

## Summary

Target DuckDB Community Extensions as the ordinary-user distribution and
trust path for `duckdb_api`. Retain source build and a checksum-verified,
explicitly unsigned local artifact only as development and controlled-preview
paths. The product manager selected the MIT License and aligned with Community
Extensions as the ordinary-user channel. Support only the latest stable DuckDB
release and the exact Community CI platform cells that pass the `0.2.0`
delivery oracle. Pre-`1.0` fixes move forward without a backport commitment;
project releases are immutable; Community rollback and historical availability
are not guaranteed; and project support is best-effort through GitHub Issues.
Ordinary-user guidance remains unpublished until the external delivery gates
prove those claims.

## Sponsorship and context

- **RFC type:** Product.
- **Sponsoring team:** Query Experience.
- **Linked outcome or objective:** `0.2.0 — repeatable installation` from
  `ROADMAP.md`.
- **Why now:** `0.1.0` proved a source-built query. The next outcome requires a
  new user to cross an installation boundary without mistaking a self-built
  native artifact or a checksum for an ordinary production trust model.

The affected customer is a DuckDB user who wants to install, restart, load by
name, identify, and query `duckdb_api` without repository-internal knowledge.
Engineering Enablement facilitates reproducible artifact production and
custody; Query Experience remains accountable for the user-visible result.

## Problem

RFC 0001 intentionally supports only source build and direct unsigned local
load on one exact DuckDB and platform cell. That path demonstrates the product
but requires a process-wide startup setting that permits any unsigned DuckDB
extension. A checksum can prove that downloaded bytes match a named evidence
bundle; it cannot make those bytes trusted by stock DuckDB.

A concrete user can currently build and directly load `duckdb_api`, but cannot
use the normal signed-extension experience. Publishing the same unsigned bytes
at a URL or custom repository would improve acquisition while preserving the
same trust reduction. DuckDB's documented third-party route to a default-
trusted artifact is its Community Extensions program, which builds, signs, and
distributes public open-source extensions. This repository is now licensed
under MIT, clearing the project-license prerequisite. The dependency audit and
external submission evidence remain delivery work.

Observed facts:

- Stock DuckDB rejects the `v0.1.0` artifact under its default signature
  policy. Enabling unsigned extensions is a startup-time, process-wide trust
  reduction, not permission scoped to `duckdb_api`.
- The bounded trial installs the verified artifact in empty state, restarts the
  host, loads by name, and preserves the exact `0.1.0` query. It also exercises
  version, platform, corruption, signature, and repeated-install behavior.
- DuckDB Community Extensions is the documented third-party path whose
  artifacts are signed by DuckDB's community build and distribution pipeline.
  Community code is not security-vetted merely because its binary is signed.
- Community submission requires public open-source source and a declared
  license. The product manager selected MIT, recorded in the root `LICENSE`.

The product manager approved the compatibility, update, immutability,
rollback/history, and support boundaries recorded by this decision. The exact
Community platform rows remain evidence-derived: delivery may claim only rows
that pass the release oracle.

## Decision drivers and invariants

- **Must preserve:** The exact `0.1.0` SQL, rows, diagnostics, lifecycle and
  security invariants, extension identity, and currently declared DuckDB
  compatibility cell.
- **Must enable:** A clean install, process restart, load by extension name,
  version identification, exact query, and actionable incompatible-input
  refusal with reproducible artifact provenance.
- **Must not introduce:** A claim that a co-located checksum authenticates an
  untrusted publisher; a normal-user instruction that weakens signature policy
  process-wide; license metadata inconsistent with the root MIT License; a
  mutable release artifact; or an implicit compatibility, update, backport, or
  support promise.

## Proposed decision

Use DuckDB Community Extensions as the normal install channel. Community
source descriptors will pin an immutable source ref, extension SemVer, build
language, project license, and maintainers. The DuckDB-managed pipeline will
build, sign, and distribute the compatible artifact. Normal user guidance will
use DuckDB's community repository syntax only after that path passes its
external build matrix and clean-host oracle.

Keep two explicitly non-production paths:

1. Source build remains the contributor and deep-diagnostic path.
2. A manifest- and checksum-verified unsigned artifact may support controlled
   previews and installation trials. It requires an isolated host that was
   deliberately started with unsigned extensions enabled. It is not the
   default user path and carries no broader compatibility promise.

Artifact evidence for every path binds project version, source commit and tree,
DuckDB version and commit, DuckDB platform, artifact size and checksum, public
contract identity, and the build/custody method. Project versions, source refs,
and Git tags are immutable; a correction uses a new SemVer. DuckDB governs the
Community endpoint, cache retention, and served artifacts, so this project does
not promise their immutability or historical availability. An unsupported
DuckDB version or platform must fail before extension initialization, and an
integrity mismatch must fail before DuckDB is invoked.

### Public behavior

The eventual ordinary-user experience is intended to be:

```sql
INSTALL duckdb_api FROM community;
LOAD duckdb_api;
```

This accepted decision does not by itself publish that instruction as currently
supported. The existing source-built unsigned path and exact `0.1.0`
compatibility cell remain the only delivered behavior until the `0.2.0`
delivery gates pass. The SQL query and query diagnostics do not otherwise
change; the delivered extension version advances to `0.2.0`.

### Shared interfaces

No connector, planning, runtime, or native scan interface changes. Query
Experience consumes an Engineering Enablement evidence service consisting of
explicit host, artifact, manifest, anchor, verifier, and negative-fixture
inputs. The installation oracle must not import build or CI internals.

### Operational behavior

Signature verification remains enabled by default. A controlled unsigned trial
must use isolated host state and must verify artifact integrity before starting
DuckDB. Community signing establishes provenance from DuckDB's build pipeline,
not source-code safety; users and operators retain responsibility for deciding
whether to load native extension code.

Installation is restart-tested because DuckDB does not unload or reload an
extension within a running process. Repeated installation must not silently
replace verified bytes inside one oracle run. Before `1.0`, fixes ship only in
new forward project releases; there is no backport commitment. `0.2.0` makes no
guarantee that Community can select or retain historical versions or provide a
rollback. Previously cached bytes may remain locally. Project support is
best-effort through GitHub Issues with no service-level commitment.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsor and accountable stream | Own the install, restart, load, identify, query, and diagnostic narrative | Collaboration during decision; then X-as-a-Service consumer | Query independently maintains the user oracle through the declared evidence inputs |
| Engineering Enablement | Artifact and custody facilitator | Provide reproducible artifacts, provenance verification, clean-host inputs, retained CI evidence, and community-build support | Facilitation | Query runs and diagnoses the oracle without build, CI, or custody internals |

The proposal removes build-system cognitive load from Query Experience without
moving ownership of the user journey. No topology or charter update is needed
if the interaction exits as specified.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** Not affected; the exact
  accepted query and public-contract identity are installation oracles.
- **Authentication, network policy, and privacy:** Product execution is not
  affected. Acquisition uses DuckDB's repository mechanism; a later delivery
  plan must record allowed publication endpoints and custody without granting
  connector runtime network authority.
- **Resources, backpressure, and cancellation:** Query execution is unchanged
  and remains covered by `0.1.0` evidence.
- **Replay, retries, caching, and duplicates:** Runtime data behavior is not
  affected. DuckDB installation caching and forced updates must be documented
  separately from scan replay.
- **Concurrency, immutability, and state ownership:** Project versions, source
  refs, and tags are immutable. A new project release produces a new identity;
  it does not relabel an existing release. Community endpoint and cache state
  remain externally governed.
- **FFI, initialization, reload, and shutdown:** Native code must not run on a
  rejected artifact. A separate process proves installed load because an
  extension cannot be unloaded or reloaded safely in-process.
- **Diagnostics and redaction:** Rejections may include safe version,
  platform, artifact name, and expected/observed checksum context. They must
  not expose credentials, unrestricted environment contents, or sensitive
  local paths in product diagnostics.

## Compatibility and migration

Until `0.2.0` passes its delivery gates, the current supported path remains the
source-built direct unsigned load on DuckDB 1.5.4 `osx_arm64`.

At release time, `0.2.0` supports exactly one DuckDB release: the then-current
latest stable version. Its release evidence must enumerate the exact DuckDB
commit and every Community CI platform cell whose build and complete Query
oracle pass. Failed, excluded, untested, older, nightly, non-Community, or
otherwise absent rows are unclaimed even if installation happens to work.

Before `1.0`, fixes move forward in a new immutable project version and do not
create a backport obligation. The project never moves or reuses a Git tag or
source ref. A Community update can change what is served for a DuckDB/platform
location, and cached bytes can remain on user machines; `0.2.0` therefore makes
no guarantee of rollback, historical-version selection, or continued upstream
availability. Release notes must state these boundaries at the installation
instructions.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Verified unsigned artifact can cross install and restart | Empty-state install, repeated install, separate-process load by name, version and exact query | `experiments/repeatable-installation/install_oracle.py` | Passed: installed SHA-256 remained `4f1a0678…`; restart/load returned the exact `0.1.0` contract. Controlled unsigned policy only |
| Default trust does not accept the self-built artifact | Signature-policy refusal and empty function inventory | Fresh default-policy host | Passed: DuckDB 1.5.4 refused the invalid signature before registration or installed bytes |
| Incompatible host and platform fail before registration | DuckDB 1.5.3 and documented wrong-platform footer fixtures | Fresh host and extension directories | Passed: diagnostics named both DuckDB versions and both platforms; one exact negative cell each |
| Corrupted download is stopped before DuckDB | Manifest anchor, size, and SHA-256 mismatch | Body-corrupted copy; host invocation sentinel | Passed: the verifier rejected SHA-256 `363a9183…` with zero Query-host invocations; proves bundle integrity, not publisher authenticity |
| Release artifact is byte-reproducible | Two independent clean workspaces produce the trusted extension bytes | Independently anchored reproduction evidence and `verify_reproduced_artifacts.py` | Passed: both 4,859,678-byte artifacts equal trusted SHA-256 `4f1a0678…`; one recorded source and product cell only |
| CI preserves the intended evidence bundle | Downloaded workflow artifact contains manifest and artifact, not only a log | Visible allowlisted custody stage, pinned upload/download actions, inner-evidence verifier, and retention guard | Decision evidence passed locally. A GitHub-hosted upload/download round trip remains a `0.2.0` delivery gate |
| Community path is accepted by stock DuckDB | DuckDB-managed build, signing, publication, clean community install | Community Extensions submission and matrix | Decision evidence is sufficient; descriptor acceptance, matrix build, signing, publication, and the complete Query oracle remain `0.2.0` delivery gates that determine the claimed platform rows |

Primary policy references are DuckDB's [extension security
guidance](https://duckdb.org/docs/current/operations_manual/securing_duckdb/securing_extensions),
[extension distribution](https://duckdb.org/docs/current/extensions/extension_distribution),
[advanced installation methods](https://duckdb.org/docs/current/extensions/advanced_installation_methods),
and [Community Extensions development
process](https://duckdb.org/community_extensions/development).

## Alternatives considered

### Continue with source build only

This preserves maximum transparency and the current contract with no external
publication dependency. It keeps compiler, template, pin, artifact-path, and
unsigned-loading burden on every evaluator, so it does not deliver the intended
ordinary installation outcome. It remains the contributor path.

### Publish a downloadable unsigned artifact or custom repository

This is quick, supports exact artifact URLs, and can preserve historical builds
outside DuckDB's community layout. A checksum and manifest give useful
integrity and provenance evidence. Stock DuckDB still rejects the artifact
unless the whole process admits unsigned extensions; a self-signed artifact
cannot add a custom trusted key to stock DuckDB 1.5.4. This remains suitable
only for a controlled preview unless a separately accepted trust design
emerges.

### Distribute a custom DuckDB host with the extension statically linked

Static linkage avoids runtime extension signature checks and gives complete
control over the host. It turns this project into a DuckDB runtime distributor,
greatly expanding compatibility, security-update, packaging, and support
responsibilities. That burden is disproportionate to the outcome.

### Use DuckDB Community Extensions

This gives the normal stock-DuckDB signed installation path and transfers
matrix build, signature, and repository distribution mechanics to DuckDB's
program. It requires an approved open-source license, public source,
DuckDB-maintainer acceptance, community CI compatibility, and ongoing source
ref maintenance. Community signing verifies build provenance but is not a code
audit. This is the proposed target because it preserves the default trust
policy and lowest-friction user experience.

## Drawbacks and failure modes

- The project depends on DuckDB-maintainer review, infrastructure, accepted
  naming, and current community-program rules. Engineering Enablement
  facilitates those mechanics only through a bounded delivery interaction or a
  reusable gate with an explicit maintenance owner. The product manager accepts
  that dependency; release evidence must expose upstream rejection or drift.
- Community repository updates and the project's immutable release/version
  expectations may not align. Query Experience owns truthful user guidance;
  guidance must state that `0.2.0` guarantees neither Community rollback nor
  historical availability.
- A valid community signature can still cover vulnerable native code. Query
  Experience must not describe signing as security vetting.
- The community matrix may expose portability defects outside the current
  product cell. Unsupported cells must fail or remain unclaimed rather than
  being inferred from one successful build.
- The controlled unsigned path is easy to copy into production guidance.
  Examples and release notes must label it at the invocation, not only in a
  general disclaimer.

## Acceptance and verification

- **End-to-end demonstration:** Verify the bundle, install into empty state,
  repeat install without byte drift, terminate, restart, load by name, identify
  version, and run the exact query; reject signature, version, platform, and
  corruption canaries before registration. For delivery, repeat the successful
  lifecycle from stock DuckDB through the Community endpoint on every claimed
  matrix row.
- **Automated oracle:** `experiments/repeatable-installation/install_oracle.py`
  through `scripts/run-installability-trial.sh` with provider-owned manifest
  verification and negative fixtures.
- **Quality gates:** Repository asset validation, source-identity validation,
  dependency-license audit, focused provider and Query oracles, applicable
  fresh product evidence, Community CI, hosted artifact download inspection,
  and Git whitespace checks.
- **Independent review:** Query lifecycle and diagnostic review plus
  Engineering Enablement supply-chain, reproducibility, and custody review;
  adversarial review before the bounded goal closes.
- **Interaction exit:** Query independently runs and maintains every oracle
  through explicit evidence paths without build or CI implementation knowledge.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Distribution, compatibility, update, and support policy | Record the accepted Community channel and exact evidence-derived support boundary | Propagate with RFC acceptance |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Not affected | None; installation does not change connector semantics | Existing contract identity oracle |
| `docs/RUNTIME_CONTRACTS.md` | Not affected | None; artifact acquisition grants no runtime network authority | Existing `0.1.0` runtime evidence |
| `docs/TEAM_TOPOLOGY.md` and active charters | Interaction only | No charter change if facilitation exits | Team plans and exit evidence |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Use the existing bounded-trial and RFC workflow | Goal and review record |
| Examples, diagnostics, fixtures, and tests | Affected | Add the stock-DuckDB Community lifecycle oracle and exact passing-row matrix; publish normal guidance only after it passes | Bounded trial complete; Community delivery proof pending |
| Release and support documentation | Affected | Record MIT, Community channel, exact matrix, forward-only fixes, no guaranteed rollback/history, and best-effort GitHub Issues support | Required for `0.2.0` delivery |

The RFC records rationale; the propagated contracts and executable oracles
define behavior after acceptance.

## Unresolved delivery evidence

- Which exact Community CI platform rows pass the complete `0.2.0` release
  oracle on the latest stable DuckDB identity at release time?
- Does the hosted custody workflow preserve the complete intended evidence set
  after upload and download?
- Does the dependency-license audit identify every required notice or
  redistribution condition?

These questions determine delivery completion and the exact support matrix;
they do not reopen the accepted product policy.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Query Experience perspective | Query Experience | Approved | The controlled lifecycle and failure oracle plus primary-source Community mechanics establish the decision facts. The PM resolved compatibility, lifecycle, immutability, rollback/history, backport, and support policy. Community build/sign/install and the exact passing matrix are delivery evidence | No objection. Require the stock-DuckDB Community lifecycle oracle on every claimed row before ordinary-user guidance; the Query–Enablement interaction exit remains open during delivery |
| Engineering Enablement perspective | Engineering Enablement | Approved | The deterministic local trial, two-workspace reproduction, custody design, negative oracles, and channel comparison establish the decision facts. Hosted custody and Community build/sign/install remain delivery evidence | No objection. Keep facilitation bounded and exit only after hosted custody proof and demonstrated Query self-sufficiency |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product license:** MIT, selected by the product manager on 2026-07-17 and
  recorded in the root `LICENSE`.
- **Distribution and trust:** DuckDB Community Extensions is the ordinary-user
  channel; source build and verified unsigned artifacts remain contributor or
  controlled-preview paths.
- **Compatibility:** Support the latest stable DuckDB release at release time
  and only the exact Community CI platform rows that pass the complete delivery
  oracle. Every other row is unclaimed.
- **Lifecycle and support:** Before `1.0`, fixes move forward without a
  backport commitment. Project releases are immutable. `0.2.0` guarantees no
  Community rollback or historical availability. Project support is
  best-effort through GitHub Issues with no SLA.
- **Rationale:** The bounded trial proves repeatable verified installation and
  deterministic refusal for the exact `0.1.0` cell, while stock DuckDB still
  rejects the self-built artifact under default signature policy. Community
  Extensions is therefore the lowest-friction path that preserves the ordinary
  trust boundary. Both required teams approve the decision and distinguish the
  remaining external proofs as delivery acceptance evidence.
- **Material objections:** None remain for the decision. Community
  build/sign/publish/install, exact matrix, dependency audit, hosted custody,
  user guidance, and the facilitation exit remain open delivery gates.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Deliver the accepted `0.2.0` installation path | Query Experience | Engineering Enablement — Facilitation until Query owns the oracle | RFC 0004 Accepted and product boundaries recorded |
| Submit and prove the Community Extension path | Query Experience | Engineering Enablement — Facilitation for descriptor, matrix, provenance, and custody | Activated `0.2.0` goal and product-manager authorization for external submission |
