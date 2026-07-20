# RFC 0009: Set the native v1 boundary and dependency-ordered release path

```yaml
rfc: "0009"
title: "Set the native v1 boundary and dependency-ordered release path"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Query Experience"
technical_decision_owner: "Lead agent"
product_approver: "Product manager"
authors:
  - "Lead agent"
required_reviewers:
  - "Connector Experience perspective"
  - "Query Experience perspective"
  - "Remote Runtime perspective"
  - "Relational Semantics perspective"
  - "Engineering Enablement perspective"
affected_teams:
  - "Connector Experience"
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
linked_outcome_or_objective: "ROADMAP.md 1.0.0 — narrow stable contract"
supersedes: "RFC 0004"
```

## Summary

Make the permanent native C++ table-function product, rather than an unproven
Rust or stable-C-API replatform, the intended `1.0.0` integration profile.
Define the exact categories governed by project SemVer, narrow the stable v1
connector-package subset to capabilities exercised before freeze, distinguish
local loaded-package registration from central package distribution, and order
the remaining releases so semantics, protocol reuse, package lifecycle, public
authoring, and compatibility rehearsal prove each dependency before the next
release relies on it.

## Sponsorship and context

- **RFC type:** Product.
- **Sponsoring team:** Query Experience.
- **Linked outcome:** `ROADMAP.md 1.0.0 — narrow stable contract`.
- **Why now:** The project has permanent native product source through the first
  predicate-selective `0.6.0` slice. The next roadmap outcomes would otherwise
  prove protocol and lifecycle behavior against an unspecified integration and
  package boundary, then discover or freeze that boundary in `0.9.0`.

The product manager approved the native-v1 direction, the public SemVer
inventory categories, the dependency-ordered release progression, and the
deliberately narrow stable package boundary on 2026-07-19. Connector Experience
retains accountability for later author outcomes, Query Experience is
accountable for the DuckDB-user v1 outcome, and Engineering Enablement
facilitates the inventory and release gates. This RFC governs the cross-team
release and compatibility frame within which those goals are shaped. The
approval did not select a support-window,
support-removal, deprecation, security-response, maintenance, or backport
policy; those remain behind the explicit pre-freeze decision gate below.

## Problem

The current documents describe a recognizable v1 product but not one coherent
path to it:

- permanent production source is a native C++ DuckDB table-function extension,
  while the architecture and roadmap make a future portable Rust/stable-C-API
  profile the intended v1 integration without assigning any release to parity,
  migration, or compatibility proof;
- `0.8.0` promises connector registration and reload before `0.9.0` first
  promises validation, compilation, loading, and a frozen package and SQL
  boundary; and
- the full draft connector specification explores providers, partitions,
  retries, rate-limit waits, caching, transforms, custom code, importers, and
  other capabilities that no remaining milestone independently proves before
  `0.9.0` declares a non-draft specification candidate.

For example, an operator cannot prove atomic reload or SQL-name collision
behavior without a permanent compiled-package and loading contract. Likewise,
a release can demonstrate a GraphQL protocol component without giving a DuckDB
user or connector author any new supported outcome, contrary to the roadmap's
own release-value rule.

The existing public documents also use “connector registry” for two different
things: the in-process set of explicitly loaded packages needed for registration
and reload, and a central distribution service with discovery, dependency,
signing, and trust infrastructure. Only the latter is excluded from v1.

## Decision drivers and invariants

- **Must preserve:** Every relational, security, resource, replay, lifecycle,
  strict-conversion, offline-planning, and conservative-fallback invariant in
  `AGENTS.md` and the system-design contracts.
- **Must preserve:** RFC 0004's MIT license, DuckDB Community Extensions target
  for ordinary-user distribution, source-build contributor path, immutable
  releases, latest-stable DuckDB requirement at initial stable release, exact
  evidence-derived Community rows, and best-effort GitHub Issues support. This
  RFC supersedes only RFC 0004's requirement to complete Community publication
  as a `0.2.0` delivery gate.
