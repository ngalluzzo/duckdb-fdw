# RFC NNNN: Short decision title

```yaml
rfc: "NNNN"
title: "Short decision title"
status: "Draft"
rfc_type: "Product or non-product"
sponsor_team: "Topology team name"
technical_decision_owner: "Named owner"
product_approver: "Named approver or none"
authors:
  - "Name or agent identity"
required_reviewers:
  - "Affected team or reviewer"
affected_teams:
  - "Team name"
linked_outcome_or_objective: "Product goal, decision-value goal, or non-product objective"
supersedes: "RFC number or none"
```

Replace every instructional placeholder before requesting review. Use only the
statuses defined in `docs/RFC_PROCESS.md`. The metadata `status` is the sole
authoritative lifecycle state; do not add or infer a second status elsewhere.

## Summary

State the proposed decision and its intended effect in one short paragraph.
Someone should understand the decision boundary without reading the full RFC.

## Sponsorship and context

- **RFC type:** Product or non-product.
- **Sponsoring team:** Name the accountable topology team.
- **Linked outcome or objective:** State the product outcome, decision-value
  goal, or non-product governance, maintenance, or risk-reduction objective.
- **Why now:** Explain why the decision is necessary for that outcome or
  objective.

For a product RFC, identify the connector author or DuckDB user affected and
use Connector Experience or Query Experience as the sponsoring team. For a
non-product RFC, identify affected teams and the accountable objective without
inventing a customer or product outcome.

Do not use repository incompleteness alone as product justification.

## Problem

Describe the current limitation, ambiguity, or conflict. Include a concrete
scenario showing why existing contracts or interfaces cannot resolve it.

Separate observed facts from assumptions and unknowns.

## Decision drivers and invariants

List the requirements and constraints that determine a responsible choice.
Carry forward every applicable invariant from `AGENTS.md` and the product and
engineering contracts.

- **Must preserve:** Required behavior or invariant.
- **Must enable:** Capability necessary for the linked outcome.
- **Must not introduce:** Unacceptable behavior, risk, or commitment.

## Proposed decision

Describe the decision precisely enough that affected teams can evaluate their
interfaces and obligations. Cover successful behavior, meaningful failure
behavior, ownership, and compatibility.

### Public behavior

Describe changes to SQL, connector packages, configuration, diagnostics,
compatibility, or explicit exclusions. Write `Not affected` with a reason when
the decision has no public-behavior impact.

### Shared interfaces

Describe changes to `CompiledConnector`, `ScanRequest`, `ScanPlan`,
`BatchStream`, protocol interfaces, fixture execution, plan explanation, or any
other team API. Identify provider and consumer expectations.

### Operational behavior

Describe changes to security, resources, cancellation, backpressure, replay,
retry, caching, concurrency, initialization, reload, shutdown, or observability.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Sponsoring team | Accountable for the product outcome or non-product objective | Describe impact | Collaboration, X-as-a-Service, or Facilitation | Observable condition |
| Affected team | Provider, consumer, subsystem, or facilitator | Describe impact | Interaction mode | Observable condition |

Explain any cognitive load moved between teams. If an accountability or
interface boundary moves, identify the required updates to
`docs/TEAM_TOPOLOGY.md` and the affected active charters.

## Correctness, security, and lifecycle analysis

For each area, explain the impact or state `Not affected` with evidence:

- relational semantics and conservative fallback;
- authentication, credentials, network policy, and privacy;
- resource budgets, backpressure, and cancellation;
- replay units, retries, caching, and duplicate prevention;
- concurrency, immutability, and state ownership;
- FFI, initialization, reload, shutdown, and failure containment; and
- diagnostics, redaction, metrics, and progress reporting.

Do not treat an invariant as optional because the initial implementation does
not exercise it.

## Compatibility and migration

Describe existing users, connector packages, team consumers, or stored data
affected by the decision. State:

- compatibility guarantees or intentional incompatibilities;
- migration or coexistence behavior;
- rollback conditions and limits; and
- how unsupported capability profiles behave conservatively.

Write `No migration required` only when the evidence supports it.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Decision-critical uncertainty | Convincing oracle | Primary-source research, deterministic test, property, or probe | Result or pending evidence |

Link raw artifacts where practical. Record counterexamples and failure paths,
not only successful demonstrations. A pending decision-critical result prevents
acceptance.

## Alternatives considered

For every credible alternative, describe:

- the approach;
- benefits;
- drawbacks and risks;
- effect on team cognitive load and interfaces; and
- why it was not selected.

Include retaining the current behavior when that is a real option.

## Drawbacks and failure modes

State the proposal's costs, complexity, operational burden, and ways it can
fail. Identify which team owns each resulting responsibility.

Do not disguise known drawbacks as future implementation details.

## Acceptance and verification

- **End-to-end demonstration:** Describe the observable successful and failure
  behavior.
- **Automated oracle:** Name the deterministic fixtures, properties, or tests
  that prove the decision.
- **Quality gates:** Name the applicable repository commands and compatibility
  checks.
- **Independent review:** Name the required perspectives based on risk.
- **Interaction exit:** State the evidence that ends collaboration or
  facilitation.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected or not affected | Describe change or reason | Link or pending |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected or not affected | Describe change or reason | Link or pending |
| `docs/RUNTIME_CONTRACTS.md` | Affected or not affected | Describe change or reason | Link or pending |
| `docs/TEAM_TOPOLOGY.md` and active charters | Affected or not affected | Describe change or reason | Link or pending |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Affected or not affected | Describe change or reason | Link or pending |
| Examples, diagnostics, fixtures, and tests | Affected or not affected | Describe change | Link or pending |

The RFC records rationale; these sources define current behavior and operation.

## Unresolved questions

List non-blocking questions that may be answered during delivery. Move every
decision-critical question to Evidence and bounded trials; an RFC with a
decision-critical unresolved question cannot be accepted.

## Review record

Include exactly one row for every entry in `required_reviewers`.

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Named reviewer | Affected team | Pending, Approved, or Objected | Link, rationale, or objection | Pending or recorded disposition |

The RFC cannot be decided while a required row is missing, a result is Pending,
or an objection lacks a disposition.

## Decision and rationale

- **Technical decision owner:** Name.
- **Product approval:** Decision and approver, or `Not required` with reason.
- **Rationale:** Explain the decision and evidence relied upon.
- **Material objections:** Record each objection and its disposition.
- **Superseded by:** RFC number or `Not applicable`.

Complete this section when the RFC is decided. Acceptance is not implementation
completion.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Product outcome or non-product objective | Owning topology team | Team, mode, and exit condition | Accepted decision and required product approval |

Product outcomes are candidate goals until activated through
`docs/PRODUCT_DELIVERY.md` and must use a stream-aligned accountable team.
Non-product work retains its owning team and stated objective. Do not include
component task breakdowns or speculative delivery dates.
