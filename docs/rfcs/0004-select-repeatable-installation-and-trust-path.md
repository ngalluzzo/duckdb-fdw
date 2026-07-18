# RFC 0004: Select the repeatable installation and trust path

```yaml
rfc: "0004"
title: "Select the repeatable installation and trust path"
status: "Draft"
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
Extensions as the target direction. Do not publish or claim the ordinary-user
path until the remaining compatibility, update, and support boundaries are
approved and the external path is proved.

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
under MIT, clearing the project-license prerequisite but not the remaining
product decisions, dependency audit, or external submission evidence.

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

Unknowns reserved for the product decision are the compatibility support
window, update/backport policy, and support boundary.

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

Pursue DuckDB Community Extensions as the normal install channel. Community
source descriptors will pin an immutable source ref, extension SemVer, build
language, project license, and maintainers. The DuckDB-managed pipeline will
build, sign, and distribute the compatible artifact. Normal user guidance will
use DuckDB's community repository syntax only after that path passes its
external build matrix and this repository records the approved product
boundaries.

Keep two explicitly non-production paths:

1. Source build remains the contributor and deep-diagnostic path.
2. A manifest- and checksum-verified unsigned artifact may support controlled
   previews and installation trials. It requires an isolated host that was
   deliberately started with unsigned extensions enabled. It is not the
   default user path and carries no broader compatibility promise.

Artifact evidence for every path binds project version, source commit and tree,
DuckDB version and commit, DuckDB platform, artifact size and checksum, public
contract identity, and the build/custody method. Published release artifacts
are immutable. An unsupported DuckDB version or platform must fail before
extension initialization, and an integrity mismatch must fail before DuckDB is
invoked.

### Public behavior

The eventual ordinary-user experience is intended to be:

```sql
INSTALL duckdb_api FROM community;
LOAD duckdb_api;
```

This Draft does not publish that instruction as currently supported. The
existing source-built unsigned path and exact `0.1.0` compatibility cell remain
the only accepted behavior until this RFC is Accepted and its delivery gates
pass. The SQL query, extension version surface, and query diagnostics do not
otherwise change.

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
replace verified bytes. Update, rollback, historical-version selection, and
backport behavior remain unapproved pending the product decisions below.

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
- **Concurrency, immutability, and state ownership:** Published artifacts and
  source refs are immutable. A new project release produces a new identity;
  it does not relabel an existing artifact.
- **FFI, initialization, reload, and shutdown:** Native code must not run on a
  rejected artifact. A separate process proves installed load because an
  extension cannot be unloaded or reloaded safely in-process.
- **Diagnostics and redaction:** Rejections may include safe version,
  platform, artifact name, and expected/observed checksum context. They must
  not expose credentials, unrestricted environment contents, or sensitive
  local paths in product diagnostics.

## Compatibility and migration

No migration is authorized by this Draft. The current accepted path remains
source build plus direct unsigned local load on DuckDB 1.5.4 `osx_arm64`.

Acceptance must state which DuckDB releases and platforms are supported, how
long a released cell remains supported, whether prior project versions receive
backports, and how a user selects or rolls back a version. A Community
Extension update can replace the artifact served for one DuckDB
version/platform location, so project release immutability and user-selectable
historical versions cannot be inferred from repository layout. If those needs
cannot be reconciled, the normal channel must remain unselected or use a
separately governed distribution design.

Rollback from a failed community publication is limited: previously installed
cached bytes may remain on user machines. A corrected project version must be
new and immutable, with explicit release notes; moving or reusing a Git tag is
forbidden.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Verified unsigned artifact can cross install and restart | Empty-state install, repeated install, separate-process load by name, version and exact query | `experiments/repeatable-installation/install_oracle.py` | Passed: installed SHA-256 remained `4f1a0678…`; restart/load returned the exact `0.1.0` contract. Controlled unsigned policy only |
| Default trust does not accept the self-built artifact | Signature-policy refusal and empty function inventory | Fresh default-policy host | Passed: DuckDB 1.5.4 refused the invalid signature before registration or installed bytes |
| Incompatible host and platform fail before registration | DuckDB 1.5.3 and documented wrong-platform footer fixtures | Fresh host and extension directories | Passed: diagnostics named both DuckDB versions and both platforms; one exact negative cell each |
| Corrupted download is stopped before DuckDB | Manifest anchor, size, and SHA-256 mismatch | Body-corrupted copy; host invocation sentinel | Passed: the verifier rejected SHA-256 `363a9183…` with zero Query-host invocations; proves bundle integrity, not publisher authenticity |
| Release artifact is byte-reproducible | Two independent clean workspaces produce the trusted extension bytes | Independently anchored reproduction evidence and `verify_reproduced_artifacts.py` | Passed: both 4,859,678-byte artifacts equal trusted SHA-256 `4f1a0678…`; one recorded source and product cell only |
| CI preserves the intended evidence bundle | Downloaded workflow artifact contains manifest and artifact, not only a log | Visible allowlisted custody stage, pinned upload/download actions, inner-evidence verifier, and retention guard | The hidden-`.build` omission is repaired and locally guarded; a new GitHub-hosted workflow round trip remains pending after integration |
| Community path is accepted by stock DuckDB | DuckDB-managed build, signing, publication, clean community install | Community Extensions submission and matrix | Not run; MIT is selected, but the approved compatibility boundary and external maintainer coordination remain prerequisites |

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
  reusable gate with an explicit maintenance owner; the product manager owns
  whether the dependency is acceptable.
- Community repository updates and the project's immutable release/version
  expectations may not align. Query Experience owns truthful user guidance;
  the final RFC must resolve the mismatch before acceptance.
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
  corruption canaries before registration.
- **Automated oracle:** `experiments/repeatable-installation/install_oracle.py`
  through `scripts/run-installability-trial.sh` with provider-owned manifest
  verification and negative fixtures.
- **Quality gates:** Repository asset validation, source-identity validation,
  focused provider and Query oracles, applicable fresh product evidence, CI
  artifact download inspection, and Git whitespace checks.
- **Independent review:** Query lifecycle and diagnostic review plus
  Engineering Enablement supply-chain, reproducibility, and custody review;
  adversarial review before the bounded goal closes.
- **Interaction exit:** Query independently runs and maintains every oracle
  through explicit evidence paths without build or CI implementation knowledge.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Not affected by trial | Update only after an accepted public distribution decision requires it | Pending RFC acceptance |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Not affected | None; installation does not change connector semantics | Existing contract identity oracle |
| `docs/RUNTIME_CONTRACTS.md` | Not affected | None; artifact acquisition grants no runtime network authority | Existing `0.1.0` runtime evidence |
| `docs/TEAM_TOPOLOGY.md` and active charters | Interaction only | No charter change if facilitation exits | Team plans and exit evidence |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Use the existing bounded-trial and RFC workflow | Goal and review record |
| Examples, diagnostics, fixtures, and tests | Affected | Add isolated experimental install and failure evidence; publish normal guidance only after acceptance | Bounded trial complete; community proof pending product approval |
| Release and support documentation | Affected after acceptance | Record channel, license, matrix, updates, rollback, and support window | Pending product approval and delivery |

The RFC records rationale; the propagated contracts and executable oracles
define behavior after acceptance.

## Unresolved questions

- Which DuckDB versions, platforms, and architectures will the first supported
  installation promise include?
- What are the support window, update cadence, backport policy, rollback story,
  and user-selectable historical-version requirements?
- Is dependence on DuckDB Community Extensions acceptable if its current
  publication behavior cannot provide the required historical-version model?

These are decision-critical product questions. They must move into recorded
decisions before this RFC can enter final decision.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Query Experience perspective | Query Experience | Objected | The controlled unsigned lifecycle oracle passes, but the proposed stock-DuckDB Community path is unrun and its compatibility, update, rollback, and support boundaries are undecided | Keep Draft. Obtain the remaining reserved product decisions, prove the DuckDB-managed build/sign/install path across the approved boundary, and repeat Query review before acceptance |
| Engineering Enablement perspective | Engineering Enablement | Approved with conditions | Local trial, two-workspace reproduction, custody, identity, and product gates pass. Hosted transfer and Community build are unrun; PM-reserved policy remains unresolved; facilitation exit is still open | Keep Draft, retain the external proofs as delivery evidence, and avoid indefinite Enablement ownership. Exit only after hosted custody proof and demonstrated Query self-sufficiency |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product license:** MIT, selected by the product manager on 2026-07-17 and
  recorded in the root `LICENSE`.
- **Distribution and trust direction:** The product manager aligned with DuckDB
  Community Extensions as the ordinary-user target. Publication remains
  unauthorized until the RFC gates pass.
- **Product approval still pending:** Compatibility, updates, rollback,
  historical-version behavior, backports, and support.
- **Rationale:** The bounded trial proves repeatable verified installation and
  deterministic refusal for the exact `0.1.0` cell, while stock DuckDB still
  rejects the self-built artifact under default signature policy. Community
  Extensions is therefore the lowest-friction candidate that preserves the
  ordinary trust boundary; acceptance remains gated on the reserved product
  decisions, external Community evidence, and a repeat Query review.
- **Material objections:** Query Experience requires a DuckDB-managed
  build/sign/install demonstration across the approved compatibility boundary
  before ordinary-user guidance can be accepted. Engineering Enablement's
  hosted upload/download custody proof, Community build evidence, PM-reserved
  policy, and facilitation exit also remain open.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Deliver the accepted `0.2.0` installation path | Query Experience | Engineering Enablement — Facilitation until Query owns the oracle | RFC 0004 Accepted with product approval and external prerequisites satisfied |
| Submit and prove the Community Extension path | Query Experience | Engineering Enablement — Facilitation for descriptor, matrix, provenance, and custody | MIT license recorded, accepted RFC, and product manager authorization for external submission |
