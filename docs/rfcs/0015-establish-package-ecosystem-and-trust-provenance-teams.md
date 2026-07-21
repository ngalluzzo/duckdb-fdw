# RFC 0015: Establish Package Ecosystem and Trust & Provenance teams

```yaml
rfc: "0015"
title: "Establish Package Ecosystem and Trust & Provenance teams"
status: "Accepted"
rfc_type: "Non-product"
sponsor_team: "Engineering Enablement"
technical_decision_owner: "Lead agent"
product_approver: "Nic Galluzzo"
authors:
  - "Lead agent"
required_reviewers:
  - "Connector Experience"
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
affected_teams:
  - "Connector Experience"
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
linked_outcome_or_objective: "Topology evolution ahead of the composition/registry backlog (issues #23-#28, #31-#40): separate ecosystem-operation and trust/provenance cognitive load from Connector Experience's authoring charter before that backlog activates, per the evolution signals in docs/TEAM_TOPOLOGY.md."
supersedes: "Not applicable"
```

## Summary

Establish a fourth stream-aligned team, **Package Ecosystem**, accountable for
the value stream from a validated connector package to governed ecosystem
availability (artifact storage, dependency resolution and lockfiles, registry
namespaces and publisher identity, discovery, release lifecycle, and
operations), and a new complicated-subsystem team, **Trust & Provenance**,
accountable for signing, provenance verification, and the registry's
adversarial threat corpus. Reassign the currently Connector-Experience-labeled
composition and registry backlog (issues #23-#28 and #31-#40) to these two new
teams. No SQL, connector-package, or runtime behavior changes; this is a
topology and accountability decision only.

## Sponsorship and context

- **RFC type:** Non-product. This decision does not itself change a connector
  author's or DuckDB user's observable behavior; it establishes which team is
  accountable when that behavior is later built. Per
  `docs/teams/ENGINEERING_ENABLEMENT.md`'s explicit charge to "sponsor
  non-product RFCs for cross-team delivery and governance objectives," and
  because no single existing stream team owns the topology document itself,
  Engineering Enablement sponsors.
- **Sponsoring team:** Engineering Enablement, as a facilitation and
  governance objective, not a product outcome. Engineering Enablement assumes
  no product accountability for Package Ecosystem or Trust & Provenance by
  sponsoring this RFC; both charters, once accepted, own their objectives
  directly.
- **Linked outcome or objective:** Fix the accountable-team routing for the
  composition and registry backlog before it activates, so that public-service
  operation, security, privacy, and cost decisions land with a team whose
  charter names them, rather than by default inheritance into an authoring
  charter that never claimed them.
- **Why now:** `docs/TEAM_TOPOLOGY.md`'s evolution signals include "a stream
  team carries persistent cognitive load outside its customer outcome" and "a
  distinct customer and acceptance narrative reveal a new value stream." Both
  are independently verifiable today, in the issue text itself, not merely
  anticipated (see Evidence). Deciding this before issue #23 begins avoids a
  disruptive mid-backlog re-routing later, and the product manager has asked
  for the process to start now rather than wait.

## Problem

Every issue in the composition and registry sequence (#23-#28, #31-#40) is
currently labeled `team/connector-experience` and `stream/api-to-trusted-connector`,
inheriting Connector Experience's accountability by default. Reading the issue
text directly (not just the labels) shows this default has already produced a
concrete overcommitment:

- Issue #40's own guardrails state: *"Must: Define sustainable Connector
  Experience ownership for registry operation, support, security response,
  privacy, data retention, moderation, and operating-cost boundaries before
  ordinary-user guidance."* Its exit condition for Engineering Enablement's
  facilitation reads: *"Connector Experience independently operates the public
  registry and owns ongoing service quality."* This assigns permanent
  operation of a public network service — moderation, incident response,
  privacy, retention, availability, cost control — to a charter
  (`docs/teams/CONNECTOR_EXPERIENCE.md`) whose actual mission and
  responsibilities text never claims any of that; it is scoped to "connector
  packages... declarative, understandable, safely configurable,
  deterministically testable, and diagnosable."
- Issue #31 independently defines "four separate planes: immutable content
  transport, catalog and namespaces, trust and provenance, and lifecycle
  governance" and an explicit adversarial threat corpus — "artifact
  substitution, mutable-version overwrite, namespace squatting, dependency
  confusion, typosquatting, compromised publishers, stale or malicious
  mirrors, metadata rollback/freeze/mix-and-match, key compromise, ownership
  transfer, equivocation, and registry recovery." That is a materially
  different cognitive domain — adversarial, cryptographic, conservative-by-
  default reasoning — from package authoring, schema validation, and
  compilation diagnostics.
- Issues #29 and #30 are already labeled `team/query-experience` with
  `interaction/connector-experience:x-as-a-service`, meaning the *existing*
  plan already treats "produce a compiled composition candidate" as a service
  Query Experience *consumes*, not something Query Experience or Connector
  Experience should each partially own. There is no team positioned to be the
  clean *producer* of that service today; it defaults to whichever team is
  asked to build #23-#28, which is currently Connector Experience by label
  only.

Concrete scenario: if #23 begins today under its current label, Connector
Experience inherits accountability for a reproducible package-graph contract,
and by the time #40 is reached, for operating a public registry — moderation
queue, incident response, and cost envelope included — despite its charter's
"Explicit non-responsibilities" section never having been amended to accept
any of it. The mismatch would surface as either silent scope creep (the
charter never catches up to what the team is actually doing) or a disruptive
re-routing mid-sequence (worse than deciding now, before any of #23-#40 has
started).

## Decision drivers and invariants

- **Must preserve:** every accepted RFC (0009, 0012, 0013, 0014) and their
  interfaces unchanged; `docs/TEAM_TOPOLOGY.md`'s operating principles (one
  accountable team per goal, platform capabilities as internal products with
  named consumers, isolate a complicated subsystem only on cognitive-load
  evidence, enabling teams transfer capability rather than accumulate
  ownership); `AGENTS.md`'s escalation and reserved-decision list.
- **Must enable:** a named accountable team for public-registry operation,
  security response, privacy, retention, moderation, and cost before that
  backlog activates; a clean, low-coupling handoff shape for
  artifact → release → lock → compiled composition candidate → activation;
  isolation of adversarial trust/threat-model reasoning from general
  distribution-service mechanics, if the evidence for that isolation is
  strong enough now (see Alternatives and Unresolved questions).
- **Must not introduce:** any change to current public SQL, connector-package,
  or compatibility behavior; any registry, signing, or distribution
  *implementation* (all of #23-#40 remains unimplemented backlog, each still
  gated by its own required RFC per its "Contract and governance" section);
  a permanent Engineering Enablement operating role for either new team.

## Proposed decision

Create two new topology entries in `docs/TEAM_TOPOLOGY.md` and two new active
charters, and reassign the accountable-team labels on issues #23-#28 and
#31-#40. No other repository behavior changes.

### New value stream

**Trusted connector package to governed ecosystem availability.** Customers:
connector publishers, package consumers, and registry operators. Flow:

```text
validated connector package
  -> immutable artifact
  -> published release
  -> trusted catalog state
  -> discovery and resolution
  -> canonical lock
  -> compiled composition candidate
```

Value: a package can be safely published, discovered, acquired, verified,
composed, reproduced, governed, and recovered without weakening its semantics
or granting remote registry state execution authority. This stream begins
where Connector Experience's stream currently ends (a validated package) and
stops before Query Experience's stream activates the package graph.

### Package Ecosystem (new stream-aligned team)

**Mission:** serve connector publishers, package consumers, and registry
operators by making a validated package safely publishable, discoverable,
acquirable, verifiable, composable, reproducible, governable, and recoverable.

**Responsibilities:** immutable package artifacts; composition manifests and
graph identity; dependency resolution and lockfiles; offline restore and
compiled composition candidates; registry coordinates, namespaces, and
publisher identities; the OCI distribution profile; acquisition semantics;
release records and release-lifecycle governance (retention, deletion,
yanking, deprecation, advisories); discovery metadata and search; mirrors,
bundles, and registry operations, including moderation workflow, availability,
privacy, support, and cost control named in issue #40.

**Explicit non-responsibilities:** connector language and package compilation
semantics (Connector Experience); DuckDB catalog activation and SQL lifecycle
(Query Experience); relational pushdown proof (Relational Semantics); generic
transport implementation (Remote Runtime); signing, provenance verification,
and the adversarial threat corpus (Trust & Provenance, below); permanent
enabling or security-review ownership (Engineering Enablement facilitates,
then exits).

**Reassigned issues:** #23-#28 (local composition and reproducibility),
#31-#39 (central registry planes other than signing/threat-model execution,
see Trust & Provenance).