- **Must enable:** Coherent DuckDB-user and connector-author outcomes whose
  prerequisites are permanent and already exercised before compatibility
  freeze.
- **Must enable:** A precise public-inventory oracle that can detect an omitted
  or incompatible SQL, package, configuration, diagnostic, compatibility,
  distribution, migration, support, or exclusion change.
- **Must not introduce:** A language replatform as a hidden v1 dependency, a
  public C++ or Rust ABI, an implication that every design-proposal feature is a
  v1 promise, or a release justified only by component completion.

## Proposed decision

### v1 integration profile

The `1.0.0` product integration is the native C++ DuckDB table-function
extension descended from the permanent `src/` product. It is supported only on
the exact DuckDB, platform, architecture, toolchain, and installation rows
published in the release matrix and proven by the complete product oracle.

This is an implementation and compatibility-profile decision, not a public
C++ ABI. The deep native catalog/optimizer profile remains excluded. A future
Rust implementation, stable C Extension API adapter, or shared portable core is
permitted only through its own evidence and RFC; it is not a v1 gate and gains
no compatibility status from this decision.

### Public behavior and SemVer inventory

Project SemVer governs only entries explicitly admitted to the schema-backed
canonical public inventory. Within that inventory, the governed categories
are:

1. extension name, project version, installation and update commands, and
   version introspection;
2. SQL functions, procedures, macros or views supplied by the project,
   including names, arguments, defaults, relation schemas, nullability claims,
   connection and secret behavior, cancellation, and query-visible lifecycle;
3. stable diagnostic categories and safe fields, connector-specific plan
   explanation, and any documented standard `EXPLAIN` integration;
4. the supported connector-spec identifier and stable package subset, package
   loading and registration behavior, package identity and digest rules,
   validation and migration behavior, and the validate, compile, explain,
   fixture-test, and load author workflows;
5. observable relational, conversion, security, network, resource, replay,
   concurrency, reload, shutdown, and failure guarantees; and
6. the release-specific DuckDB compatibility matrix, distribution channel,
   support and deprecation policy, migration guidance, and explicit
   experimental or unsupported surfaces.

Internal C++ types and headers, Rust-like design notation, build-target layout,
`CompiledConnector`, `ScanRequest`, `ScanPlan`, `BatchStream`, protocol service
interfaces, runtime queues, caches, and fixture-only composition remain outside
project SemVer unless a later RFC publishes a separately versioned interface.
Illustrative or design-proposal syntax elsewhere in the repository is
non-normative and outside project SemVer until an accepted decision and the
inventory gate admit it as public behavior.

The exact SQL spelling and supported v1 relations are not chosen by this
release-governance decision. Query Experience must decide and exercise them in
an accepted product RFC before `0.8.0` makes registration or naming behavior a
release dependency. `0.9.0` freezes only syntax and behavior already delivered
and migration-tested in an earlier preview.

Likewise, this RFC does not guess the stable successor to
`duckdb_api/draft`. Before `0.8.0`, Connector Experience must sponsor an
accepted product RFC that chooses that identifier, defines its compatibility
and migration rules, and states its relationship to project SemVer and the
independent SemVer of each connector package. The `0.8.0` compiler and fixtures
must use the chosen identifier; `0.9.0` may validate and freeze that proven
contract but cannot introduce it. Because `duckdb_api/draft` authoring was
never activated, it creates no accepted package-migration source: validators
must reject it explicitly, and migration fixtures begin with bounded package
previews produced under the accepted successor contract.

### Stable v1 connector-package boundary

The stable v1 authoring candidate is local, explicit, declarative, read-only,
and static-schema. Its required subset contains:

- a versioned connector manifest, stable connector and relation identifiers,
  package digest identity, and explicit local loading;
- typed static columns, separate typed inputs, named base-row operations, strict
  JSON extraction and conversion, and deterministic operation selection;
