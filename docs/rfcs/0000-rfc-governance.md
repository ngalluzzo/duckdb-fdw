# RFC 0000: Adopt cross-team RFC governance

```yaml
rfc: "0000"
title: "Adopt cross-team RFC governance"
status: "Accepted"
rfc_type: "Non-product"
sponsor_team: "Engineering Enablement"
technical_decision_owner: "Lead agent"
product_approver: "Not required; no product-policy choice"
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
linked_outcome_or_objective: "Make durable cross-team technical decisions reviewable, auditable, and enforceable in agent-led delivery"
supersedes: "none"
```

## Summary

Adopt `docs/RFC_PROCESS.md` and `docs/RFC_TEMPLATE.md` as the mechanism for
durable decisions that affect public behavior, shared interfaces, multiple
teams, or difficult-to-reverse commitments, and enforce the decision gate in
the product-goal and delivery workflows.

## Sponsorship and context

- **RFC type:** Non-product.
- **Sponsoring team:** Engineering Enablement.
- **Linked outcome or objective:** Make durable cross-team technical decisions
  reviewable, auditable, and enforceable in agent-led delivery.
- **Why now:** The project has established product delivery and team topology
  but lacks an explicit way to qualify, review, decide, and propagate shared
  technical commitments before implementation makes them implicitly.

This operating decision affects every topology team. It does not alter a
customer-facing product outcome or reserve a product-policy choice.

## Problem

`AGENTS.md` assigns technical ownership to the lead agent and the topology
defines team accountabilities, but neither previously defined which decisions
need cross-team proposal review, how objections are resolved, or what artifact
records the result. A public contract or shared interface could therefore be
established by an implementation commit without a discoverable decision,
affected-team review, or required propagation.

## Decision drivers and invariants

- **Must preserve:** Product-manager authority, lead-agent technical ownership,
  engineering invariants, and each topology team's accountability.
- **Must enable:** Proportionate review of durable decisions, bounded trials for
  unknowns, explicit decision authority, and auditable propagation.
- **Must not introduce:** A universal approval queue, architecture hidden only
  in RFC history, fabricated product outcomes, or a route around existing
  safety and correctness contracts.

## Proposed decision

Adopt the process and template in this change. Mandatory triggers take
precedence over the diagnostic and exemptions. Product RFCs retain a stream
outcome; non-product RFCs use the owning topology team and a real operating
objective. The lead agent is the default technical decision owner, while
product-manager-reserved choices still require a recorded product decision.

Required reviewers record approval or objection and evidence. Accepted
decisions are propagated into authoritative contracts before delivery closes;
the RFC remains the rationale rather than the sole source of current behavior.

### Public behavior

Not affected. This RFC changes the project operating model, not SQL, connector
packages, diagnostics, compatibility, or exclusions.

### Shared interfaces

The team review and decision path becomes a shared organizational interface.
Technical interfaces such as `CompiledConnector`, `ScanRequest`, `ScanPlan`,
and `BatchStream` are unchanged, but later changes to them become mandatory RFC
triggers.

### Operational behavior

The goal-drafting and delivery skills must qualify RFC triggers and stop at an
unaccepted decision boundary. Urgent security or correctness containment stays
possible through a temporary, scoped, auditable exception with an activated
resolution goal.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Engineering Enablement | Sponsor and process facilitator | Maintains reusable RFC guidance and workflow enforcement without deciding proposal content | Facilitation | Every team can apply the trigger, review record, and propagation rules from the durable docs and skills |
| Connector Experience | Affected stream team | Reviews connector-author and package decisions | Collaboration | Review obligations and product sponsorship remain consistent with its outcome boundary |
| Query Experience | Affected stream team | Reviews DuckDB user and adapter decisions | Collaboration | Review obligations and product sponsorship remain consistent with its outcome boundary |
| Remote Runtime | Affected platform team | Reviews shared execution and operational decisions | Collaboration | Platform decisions have explicit sponsorship, consumers, and authority |
| Relational Semantics | Affected complicated-subsystem team | Reviews relational and correctness decisions | Collaboration | Semantic objections remain evidence-backed and cannot weaken invariants |

No product accountability moves. The process adds a review protocol across the
existing boundaries and ends bootstrap collaboration once the operating docs,
skills, and validation agree.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** No semantic rule changes;
  later changes to these rules require an RFC and Relational Semantics review.
- **Authentication, credentials, network policy, and privacy:** No policy
  changes; decisions in these areas become mandatory triggers.
- **Resource budgets, backpressure, and cancellation:** No runtime changes;
  durable changes become mandatory triggers.
- **Replay units, retries, caching, and duplicate prevention:** No behavior
  changes; durable changes become mandatory triggers.
- **Concurrency, immutability, and state ownership:** No implementation change;
  durable concurrency commitments become mandatory triggers.
- **FFI, initialization, reload, shutdown, and failure containment:** No
  lifecycle change; the process records an explicit urgent-containment path.
- **Diagnostics, redaction, metrics, and progress reporting:** No product
  diagnostic changes. Review status and decisions are recorded in RFC files.

## Compatibility and migration

No user migration is required. Existing operating documents and skills are
updated coherently in the adoption commit. Future qualifying work must either
reference an accepted RFC, stay within a documented exemption, or remain a
bounded evidence trial that does not establish the disputed contract.

