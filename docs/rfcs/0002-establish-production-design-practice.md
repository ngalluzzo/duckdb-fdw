# RFC 0002: Establish production design and code documentation practice

```yaml
rfc: "0002"
title: "Establish production design and code documentation practice"
status: "Accepted"
rfc_type: "Non-product"
sponsor_team: "Engineering Enablement"
technical_decision_owner: "Lead agent"
product_approver: "none"
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
linked_outcome_or_objective: "Prevent experimental source shape from becoming production design debt"
supersedes: "none"
```

## Summary

Adopt a responsibility-based production-design practice for agent-led delivery.
Before a trial becomes product code, the lead agent maps the accepted team
interfaces to cohesive production modules, dependency direction, focused test
homes, and charter-specific code documentation. Final topology interaction
exits are audited against actual source dependencies. The practice uses
engineering judgment rather than line-count, file-count, comment-density, or
subjective approval gates.

## Sponsorship and context

- **RFC type:** Non-product.
- **Sponsoring team:** Engineering Enablement.
- **Linked outcome or objective:** Prevent experimental source shape from
  becoming production design debt while keeping receiving teams responsible
  for their own code quality.
- **Why now:** The first product slice proved its behavioral contracts but
  promoted the boundary trial's single-file shape into a catch-all production
  module and test suite. Every charter-backed hygiene perspective objected to
  the resulting cognitive load and incomplete team-interface exits.

## Problem

The operating model names durable interfaces for `CompiledConnector`,
`ScanRequest`, `ScanPlan`, fixture execution, `BatchStream`, and the DuckDB
adapter. The delivery workflow requires a thin trial for uncertain boundaries,
but it does not require a responsibility pass when trial code is promoted.

In commit `966315f`, the current source consequently has these observed facts:

- `src/duckdb_api_core.cpp` contains connector construction, snapshots,
  relational planning, JSON decoding, policy enforcement, fixture execution,
  and stream lifecycle in 964 lines;
- `src/include/duckdb_api/contracts.hpp` exposes connector, planner, fixture,
  runtime, and DuckDB-coupled types in one undocumented header;
- `src/duckdb_api_extension.cpp` constructs and retains fixture-provider
  internals even though RFC 0001 requires the adapter to consume only the
  runtime stream interface;
- `test/cpp/duckdb_api_contract_tests.cpp` combines connector, planner,
  decoder, runtime, concurrency, lifecycle, and adapter integration in one
  708-line suite; and
- the production and test C++ files contain no substantive code comments or
  adjacent interface documentation.

The behavior is deterministic and currently passes its product oracles. The
failure is that independently changing responsibilities, cognitive-load
boundaries, and design intent are not visible in the code. A future connector
schema, planner capability, decoder rule, or lifecycle change converges on the
same source and test modules.

## Decision drivers and invariants

- **Must preserve:** Product behavior, relational correctness, lifecycle and
  resource invariants, RFC governance, and receiving-team quality ownership.
- **Must enable:** A maintainer or technically literate product reader can
  trace the system through cohesive modules and understand team APIs and
  non-obvious safety rationale near the code.
- **Must enable:** Agents can change and review one responsibility without
  loading unrelated connector, planner, runtime, and adapter internals.
- **Must not introduce:** Directories named mechanically after teams, arbitrary
  size thresholds, comment quotas, more adversarial scripts as a substitute
  for design, or a permanent Engineering Enablement approval queue.

## Proposed decision

Add a mandatory responsibility pass to agent-led delivery before a greenfield
trial or vertical slice becomes product code:

1. Map each affected team interface to its producer, consumers, contract, and
   interaction exit condition.
2. Give each production module one primary responsibility and reason to change;
   keep host adapters at the edge and dependencies directed through provider
   interfaces rather than provider internals.
3. Give each oracle family a test home matching the production responsibility,
   with explicit shared test support and integration-only suites for genuinely
   cross-layer behavior.
4. Apply the selected charters' code-documentation expectations to team APIs,
   lifecycle-sensitive state, non-obvious correctness algorithms, policy
   ordering, and compatibility boundaries.
5. Audit claimed interaction exits against declarations, includes,
   construction points, build targets, tests, and adjacent documentation.

The responsibility map remains in the active goal or task plan while delivery
is underway. Shared documentation contains only durable module and dependency
intent after the implementation exists.

### Public behavior

Not affected. This RFC changes internal engineering practice and does not alter
SQL, connector-package behavior, configuration, diagnostics, compatibility, or
product exclusions.

### Shared interfaces

No semantic product or runtime interface is changed by this RFC. The practice
requires the implementation shape to make accepted team APIs independently
understandable and to preserve their documented consumer-provider direction.
Any later proposal that changes a shared interface still follows the normal RFC
triggers.