- REST/JSON operations, exact or superset predicate declarations, conservative
  fallback, projection closure, safe ordering and limit declarations, and at
  least one bounded sequential pagination strategy;
- anonymous and capability-scoped bearer authentication, connector network
  policy, secret placement restrictions, and connector budgets that only
  narrow host authority; and
- schema-backed validation, source-located diagnostics, deterministic
  compilation and explanation, offline fixture tests, atomic load, and package
  migration fixtures.

GraphQL is conditional: `0.7.0` must prove one user-visible representative
GraphQL relation through the same permanent planning, policy, stream,
diagnostic, and lifecycle boundaries or the v1 claim is narrowed to REST before
release.

The following design areas are not in the stable v1 package subset unless a
later accepted RFC and pre-freeze product evidence add them: dynamic schemas;
Tier 2 JQ-compatible transforms; Tier 3 native or WASM code; custom protocols
or pagination ABIs; column providers; partitions; automatic retry or rate-limit
waiting; cache and single-flight controls; OpenAPI or GraphQL importers; OAuth,
SigV4, GitHub App, or caller-defined authenticators; central connector
discovery or distribution registries; dependency resolution and lockfiles; and
connector-package signing or trust infrastructure. Runtime implementations may
keep a feature disabled internally, but validators must reject unaccepted
author syntax rather than silently treating it as stable.

### Local registration versus central distribution

V1 includes a bounded in-process registry of packages loaded from explicit
local paths. It owns deterministic SQL naming, collision refusal, atomic
initial publication and replacement, immutable in-flight snapshots, and
new-scan visibility after successful reload. A failed multi-relation load or
replacement publishes no partial package and leaves the prior registry state
unchanged. The registry does not discover packages, resolve dependencies,
fetch from Git, select remote versions, maintain lockfiles, or establish
publisher trust.

DuckDB Community Extensions remains the distribution and trust path for the
`duckdb_api` extension itself under this RFC, carrying forward RFC 0004's
durable channel choice. That decision is separate from distribution or trust
for connector packages.

### Remaining release progression

- **`0.6.0` — semantic trust and explainable optimization.** Complete the
  protocol-neutral semantic-law matrix for exact, superset, unsupported,
  ambiguous, and failure classifications; composition and `NULL` behavior;
  projection closure; filtering and ordering before limits; capability-profile
  fallback; strict conversion; and deterministic explanation. Correct local
  execution is sufficient when remote optimization is unavailable. The
  milestone does not require remote projection, ordering, or limit pushdown.
- **`0.7.0` — reusable protocol product path.** A DuckDB user queries a second
  representative API shape through the same permanent connector, semantics,
  runtime, and Query interfaces. Retain REST and GraphQL only if the GraphQL
  relation passes the same user, policy, semantic, diagnostic, and lifecycle
  evidence; otherwise narrow the public claim before release. Internal protocol
  machinery alone is not a release outcome.
- **`0.8.0` — local package lifecycle and analytical workflows.** Before the
  release begins, the intended v1 package-candidate subset is closed by the
  accepted connector-spec version RFC. Every author-facing declaration retained
  in that subset is implemented and exercised through one or more
  repository-owned packages using the permanent validation, compilation,
  explanation, fixture, loading, registration, and reload path. Users exercise joins,
  materialization, export, prepared and repeated scans; operators exercise
  collisions, all-or-nothing initial publication and reload, immutable
  in-flight snapshots, shutdown, and resource failure. The authoring surface
  remains a preview but is complete for
  the proposed v1 subset, permanent, and not throwaway.
- **`0.9.0` — public authoring and API candidate.** Connector authors use the
  complete accepted v1 subset and tools against multiple independently authored
  packages, and migration fixtures cover the preceding preview. The project
  freezes the already exercised SQL, package,
  configuration, diagnostic, explain, compatibility, distribution, migration,
  support, and exclusion inventory. No first implementation of a frozen surface
  is allowed in this milestone.