### Trust & Provenance (new complicated-subsystem team)

**Mission:** supply Package Ecosystem with conservative-by-default trust
evaluation — signing, provenance attestation, trust-root management, and the
adversarial threat corpus — kept separate because verifying trust demands
specialist, adversarial, security-first reasoning distinct from distribution
and catalog mechanics, in the same way Relational Semantics keeps conservative
relational proof separate from Connector and Query's own reasoning.

**Responsibilities:** signing-scheme choice and verification; publisher
identity binding; trust-root distribution and rotation; provenance attestation
format and verification; the revocation/advisory/quarantine state machine and
its safe-default semantics; the adversarial threat corpus named in issue #31
(artifact substitution, mutable-version overwrite, namespace squatting,
dependency confusion, typosquatting, compromised publishers, malicious
mirrors, metadata rollback/freeze/mix-and-match, key compromise, ownership
transfer, equivocation, registry recovery) and its executable oracles.

**Explicit non-responsibilities:** artifact storage, transport, and OCI
distribution mechanics; namespace CRUD and discovery/search; the
dependency-resolution algorithm; moderation *process* or human workflow
(Trust & Provenance supplies the revocation/advisory primitives moderation
acts through, not the moderation workflow itself); mirrors, disaster recovery,
and registry operating cost.