### Operational behavior

Not affected. Security, resources, cancellation, replay, caching, concurrency,
and shutdown contracts remain authoritative. The new documentation expectation
makes their non-obvious ownership and ordering visible beside lifecycle-
sensitive code.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Engineering Enablement | Sponsor and facilitator | Owns concise responsibility-pass guidance and transfers its first use | Facilitation | Query Experience applies and maintains the practice without Enablement review or waiver |
| Query Experience | Receiving stream team for the current correction | Documents DuckDB callback phases, adapter state, compatibility, error, cancellation, and close boundaries | Facilitation, then independent ownership | The adapter consumes provider APIs without provider internals and Query Experience owns its source and tests |
| Connector Experience | Provider of connector metadata | Documents schema meaning, provenance, immutability, defaults, and compatibility status | Collaboration for the current correction | `CompiledConnector` is independently inspectable without planner, runtime, or adapter knowledge |
| Remote Runtime | Provider of execution services | Documents resource authority, state machines, cancellation, close, error, and safety ordering | Collaboration for the current correction | Runtime execution is understandable and exercisable without DuckDB adapter internals |
| Relational Semantics | Provider of planning | Documents semantic ownership, classification reasons, conservative fallback, and proof assumptions | Collaboration for the current correction | Planning is understandable and exercisable without transport or DuckDB lifecycle knowledge |

No accountability or durable semantic interface moves. The charters gain
documentation expectations within responsibilities they already own.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** Not changed; planner
  decisions and their proof assumptions become locally documented and
  separately reviewable.
- **Authentication, credentials, network policy, and privacy:** Not changed.
- **Resource budgets, backpressure, and cancellation:** Not changed; ownership
  and ordering become explicit in runtime API documentation.
- **Replay units, retries, caching, and duplicate prevention:** Not changed.
- **Concurrency, immutability, and state ownership:** Not changed; lifecycle-
  sensitive declarations must state these invariants.
- **FFI, initialization, reload, shutdown, and failure containment:** Not
  changed; adapter and runtime documentation must identify the boundary.
- **Diagnostics, redaction, metrics, and progress:** Not changed.

## Compatibility and migration

No user, connector-package, data, or public API migration is required. Existing
production code is evaluated under the new responsibility pass before the next
release tag. Future delivery applies the practice when a trial graduates or a
change crosses multiple team responsibilities.

Rollback consists of reverting the operating-document and skill changes. That
would restore the process gap and is appropriate only if evidence shows the
practice adds cognitive load without improving independent changeability.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Does the present process omit trial graduation? | Direct workflow inspection | Compare `$delivery-loop` trial, implementation, and final-audit steps | Confirmed: no responsibility or code-documentation pass existed |
| Does the current slice combine different reasons to change? | Source, header, build, and test dependency inspection | Inspect commit `966315f` and RFC 0001's mandatory interfaces | Confirmed across connector, planner, decoder/runtime, adapter, and their tests |
| Is the issue a topology concern rather than file-size preference? | Independent charter-backed consequences and exit conditions | Query, Connector, Runtime, Semantics, and Enablement hygiene consultation | All five perspectives objected with concrete cross-boundary evolution paths |
| Does the revised skill produce a usable responsibility map? | Fresh-context forward use of revised `$delivery-loop` | Apply the skill to the current slice without prior diagnosis | Confirmed: the reviewer independently separated connector metadata, adapter request, planning, decoding, runtime streaming, DuckDB integration, and example composition; directed their dependencies; assigned focused oracle homes and documentation obligations; routed RFC boundaries; and kept every current interaction exit honestly Open. It also identified `BatchStream`/`ClientContext` decoupling as a separate shared-interface RFC decision rather than hiding it in the refactor. |
| Are the revised skills structurally valid? | Skill and repository asset validation | Run `quick_validate.py` for both changed skills and `scripts/validate-agent-assets.rb` | Confirmed with `/usr/bin/python3`: both skills returned `Skill is valid!`; repository agent-asset validation passed. |

## Alternatives considered

### Retain the current process

This preserves delivery speed and avoids subjective judgment, but repeats the
demonstrated path: behavioral evidence can be green while implementation
boundaries and interaction exits remain false. Rejected.

### Enforce maximum file sizes or comment coverage

This is deterministic and cheap to automate, but rewards arbitrary
fragmentation and low-value comments while failing to measure dependency
direction or reasons to change. Rejected.

### Add a maintainability adversarial-review gate

This supplies another review opportunity, but makes Engineering Enablement or
review agents a permanent subjective approval queue and acts after design debt
has already accumulated. Rejected as the primary control; normal code review
still applies.

### Create directories named after topology teams

This makes ownership labels visible but confuses an organizational model with a
component map. It can preserve the same coupling behind different paths.
Rejected.