- **`1.0.0-rc.N` — compatibility rehearsal.** Exact immutable candidate
  artifacts run the frozen clean-install, upgrade, migration, relational,
  security, lifecycle, failure, and support oracles. An RC introduces no design
  decision.
- **`1.0.0` — narrow stable contract.** Publish only after every promised
  inventory item and matrix row passes the complete release gate.

Semantic Versioning does not require `0.9.0` to be the last prerelease. If an
approved outcome cannot be proven without another coherent preview, the roadmap
may add `0.10.0` or later rather than compressing unproven work into the API
candidate or RC.

### Shared interfaces

No current C++ team API changes in this decision. Later release goals retain the
existing provider/consumer direction:

- Connector Experience produces immutable validated connector metadata;
- Query Experience produces capability-profiled `ScanRequest` and consumes
  `ScanPlan` plus bounded streams;
- Relational Semantics exclusively classifies relational meaning; and
- Remote Runtime executes accepted plans without reconstructing package or
  relational authority.

The v1 public inventory must not publish those internal team APIs by accident.

### Operational behavior

This decision does not enable a new network, credential, retry, cache,
concurrency, FFI, or lifecycle capability. Each later product RFC must preserve
least authority, offline planning, bounded execution, cancellation, immutable
snapshots, replay safety, redaction, and failure containment. Unsupported
features remain explicit and fail closed.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Engineering Enablement | Release-governance facilitator | Own the canonical public-inventory schema and reusable release classification gate, then transfer domain entries to their owners | Facilitation | Each domain team independently maintains and verifies its inventory entries without Enablement approval |
| Connector Experience | Owner of later author outcomes | Define and prove the stable package subset, compiler diagnostics, fixtures, and migrations | Collaboration, then X-as-a-Service provider | Query, Semantics, and Runtime consume immutable compiled packages without authoring internals |
| Query Experience | Sponsor and owner of the DuckDB-user v1 outcome | Decide and prove SQL, registration, explanation, lifecycle, and compatibility behavior | Collaboration, then X-as-a-Service consumer | The intended SQL and compatibility matrix pass without package, planner, or runtime internals in the adapter |
| Remote Runtime | Platform provider | Supply protocol, policy, stream, reload, shutdown, and failure capabilities only for named stream outcomes | Collaboration, then X-as-a-Service provider | Both stream teams use documented runtime services without bespoke protocol or lifecycle coordination |
| Relational Semantics | Complicated-subsystem provider | Supply the semantic-law matrix and reject unproved author declarations or pushdown | Collaboration, then X-as-a-Service provider | Connector validation and Query capability profiles share one executable conservative planning oracle |

No accountability or charter boundary moves. The decision reduces cognitive
load by preventing each team from treating the full design proposal or a future
language implementation as an implicit v1 obligation.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** The release sequence now
  requires the protocol-neutral law matrix before protocol breadth. Remote
  optimization is optional; DuckDB-equivalent results and truthful ownership
  are mandatory.
- **Authentication, credentials, network policy, and privacy:** V1 authoring is
  limited to anonymous and capability-scoped bearer behavior already grounded
  by native evidence. Any broader authenticator requires an RFC and separate
  security evidence.
- **Resource budgets, backpressure, and cancellation:** Existing mandatory
  bounds remain. Later protocols and packages must use the same host-enforced
  capabilities rather than define independent authority paths.
- **Replay units, retries, caching, and duplicate prevention:** Replay safety
  remains a required invariant even with one attempt. Author-facing retry,
  rate-wait, and cache controls are outside the stable v1 subset.
- **Concurrency, immutability, and state ownership:** `0.8.0` must prove atomic
  initial multi-relation publication and replacement, including rollback after
  late validation, registration, or collision failure, and immutable in-flight
  snapshots before those behaviors enter the candidate inventory.