**Sole customer:** Package Ecosystem. Trust & Provenance does not interact
directly with Query Experience, Connector Experience, or Remote Runtime;
Package Ecosystem resolves trust state upstream of the compiled composition
candidate it hands to Query Experience, keeping that seam low-coupling and
matching how Relational Semantics interfaces with exactly the teams that need
conservative relational proof.

**Reassigned scope:** the signing, provenance, and threat-model-execution
portion of #31 and #40, split out of Package Ecosystem's registry ownership.

### Public behavior

Not affected. No SQL, connector-package, compatibility, or diagnostic behavior
changes. `connectors/github`, `connectors/rickandmorty`, the `duckdb_api/v1`
contract, and RFCs 0009-0014 are unchanged.

### Shared interfaces

No new C++ type or runtime interface exists yet — none of #23-#40 is
implemented. This RFC fixes *who* will define and own each future interface
when that backlog activates, each still gated by its own required RFC:

- **Connector Experience -> Package Ecosystem (X-as-a-Service):** validated
  package, immutable semantic identity, compiled structural metadata, package
  fixtures and validation evidence, compatibility descriptor. This is the same
  artifact Connector Experience already produces today (`CompiledConnector`
  and its registration view); only the downstream consumer changes.
- **Package Ecosystem -> Query Experience (X-as-a-Service):** exact compiled
  composition candidate, graph and lock identity, ordered package
  generations, dependency edges, complete generated SQL inventory, lifecycle
  eligibility already enforced, safe explanation. This retargets the producer
  issues #29-#30 already expect Query Experience to consume from (labeled
  `interaction/connector-experience:x-as-a-service` today) to Package
  Ecosystem; the shape Query Experience depends on is unchanged.
- **Remote Runtime -> Package Ecosystem (Collaboration until issue #33 proves
  the boundary, then X-as-a-Service):** bounded authenticated registry
  transfer, credential scoping, destination and redirect enforcement,
  cancellation, request/byte/time/temporary-resource limits. A new consumer
  of Remote Runtime's existing bounded-transport pattern; no change to
  Runtime's own responsibilities.