## Drawbacks and failure modes

- The responsibility pass adds up-front design work and requires judgment.
  Query Experience owns applying it proportionately; Engineering Enablement
  must not expand it into ceremony.
- Documentation can drift. The owning team updates adjacent interface
  documentation in the same change as the behavior or lifecycle rule.
- Over-splitting can obscure a small coherent algorithm. Co-location remains
  allowed when responsibilities share invariants and a reason to change; line
  count alone cannot reject it.
- A responsibility map can become a speculative architecture plan. Keep it in
  the active goal until implementation exists and promote only durable intent.

## Acceptance and verification

- **End-to-end demonstration:** A fresh agent applies the revised delivery skill
  to the current slice and produces module responsibilities, dependency
  direction, test homes, code-documentation obligations, governance routing,
  and honest interaction-exit states without being given the prior diagnosis.
- **Automated oracle:** Repository skill structural validation and existing
  agent-asset validation; no new maintainability script.
- **Quality gates:** `quick_validate.py` for the changed skills,
  `ruby scripts/validate-agent-assets.rb`, `git diff --check`, and
  `git diff --cached --check`.
- **Independent review:** One fresh perspective for every affected charter on
  this exact proposal.
- **Post-acceptance facilitation exit:** Query Experience applies the
  responsibility pass to the current slice, owns the resulting module and test
  structure, and can make the next contained change without Enablement
  approval or unrelated provider knowledge. This exit remains Open; RFC
  acceptance authorizes the practice but does not claim that the current code
  has already been corrected.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Not affected | Product and relational behavior do not change | No update required |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Not affected | Public authoring behavior does not change | No update required |
| `docs/RUNTIME_CONTRACTS.md` | Not affected | Runtime semantics do not change | No update required |
| `docs/TEAM_TOPOLOGY.md` and active charters | Affected | Clarify source-dependency evidence and charter-specific code documentation | Updated with responsibility and documentation expectations in this decision change |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Affected | Add trial graduation, responsibility mapping, final exit audit, and code-documentation practice | Updated in this decision change; both skills and repository agent assets validate |
| Examples, diagnostics, fixtures, and tests | Not affected | No product behavior changes | No update required |

`CONTRIBUTING.md` also records the contributor-facing organization and code-
documentation conventions.

## Unresolved questions

None. Module decomposition for the current `0.1.0` slice is follow-on delivery
work and must preserve RFC 0001 rather than being decided generically here.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Query Experience perspective | Query Experience | Approved | The forward trial recovered adapter ownership, provider-directed dependencies, focused oracle homes, documentation obligations, governance routing, and honest Open exits | No action required for acceptance; correct the adapter as follow-on delivery and route any `BatchStream` interface change through its own RFC |
| Connector Experience perspective | Connector Experience | Approved | The practice makes metadata, provenance, immutability, consumer guarantees, and provider independence locally inspectable without changing author-facing behavior | Keep the Connector exit Open until `CompiledConnector` and its oracle are independently understandable and exercisable |
| Remote Runtime perspective | Remote Runtime | Approved | The practice covers runtime state, budgets, backpressure, cancellation, close, errors, shutdown, and validation ordering while preserving separate interface governance | Keep the Runtime exit Open until runtime code and tests are independent and the adapter consumes only runtime APIs |
| Relational Semantics perspective | Relational Semantics | Approved | Separate planner ownership, semantic oracle homes, proof documentation, and dependency auditing preserve `ScanRequest → ScanPlan` authority | Keep the Semantics exit Open until runtime stops reclassifying meaning and planning is independently exercisable |
| Engineering Enablement perspective | Engineering Enablement | Approved | Fresh-context use and validation proved the reusable practice without an automated subjective gate or permanent approval queue | Keep Facilitation Open until Query Experience applies, owns, and maintains the practice without Enablement approval |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Not required. The product manager requested the
  improvement, but this RFC changes no public or reserved product decision.
- **Rationale:** Accept. The current slice demonstrates a systemic trial-
  graduation gap, the proposed control addresses responsibility and dependency
  evidence without metric-driven fragmentation, a fresh-context agent used it
  to recover a governable correction map, and every affected charter approved
  the exact proposal.
- **Material objections:** Query Experience and Engineering Enablement first
  requested the decision-critical forward-use and structural-validation
  evidence. That evidence was supplied and both perspectives approved on
  focused re-review. No unresolved objection remains. All implementation and
  facilitation exits remain Open for follow-on delivery.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Correct the `0.1.0` implementation structure without changing accepted behavior | Query Experience | Connector Experience, Remote Runtime, and Relational Semantics through Collaboration; Engineering Enablement through Facilitation | RFC 0002 accepted and the current goal records the responsibility map and exit conditions |