- **FFI, initialization, reload, shutdown, and failure containment:** Native C++
  removes a cross-language FFI from the v1 requirement. DuckDB callback
  exception containment, process/runtime initialization, package reload, and
  extension shutdown remain required product evidence. Dynamic extension DSO
  unload need not be promised.
- **Diagnostics, redaction, metrics, and progress:** Stable diagnostic
  categories, safe fields, explanation, and version output enter the public
  inventory. Metrics and progress remain excluded unless separately accepted
  and exercised before freeze.

## Compatibility and migration

Pre-v1 releases retain the existing policy: a `0.Y.0` may make an incompatible
change with curated release notes and migration guidance. Current
`duckdb_api_scan` syntax and the three fixed GitHub relations are preview public
surfaces; this RFC neither freezes nor removes them. Query Experience must
choose their v1 disposition before `0.8.0`, and `0.9.0` must test the resulting
migration from every retained preview surface.

Beginning with `1.0.0`, the admitted public inventory above follows project
SemVer. The initial stable release must support the latest stable DuckDB release
at release time and only the exact Community CI platform rows that pass the
complete v1 oracle. The compatibility classification for later adding,
removing, or time-limiting a row is not decided here. Before `0.9.0` activation,
Engineering
Enablement must sponsor an accepted non-product release-and-support-policy RFC
with product-manager approval. It must decide support windows and removals,
deprecation, migration, security response, maintenance, and backport policy and
make those rules executable by the public-inventory classification gate.
Unsupported or absent adapter capabilities fall back conservatively or fail
with an actionable diagnostic; they never cause SQL-text reconstruction or
silent semantic changes.

This RFC supersedes RFC 0004 as the current distribution and compatibility
decision. It carries forward Community Extensions as the ordinary extension
channel, MIT licensing, source build as the contributor path, immutable
releases, the latest-stable requirement for the initial stable release, and
best-effort support through GitHub Issues. It removes Community publication
from the completed `0.2.0` gate: publication evidence remains mandatory before
ordinary-user guidance and `1.0.0`, but it does not block intervening product
releases. The exact initial v1 matrix is evidence-derived and frozen in
`0.9.0`; this RFC does not claim rows that have not passed.

A future portable implementation may coexist behind the same public inventory
only after it passes every claimed row and behavior. Replacing the native v1
profile or changing its support promise requires compatibility review and the
SemVer consequence dictated by the then-current public contract.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Is the native product a permanent, topology-aligned implementation rather than only a spike? | Production source and focused tests separated across Connector, Query, Semantics, and Runtime with one end-to-end product oracle | Inspect `src/`, `test/`, build targets, accepted RFCs 0003–0008, and the `0.6.0` public contract | Established for the current native subset; broader protocols and packages remain future delivery evidence |
| Is a portable Rust/stable-C-API profile proven enough to be a v1 dependency? | Permanent source plus build/load/query/cancellation and compatibility evidence on the claimed profile | Repository inventory and current release evidence | Not established: no permanent Rust product or portable-profile oracle exists. This supports removing it as a v1 gate, not claiming that it is infeasible |
| Can conservative local execution satisfy relational trust without every remote optimization? | Planner counterexamples and DuckDB end-to-end ordering/filter/limit equivalence | Current native planner and DuckDB product tests, expanded by the `0.6.0` semantic-law gate | Established for the current local-ownership envelope; the general law matrix remains `0.6.0` delivery evidence |
| Are Community Extensions and MIT still open decisions? | Accepted decision and repository license | RFC 0004, this superseding RFC, and root `LICENSE` | Resolved and carried forward; external Community delivery rows remain evidence-derived and are required before ordinary-user guidance and v1 |
| Can package lifecycle precede a public authoring freeze? | Permanent minimum compiler/loader plus atomic register/reload and migration oracles | Required `0.8.0` product evidence | Not yet implemented; it is a delivery dependency, not a reason to defer the ordering decision |