Rollback requires reverting the process, template, integrations, and this
decision together so the repository does not advertise an unenforced gate.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| The process has no contradictory trigger or authority path | Fresh-context governance review | Review `AGENTS.md`, product delivery, topology, process, and template together | Initial review found authority, sponsorship, trigger-precedence, audit, containment, and bootstrap gaps; each was corrected, and focused re-review found no remaining P1-P3 finding |
| Required reviews leave an auditable record | A one-to-one reviewer roster and review table | Inspect `docs/RFC_TEMPLATE.md` and this bootstrap RFC | Template and bootstrap record contain one row per required perspective; all five perspectives approved after focused re-review |
| Agent workflows enforce the RFC boundary | Skill validation and forward tests | Validate and exercise `$draft-product-goal` and `$delivery-loop` with a public retry-contract scenario, then exercise the bounded-trial branch | Both skills pass structural validation; fresh-context retry simulations assigned Connector Experience, identified Remote Runtime support, and refused activation or implementation before acceptance, while a second goal simulation activated only private decision evidence and preserved the RFC boundary |
| Documentation remains internally valid | Repository validation, local-link check, and diff checks | Run the documented validation commands | Skill and asset validation, local-link validation, worktree diff checks, and cached-diff checks pass |

## Alternatives considered

### Continue with lead-agent judgment only

This is lower ceremony but leaves consequential decisions undiscoverable and
gives affected teams no durable review record. It does not scale the simulated
organization beyond one agent's task context.

### Require an RFC for every change

This is simple to state but creates an approval queue for defects, refactors,
tests, and reversible charter-local choices. The selected trigger and exemption
model concentrates ceremony on decisions with shared or durable consequences.

### Use lightweight architecture decision records only

An ADR can preserve rationale but does not by itself establish sponsorship,
affected-team review, bounded evidence, product-decision authority, or contract
propagation. The RFC template includes those controls while remaining a
repository-native Markdown record.

## Drawbacks and failure modes

The process adds writing and review cost before some implementation work.
Engineering Enablement owns keeping the template and skills usable; the lead
agent owns preventing routine work from being over-classified. Mandatory
triggers and explicit review rows reduce the risk of quiet bypass, while the
bounded-trial and containment paths prevent the process from blocking evidence
or urgent safety work.

The process can still become ceremonial if reviewers approve without evidence
or contract updates are deferred indefinitely. Delivery cannot close until
required propagation and evidence are complete.

## Acceptance and verification

- **End-to-end demonstration:** A public-contract product request is assigned
  to a stream, classified as RFC-required, and prevented from activating
  implementation before acceptance; a bounded evidence trial remains possible.
- **Automated oracle:** Repository asset validation, skill validation, local
  link validation, and diff checks pass.
- **Quality gates:** `ruby scripts/validate-agent-assets.rb`, both skill-creator
  quick validators, `git diff --check`, and a local Markdown link check.
- **Independent review:** Fresh-context review covers governance consistency and
  each affected topology perspective.
- **Interaction exit:** All required perspectives approve or have objections
  dispositioned, and the operating documents and skills agree.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Not affected | No behavioral change | No update required |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Not affected | No connector syntax change | No update required |
| `docs/RUNTIME_CONTRACTS.md` | Not affected | No runtime contract change | No update required |
| `docs/TEAM_TOPOLOGY.md` and active charters | Affected | Require an RFC for interface or accountability movement | Included in adoption diff |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Affected | Add team assignment, qualification, activation, delivery, and completion gates | Included in adoption diff; skill and asset validation pass |
| Examples, diagnostics, fixtures, and tests | Not affected | No product behavior change | No update required |

The RFC records rationale; the operating documents and skills define current
operation.

## Unresolved questions

No decision-critical question is intended to remain at acceptance. Process
friction discovered during real delivery will be evidence for a superseding
RFC or a non-material clarification, depending on whether the decision changes.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Connector Experience perspective | Connector Experience | Approved | Connector-contract triggers, documented-contract defect restoration, and both activation paths preserve its product accountability | Accepted; no objection |
| Query Experience perspective | Query Experience | Approved | Query decisions require participation, PM-reserved public choices remain explicit, and no query accountability moves | Accepted; no objection |
| Remote Runtime perspective | Remote Runtime | Approved | Shared runtime commitments trigger review, platform work retains a stream consumer, and delivery enforces the accepted-RFC and containment boundaries | Accepted; no objection |
| Relational Semantics perspective | Relational Semantics | Approved | Correctness changes trigger review, invariants remain binding, and bounded trials cannot establish unproven semantic contracts | Accepted; no objection |
| Engineering Enablement perspective | Engineering Enablement | Approved | Non-product sponsorship, authority, lifecycle, review records, skill routing, and validation form a coherent operating capability | Accepted; no objection |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Not required because this changes the engineering
  operating model without selecting a product-policy option.
- **Rationale:** Accept the proportionate trigger, sponsorship, affected-team
  review, decision, propagation, and supersession model. It makes durable
  decisions auditable without turning routine implementation into an approval
  queue, and the delivery skills enforce the same boundary described by the
  documents.
- **Material objections:** Initial review identified undefined authority, no
  non-product sponsor, conflicting trigger precedence, incomplete reviewer
  evidence, duplicate status, workflow bypasses, incomplete containment,
  non-deterministic withdrawal preservation, and an implicit bootstrap. The
  accepted text assigns the lead agent, defines both sponsorship paths and
  precedence, adds one-to-one review records and one status, gates both skills,
  makes containment and archival requirements explicit, and records adoption
  in this RFC. Focused re-review approved every required perspective with no
  remaining objection.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Maintain usable RFC governance and correct process drift discovered in delivery | Engineering Enablement | Affected teams through facilitation or bounded collaboration | Evidence of repeated friction, bypass, or ambiguity; no active follow-on is required at adoption |

This non-product objective remains with Engineering Enablement. The adoption
itself completes in the coherent decision and operating-document commit.