- **Package Ecosystem <-> Trust & Provenance (Collaboration until proven,
  then X-as-a-Service):** Package Ecosystem supplies a candidate artifact
  digest, claimed provenance, and namespace/publisher coordinate; Trust &
  Provenance returns a conservative-by-default trust-state verdict and
  provenance-verification result. An unknown or ambiguous trust state must
  never be treated as trusted.
- **Engineering Enablement -> Package Ecosystem and -> Trust & Provenance
  (Facilitation, temporary):** initial supply-chain testing, identity
  operations, disaster-recovery, compatibility, and launch-gate practices,
  transferred out once each team demonstrates independent use, per
  `docs/teams/ENGINEERING_ENABLEMENT.md`'s exit principle. Issue #40's
  current text ("Connector Experience independently operates the public
  registry") is superseded by this RFC to read Package Ecosystem in place of
  Connector Experience.

### Operational behavior

Not affected today: no registry, signing, or public service exists. The
operational consequence of this RFC is naming which team will own future
security, privacy, moderation, availability, and cost decisions for that
future service (Package Ecosystem and Trust & Provenance), rather than those
decisions defaulting into an authoring charter that never claimed them.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Package Ecosystem (new) | Accountable stream team for the new value stream | New charter `docs/teams/PACKAGE_ECOSYSTEM.md`; receives #23-#28, #31-#39 | Producer to Query Experience, consumer of Connector Experience and Remote Runtime, consumer of Trust & Provenance | First registry-plane RFC (successor to #31) accepted and its own goals can activate |
| Trust & Provenance (new) | Accountable complicated-subsystem team | New charter `docs/teams/TRUST_AND_PROVENANCE.md`; receives the signing/threat-model portion of #31/#40 | X-as-a-Service to Package Ecosystem only | Package Ecosystem consumes trust verdicts without re-deriving threat-model logic itself |
| Connector Experience | Cedes accountability for #23-#28 and #31-#40 | No charter text changes (its current charter never claimed this scope); loses those labels; its `CompiledConnector`/registration output gains a second consumer (Package Ecosystem) alongside Query Experience, Relational Semantics, and Remote Runtime | Unaffected X-as-a-Service, new consumer | Package Ecosystem consumes validated packages without Connector Experience coordination on registry matters |
| Query Experience | Retargets its upstream composition-candidate dependency | No charter text changes; #29-#30's producer becomes Package Ecosystem instead of Connector Experience | X-as-a-Service (consumer) | Query Experience implements #29-#30 against Package Ecosystem's service without assuming Connector Experience internals |
| Remote Runtime | Gains one more real consumer | No charter text changes; strengthens existing platform justification (another named stream consumer, per `docs/TEAM_TOPOLOGY.md`'s "not justified without a named stream consumer" principle) | Collaboration then X-as-a-Service | Issue #33's boundary is proven and Package Ecosystem uses Runtime's transport interface without bespoke coordination |
| Relational Semantics | Not affected | No charter text changes; package composition and registry distribution grant no relational authority | None | Not applicable |
| Engineering Enablement | Sponsors this RFC; facilitates both new teams' initial practices | No charter text changes; gains two new temporary facilitation relationships | Facilitation | Each new team demonstrates independent use of the transferred practice |

Cognitive load moves from Connector Experience (which never formally accepted
it) to two new named teams whose charters state it explicitly. No cognitive
load moves onto Query Experience, Remote Runtime, or Relational Semantics
beyond a new named service dependency each already anticipated in issue
labels or the platform-justification principle.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** Not affected. Package
  composition and registry distribution grant no relational authority;
  Relational Semantics remains focused on `ScanRequest -> ScanPlan`.
- **Authentication, credentials, network policy, and privacy:** Directly
  relevant to why this split exists. Issue #31's own guardrails already
  require registry credentials to stay separate from connector API
  credentials and registry coordinates to stay separate from connector IDs;
  this RFC does not change those requirements, it names the team accountable
  for enforcing them (Package Ecosystem) and the team accountable for the
  trust-state reasoning behind them (Trust & Provenance) instead of leaving
  them implicit.
- **Resource budgets, backpressure, and cancellation:** Not affected today.
  Remote Runtime's existing bounded-transport pattern is expected to extend
  unchanged to a new consumer; no new resource model is decided by this RFC.
- **Replay, retries, caching, and duplicate prevention:** Not affected;
  deferred to #33's and #40's own required RFCs.
- **Concurrency, immutability, and state ownership:** Not affected. Today's
  `CompiledConnector` and package-generation immutability model is preserved
  unchanged as the exact interface Package Ecosystem will consume.
- **FFI, initialization, reload, shutdown, and failure containment:** Not
  affected.
- **Diagnostics, redaction, metrics, and progress reporting:** Not affected
  today; future registry diagnostics will follow the same redaction
  discipline established by `docs/CONNECTOR_SPECIFICATIONS.md` and
  `docs/RUNTIME_CONTRACTS.md`, enforced by whichever future RFC defines them.

## Compatibility and migration

No existing users, connector packages, or stored data are affected — no
implementation exists for #23-#40 today. Migration is confined to:

- relabeling issues #23-#28 and #31-#40 from `team/connector-experience` to
  `team/package-ecosystem` (and the signing/threat-model portion of #31/#40 to
  `team/trust-and-provenance`), and updating each issue's "Topology" section's
  "Accountable team" line and Engineering Enablement exit-condition text where
  it currently names Connector Experience;
- adding a new `stream/trusted-package-to-ecosystem-availability` label;
- creating `docs/teams/PACKAGE_ECOSYSTEM.md` and
  `docs/teams/TRUST_AND_PROVENANCE.md`;
- updating `docs/TEAM_TOPOLOGY.md`'s value-streams section, initial-shape
  diagram and table, team-interfaces table, and charter-ownership list.

`docs/teams/CONNECTOR_EXPERIENCE.md` needs no text changes: its current
mission, responsibilities, and non-responsibilities never claimed registry or
ecosystem-operation scope, so nothing in it is inconsistent with this RFC.
Rollback is limited to reverting the label and documentation changes; no code
or stored state depends on this decision.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Connector Experience's issue set already commits to permanent public-service operation outside authoring | Verbatim issue text | `gh issue view 40 --json body` | Confirmed: "Must: Define sustainable Connector Experience ownership for registry operation, support, security response, privacy, data retention, moderation, and operating-cost boundaries" and exit condition "Connector Experience independently operates the public registry and owns ongoing service quality." |
| Issue #31 already separates registry concerns into distinguishable planes including trust/threat-modeling | Verbatim issue text | `gh issue view 31 --json body` | Confirmed: "four separate planes: immutable content transport, catalog and namespaces, trust and provenance, and lifecycle governance," plus a twelve-item adversarial threat corpus. |
| Query Experience already expects to consume a composition candidate as a service rather than co-own its production | Issue labels | `gh issue view 29,30 --json labels` | Confirmed: both carry `team/query-experience` and `interaction/connector-experience:x-as-a-service`. |
| Trust & Provenance's cognitive load is large enough to isolate now, not only after Package Ecosystem itself reports the signal | Verbatim issue text describing the threat corpus | `gh issue view 31 --json body` | Confirmed as *scoped, textual* evidence (the corpus is explicit and itemized), but it is **anticipatory**: no team has yet operated a registry and reported this load first-hand, unlike the Package Ecosystem evidence above, which cites an existing over-assignment. Flagged for reviewers in Alternatives and Unresolved questions. |

## Alternatives considered

1. **Do nothing now; wait until Connector Experience begins #23 and reports
   the load.** Benefit: strictly evidence-driven, no anticipatory structure.
   Drawback: the evolution-signal evidence already exists in issue text
   today; waiting risks the backlog beginning under the wrong accountable
   team and a disruptive re-routing mid-sequence. Not selected — the product
   manager has also asked to start the process now.
2. **Split only the registry issues (#31-#40) into Package Ecosystem; leave
   composition (#23-#28) with Connector Experience.** Drawback: breaks the
   natural artifact -> lock -> compiled-candidate flow into two owners for no
   customer-facing reason, and contradicts #29-#30's already-recorded
   expectation of one upstream composition-candidate producer. Not selected.
3. **Fold registry/ecosystem work into Remote Runtime instead of a new
   stream-aligned team.** Drawback: Remote Runtime's customers are other
   stream-aligned teams, not external publishers, consumers, or registry
   operators; modeling an externally customer-facing domain as an internal
   platform obscures product accountability — the exact "Registry Platform"
   naming trap this RFC avoids. Not selected.
4. **Establish Package Ecosystem now; defer Trust & Provenance until Package
   Ecosystem itself reports the same evolution signals.** Benefit: strictly
   evidence-driven for the one piece whose evidence is anticipatory rather
   than observed; matches `docs/TEAM_TOPOLOGY.md`'s caution against forming
   subsystems around a merely "technically important" area. Drawback:
   revisiting mid-backlog if the threat-corpus load turns out as large as
   issue #31 already describes it, which would repeat exactly the disruption
   this RFC exists to avoid for Package Ecosystem. **Presented to reviewers
   as an open, decision-critical question, not foreclosed** — see Unresolved
   questions.
5. **Retain the current behavior (no topology change).** Rejected for the
   reasons in Problem: the mismatch between issue #40's text and Connector
   Experience's charter is already present, not hypothetical.

## Drawbacks and failure modes

- Two charters would exist with no staffed work and no code yet. Mitigation:
  charters activate accountability for future goals only; an inactive charter
  carries no cost, the same as any team charter between goals.
- Query Experience's #29-#30 upstream dependency changes producer identity
  before either goal is implemented. Low risk since neither is built, but
  requires Query Experience's review to confirm no baked-in assumption in
  either goal brief breaks.
- If Trust & Provenance is created now and the anticipated load does not
  materialize as severe, Package Ecosystem coordinates with an underused
  subsystem team. Mitigation: the charter can note this isolation is
  provisional, subject to the same evolution-signal review once the first
  real registry RFC (successor to #31) is drafted.
- Engineering Enablement, as sponsor, must not let facilitation become
  permanent operation for either new team; this repeats the exact risk this
  RFC is correcting for Connector Experience if not held to its stated exit
  conditions.

## Acceptance and verification

- **End-to-end demonstration:** Not applicable to a topology decision. The
  demonstration is a coherent, cross-referenced set of updated documents
  (`docs/TEAM_TOPOLOGY.md`, two new charters) and relabeled issues #23-#28
  and #31-#40, committed together per `docs/RFC_PROCESS.md`'s propagation
  rule.
- **Automated oracle:** Not applicable; no code path is affected.
- **Quality gates:** Repository documentation and whitespace checks; no
  behavioral gate applies.
- **Independent review:** `$topology-consult` review from all five existing
  teams, recorded in the Review record below.
- **Interaction exit:** Not applicable until the first goal under either new
  team activates; interaction exits are then tracked normally under that
  goal.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Not affected | Describes runtime and relational architecture, not team topology | Not applicable |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Not affected | No package-format change | Not applicable |
| `docs/RUNTIME_CONTRACTS.md` | Not affected | No runtime-behavior change | Not applicable |
| `docs/TEAM_TOPOLOGY.md` and active charters | Affected | New value-stream section; initial-shape diagram and table; team-interfaces table rows; goal-accountability routing rule; charter-ownership list entries for `docs/teams/PACKAGE_ECOSYSTEM.md` and `docs/teams/TRUST_AND_PROVENANCE.md` | Integrated with this acceptance |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Affected (skills only) | No change to the goal template or escalation rules; `.claude/skills/topology-consult/SKILL.md` hardcodes the five-charter reading list for routing and must add the two new charters so future consultations select them correctly | Integrated with this acceptance |
| Examples, diagnostics, fixtures, and tests | Not affected | No code exists for #23-#40 today | Not applicable |
| GitHub issues #23-#28, #31-#40 | Affected | Relabel `team/connector-experience` to `team/package-ecosystem` (or `team/trust-and-provenance` for the signing/threat-model portion of #31/#40); add `stream/trusted-package-to-ecosystem-availability`; update each issue's "Accountable team" line and any Engineering Enablement exit-condition text naming Connector Experience | Integrated with this acceptance |

## Unresolved questions

- **Decision-critical, resolved by review:** should Trust & Provenance be
  created now alongside Package Ecosystem, or should the registry/threat-model
  portion of #31/#40 stay inside Package Ecosystem's initial scope until
  Package Ecosystem itself reports the cognitive-load evolution signal
  firsthand? All five reviewers independently recommended creating it now;
  see Review record and Decision and rationale below.
- Non-blocking: exact GitHub label taxonomy naming
  (`stream/trusted-package-to-ecosystem-availability` versus an alternative
  spelling) — cosmetic, resolved at propagation time.
- Non-blocking: whether Trust & Provenance eventually needs its own further
  subsystem split — a future evolution-signal question, not this RFC's to
  answer.
- Non-blocking, raised by Query Experience: when #29-#30's own delivery RFC
  is drafted, `docs/teams/QUERY_EXPERIENCE.md`'s "Team API and service
  expectations" section should gain an explicit line naming Package Ecosystem
  as the composition-candidate producer (it currently names only Connector
  Experience, Relational Semantics, and Remote Runtime).
- Non-blocking, raised by Relational Semantics: if a future composition ever
  produces relations that span package boundaries (rather than each package
  independently contributing its own generated SQL objects), Relational
  Semantics' proof obligations ("every residual predicate has exactly one
  owner," "providers preserve base-row cardinality") will need an explicit
  definition of what "provider" means at a multi-package composition
  boundary. Not created by this RFC; raise it when the successor RFC to #31
  or the #29-#30 composition-candidate contract is drafted, with Relational
  Semantics named as a required reviewer at that point.
- Non-blocking, raised by Engineering Enablement: the exit wording in both
  draft charters ("demonstrates independent use," "maintains independently")
  is qualitative, not yet falsifiable. When the follow-on goals (issue #23,
  and the #31-successor RFC) activate, their goal briefs must state concrete,
  named exit evidence (a specific drill or fixture suite the receiving team
  runs and maintains without Enablement authorship) rather than repeating the
  charter phrase verbatim.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Connector Experience perspective | Connector Experience | Approved | Charter's own Responsibilities/Explicit non-responsibilities never claimed registry scope; issue #40/#31 quotes independently verified verbatim via `gh issue view`; the new Connector Experience -> Package Ecosystem interface names a fourth consumer of output already produced, adding no new production obligation. Recommended creating Trust & Provenance now, citing Connector Experience's own mislabeling as the cautionary precedent for waiting. | Accepted as dispositive; no action required. |
| Query Experience perspective | Query Experience | Approved | Verified via `gh issue view 29,30` that both issues already carry `team/query-experience` + `interaction/connector-experience:x-as-a-service` with the identical composition-candidate shape this RFC promises from Package Ecosystem; the "no registry lookups or trust evaluation during activation" line restates an invariant Query Experience's charter already holds ("bind and planning remain deterministic and free of network I/O"), not a new constraint. Raised one non-blocking future-propagation item (see Unresolved questions). Recommended creating Trust & Provenance now as cheap insurance against trust-adjacent shortcuts leaking toward activation later. | Accepted; non-blocking item carried to Unresolved questions for the #29-#30 delivery RFC. |
| Remote Runtime perspective | Remote Runtime | Approved | Registry/OCI transfer maps onto Remote Runtime's existing "add a protocol family" interaction pattern already recorded in `docs/TEAM_TOPOLOGY.md`; issue #33 (verified via `gh issue view`) asks for bounded transport, credential scoping, and destination/redirect enforcement, matching Runtime's existing charter language, with mirror selection and trust decisions correctly left to Package Ecosystem and Trust & Provenance. Interaction exit is Open (deferred to #33's own delivery), which the RFC already states. Leaned toward creating Trust & Provenance now but noted low confidence outside Runtime's own interface stake. | Accepted; exit condition's Open status matches the RFC's own framing, no change needed. |
| Relational Semantics perspective | Relational Semantics | Approved | Confirmed directly that none of Package Ecosystem's or Trust & Provenance's responsibilities touch pushdown, residual ownership, ordering, limits, or cardinality; the RFC's "Not affected" self-assessment holds. Raised one non-blocking forward-looking item about multi-package relational composition (see Unresolved questions) and confirmed Trust & Provenance's "conservative-by-default" language does not duplicate or conflict with Relational Semantics' own conservative-fallback authority. Recommended creating Trust & Provenance now by direct analogy to its own formation as a complicated subsystem. | Accepted; non-blocking item carried to Unresolved questions for the #31-successor or #29-#30 contract RFC. |
| Engineering Enablement perspective | Engineering Enablement | Approved | As sponsor, evaluated critically rather than rubber-stamping: confirmed the RFC *reduces* Engineering Enablement's committed facilitation surface relative to issue #40's current text (five named practices to Package Ecosystem instead of thirteen, plus a separately scoped engagement with Trust & Provenance) rather than expanding it. Flagged that exit-condition wording needs concrete, named evidence at future goal-activation time rather than qualitative phrasing (see Unresolved questions) — non-blocking for this RFC. Recommended creating Trust & Provenance now: a single undifferentiated engagement spanning both adversarial/cryptographic and general distribution-ops practice would itself be a worse cognitive-load shape for Engineering Enablement to facilitate. | Accepted; non-blocking item carried to Unresolved questions for future goal briefs. |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Accepted 2026-07-21 — Nic Galluzzo approved starting
  this RFC process and explicitly directed proposing Trust & Provenance now
  rather than deferring it, "then see what comes out of the RFC discussions,"
  then reviewed the completed review record (all five teams approved, no
  objections) and confirmed acceptance.
- **Rationale:** All five required reviewers approved without objection.
  Two independently verifiable factual claims (issue #40's and #31's
  guardrail/exit-condition text; issues #29-#30's existing
  `interaction/connector-experience:x-as-a-service` labels) were spot-checked
  directly by at least three reviewers via `gh issue view` and confirmed
  accurate in every case. On the one decision-critical open question — timing
  of Trust & Provenance — all five reviewers independently recommended
  creating it now, each from their own charter's reasoning: Connector
  Experience (its own mislabeling is the precedent for not waiting), Query
  Experience (insurance against trust-adjacent shortcuts reaching its
  activation boundary), Remote Runtime (issue #31's itemized corpus is
  unusually concrete anticipatory evidence), Relational Semantics (direct
  analogy to its own formation as an isolated complicated subsystem), and
  Engineering Enablement (a merged engagement would be a worse facilitation
  shape than two narrowly scoped ones). No reviewer's charter authority
  compelled a different answer, and the RFC's own mitigation (revisit at the
  first real registry RFC if the load does not materialize) bounds the
  downside. The decision owner adopts this convergent recommendation:
  **Trust & Provenance is created now, alongside Package Ecosystem.**
- **Material objections:** None recorded. Five non-blocking items were
  raised and carried to Unresolved questions rather than requiring
  disposition here, since none identifies a violated contract, invariant,
  unacceptable product consequence, or operational hazard in this RFC's own
  scope — each is a concrete action deferred to a specifically named future
  goal or RFC.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Reproducible package graph contract and corpus (issue #23 and the #24-#28 sequence) | Package Ecosystem | Remote Runtime (Collaboration), Engineering Enablement (Facilitation), Query Experience (Collaboration on the composition-candidate interface) | This RFC accepted; issue's own required RFC accepted; product approval per that issue |
| Central registry contract and threat model (issue #31 and the #32-#39 sequence) | Package Ecosystem, with Trust & Provenance accountable for the signing/threat-model slice if created by this RFC | Remote Runtime (Collaboration then X-as-a-Service), Engineering Enablement (Facilitation) | This RFC accepted; issue's own required RFC accepted; product approval per that issue |
| Public connector registry ecosystem certification (issue #40) | Package Ecosystem | Query Experience (X-as-a-Service consumer), Remote Runtime (X-as-a-Service), Trust & Provenance (X-as-a-Service), Engineering Enablement (Facilitation) | This RFC accepted; issue's own required RFC accepted; product approval per that issue |