No decision-critical feasibility trial is pending. The proposal selects the
already proven implementation lineage and makes every broader capability earn
its public status through later product evidence.

## Alternatives considered

### Keep portable Rust/stable-C-API integration as a v1 requirement

This could reduce DuckDB internal-version coupling and provide a portable core
for later integrations. The repository has no permanent implementation or
product oracle for it, so retaining it would make an unbounded replatform a
hidden prerequisite after the native product has already earned permanent
structure. If portability becomes product-critical, it should receive its own
coherent release rather than being buried inside protocol breadth.

### Preserve the current `0.7.0` through `0.9.0` wording

This minimizes document change. It allows an internal protocol component to
count as a release, asks registration/reload to precede the package it
registers, and combines first authoring implementation with compatibility
freeze. The dependency and feedback risks are concrete and were identified by
all affected charter perspectives during the roadmap review.

### Ship v1 only for curated built-in connectors

This would substantially reduce the package/compiler surface and may deliver a
stable user product sooner. It abandons the already approved v1 author outcome
and the project's declarative-adapter claim. The product manager retained a
narrow public local-authoring surface instead.

### Move the full YAML compiler and authoring surface earlier

This could expose author feedback sooner. It would again prioritize a broad
distribution-shaped language before the reusable protocol, semantics, and
runtime boundaries are proven. The selected sequence implements and exercises
the complete proposed v1 subset in `0.8.0`; `0.9.0` adds independent-author and
migration breadth before freezing that already proven surface.

## Drawbacks and failure modes

- Native DuckDB C++ APIs carry per-version compatibility and build-matrix work.
  Query Experience owns truthful compatibility claims; Enablement supplies a
  reusable matrix gate without becoming the approver.
- Narrowing the v1 package subset means the design documents will continue to
  discuss post-v1 capabilities. Connector Experience must label their status
  clearly enough that authors do not mistake design exploration for accepted
  syntax.
- `0.8.0` now contains both author-facing preview value and operator lifecycle
  value. Goal shaping must split independent outcomes if they cannot be proven
  in one end-to-end package-to-query narrative.
- Conditional GraphQL can become an indefinite claim. The `0.7.0` release must
  either pass the named user-visible oracle or explicitly remove GraphQL from
  v1; it cannot carry an unresolved condition into `0.9.0`.
- A manually curated inventory can omit a surface. Engineering Enablement must
  establish a schema-backed, fail-closed completeness and change-classification
  oracle before the API candidate.

## Acceptance and verification

- **End-to-end demonstration:** Documentation presents one coherent native v1
  boundary and dependency-ordered release path while preserving every current
  delivered behavior and accepted distribution/license decision.
- **Automated oracle:** `ruby scripts/validate-agent-assets.rb`, public-document
  reference searches, Markdown/link inspection, and Git whitespace gates. The
  canonical inventory completeness oracle is a required pre-`0.8.0` follow-on,
  not fabricated in this governance-only change.
- **Quality gates:** `ruby scripts/validate-agent-assets.rb`, `git diff --check`,
  and `git diff --cached --check`.
- **Independent review:** Fresh Connector Experience, Query Experience, Remote
  Runtime, Relational Semantics, and Engineering Enablement RFC reviews plus
  independent adversarial review of compatibility, FFI/lifecycle, and contract
  propagation.
- **Interaction exit:** This RFC's collaboration exits when every affected
  contract distinguishes current delivery, v1 stable intent, and post-v1
  design; later product interactions remain open until their named source and
  test evidence exists.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Make native C++ the v1 table-function profile, move portable Rust/stable-C-API and custom code to future optional profiles, and narrow the v1 claim | Propagated in the integration strategy, product claim, profile status, and decision summary |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected | Add the stable v1 candidate subset and clearly label post-v1 design areas; distinguish local loading from central distribution | Propagated in the candidate boundary, auth catalog, and distribution sections |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Make native C++ the v1 implementation mapping while retaining language-neutral internal contracts and future portability options | Propagated in the purpose, v1 capability boundary, native mapping, lifecycle, registry, unresolved-design, and summary sections |
| `ROADMAP.md` | Affected | Replace the dependency-inverted `0.7.0`–`0.9.0` outcomes, correct `0.6.0`, and record the accepted v1 inventory and governance decisions | Propagated in the remaining release progression, v1 boundary, exclusions, and governance gates |
| `docs/rfcs/0004-select-repeatable-installation-and-trust-path.md` | Affected | Mark the old `0.2.0` publication timing superseded while carrying its durable license, channel, immutability, initial compatibility, and support choices into this RFC | Propagated through RFC 0004 status and supersession notice |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | Existing accountability, team APIs, and interaction modes govern the revised sequence | Charter-backed RFC review and unchanged boundaries |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing goal, RFC, contract-change, topology, review, and delivery processes enforce the decision | Repository skill validation |
| Examples, diagnostics, fixtures, tests, and release evidence | Not affected by this governance-only change | Current preview behavior remains unchanged; future product goals add the named evidence before claiming release outcomes | Existing public-contract identity remains unchanged |

The RFC records rationale; the propagated contracts define current direction.

## Unresolved questions

- Which exact SQL spelling and preview relations become the v1 candidate?
- What stable identifier succeeds `duckdb_api/draft`, what compatibility and
  migration rules govern it, and how does it relate to project and connector
  package SemVer? Connector Experience must decide this in an accepted product
  RFC before `0.8.0`.
- Which representative GraphQL relation, if any, should carry the `0.7.0`
  user-visible decision?
- Which exact DuckDB and Community platform rows will pass the initial v1
  matrix?
- What support-window and removal, deprecation, migration, security-response,
  maintenance, and backport policies govern the v1 line? Engineering Enablement
  must decide these in an accepted non-product RFC with product-manager approval
  before `0.9.0` activation.

These are deliberately assigned to later product RFCs and evidence gates. They
do not change the decision that each must be resolved and exercised before the
release that consumes it.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Connector Experience perspective | Connector Experience | Approved | Focused re-review confirmed that the stable spec identifier/version relationship now has a named pre-`0.8.0` product RFC and that `0.8.0` must implement every declaration retained for the candidate | Initial objections resolved; retain compiler, fixture, package-lifecycle, independent-author, and migration oracles as delivery evidence |
| Query Experience perspective | Query Experience | Approved | Focused sponsor re-review confirmed Query accountability for the DuckDB-user v1 outcome, the pre-`0.8.0` SQL/naming/migration RFC, latest-stable and passing-row constraints, private native implementation, and callback containment | RFC review and propagation exit satisfied; SQL, matrix, callback/lifecycle, and preview-migration delivery exits remain open by design |
| Remote Runtime perspective | Remote Runtime | Approved | The proposal preserves least authority, bounds the stable runtime subset, distinguishes extension distribution from local package loading, and makes GraphQL and reload/lifecycle behavior pass evidence gates before freeze | No objection; broader protocol, package-policy, cancellation, reload, and shutdown oracles remain delivery evidence |
| Relational Semantics perspective | Relational Semantics | Approved | The revised `0.6.0` law matrix, conservative fallback, exclusive semantic ownership, and pre-freeze proof obligations preserve DuckDB meaning without requiring every remote optimization | No objection; general classification, composition, projection closure, ordering/limit, and property oracles remain delivery evidence |
| Engineering Enablement perspective | Engineering Enablement | Approved | Focused final re-review confirmed Query-sponsored product accountability, admitted-inventory scope, explicit RFC 0004 supersession, dependency-ordered proof, and coherent propagation | RFC change is complete; facilitation remains open until omissions and incompatible drift fail closed and every domain maintains its inventory entries without Enablement approval |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Product manager approved the native-v1 profile, public
  SemVer inventory categories, narrow stable package boundary, and revised
  release direction on 2026-07-19. Support-window, support-removal,
  deprecation, security-response, maintenance, and backport choices remain
  reserved for the explicitly gated follow-on RFC.
- **Rationale:** Accept. The permanent native source and current product oracles
  provide a proven implementation lineage, while no portable Rust/stable-C-API
  product exists to justify a hidden v1 replatform. The revised progression
  makes semantics, user-visible protocol reuse, the complete candidate package
  subset, public-author breadth, and compatibility rehearsal establish their
  dependencies in order. Every required charter approves the final proposal.
- **Material objections:** Connector Experience objected that the stable
  connector-spec identity/version relationship had no owner and that `0.9.0`
  could first implement retained author syntax while freezing it. The RFC now
  requires an accepted pre-`0.8.0` Connector Experience product RFC and complete
  implementation of the candidate subset in `0.8.0`; focused re-review
  approved. Engineering Enablement objected that product approval did not cover
  a support-row rule and that the remaining v1 support policies and inventory
  gate had no owned prerequisite. The RFC now limits recorded approval to the
  approved inventory, assigns the support policies to a PM-approved pre-`0.9.0`
  RFC, and assigns the inventory gate before `0.8.0`; focused re-review approved.
  Independent adversarial review additionally found that the initial draft
  over-scoped SemVer to design examples, left RFC 0004's obsolete `0.2.0`
  publication timing and latest-stable constraint ambiguous, misclassified the
  v1 product choice as an Enablement objective, left inactive-draft migration
  unclear, used Rust-specific exception wording, and omitted failed initial-load
  atomicity from the roadmap oracle. The final decision narrows SemVer to the
  admitted inventory, supersedes RFC 0004 while carrying its durable choices
  forward, makes Query Experience the Product RFC sponsor, rejects the inactive
  draft explicitly, and propagates native containment and all-or-nothing load
  evidence. Focused affected-team and adversarial re-review approved the fixes.
  No unresolved objection remains.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Complete `0.6.0` semantic trust | Query Experience | Relational Semantics — Collaboration then X-as-a-Service; Connector Experience and Remote Runtime for declaration/execution boundaries; Engineering Enablement for bounded facilitation | RFC 0009 accepted and revised `0.6.0` goal shaped and approved |
| Deliver `0.7.0` reusable protocol product path | Query Experience | Connector Experience and Remote Runtime — Collaboration then X-as-a-Service; Relational Semantics for the shared oracle | `0.6.0` released and the GraphQL-or-narrowing product decision accepted |
| Establish the canonical public inventory and change-classification gate | Engineering Enablement | Every domain team — Facilitation to assign and transfer maintenance of its inventory entries | RFC 0009 accepted; must be complete before `0.8.0` activation, with domain self-sufficiency required before `0.9.0` |
| Decide the stable connector-spec identity and version relationship | Connector Experience | Query Experience, Remote Runtime, and Relational Semantics — Collaboration on consumer, execution, and semantic compatibility; Engineering Enablement — Facilitation for migration evidence | RFC 0009 accepted; must be Accepted before `0.8.0` activation |
| Deliver `0.8.0` local package lifecycle | Connector Experience | Query Experience and Remote Runtime — Collaboration; Relational Semantics as X-as-a-Service; Engineering Enablement for inventory-gate transfer | `0.7.0` released; SQL/naming and connector-spec version RFCs accepted; every proposed v1 declaration assigned to `0.8.0` evidence |
| Decide v1 release and support policy | Engineering Enablement | Connector Experience, Query Experience, and Remote Runtime as affected public-surface owners; Relational Semantics for compatibility oracles | RFC 0009 accepted; must be Accepted with product approval before `0.9.0` activation |
| Deliver `0.9.0` public authoring and API candidate | Connector Experience | All affected teams through their established service and compatibility oracles | `0.8.0` released; release-and-support-policy RFC accepted; release-governance inventory gates independently maintained |
