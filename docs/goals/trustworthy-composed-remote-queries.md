# Goal: Trustworthy composed remote queries

Follow `docs/PRODUCT_DELIVERY.md`.

Status: **Active as of 2026-07-19**. RFC 0010 is Accepted.

## PM brief

### Outcome

For DuckDB users querying remote relations, enable projections, predicates,
ordering, and limits to compose without changing DuckDB meaning, so ordinary
analytical queries remain trustworthy even when remote optimization is partial
or unavailable.

### Why now

The product has proved one predicate-selective scan. Before another protocol or
optimization path depends on the same planner, the native path must prove that
ordinary relational composition remains correct across supported, unsupported,
ambiguous, and unavailable optimization states.

### Product guardrails

- Must preserve the existing SQL surface, schema, secret behavior, and sole
  public `visibility = 'private'` remote predicate mapping.
- Must preserve duplicate-sensitive DuckDB results and truthful explanation.
- Must not add another public predicate mapping or remote projection, ordering,
  limit, or offset pushdown.
- Preserve the relational, security, resource, immutability, conversion, and
  lifecycle invariants in `AGENTS.md`.

### Success signals

- Users can compose projection, supported and unsupported predicates, ordering,
  limits, and offsets over the existing authenticated-repositories relation and
  receive the result DuckDB would produce after complete traversal.
- A supported conjunct may reduce controlled remote work; unsafe or unencodable
  composition falls back without changing the result.
- Explanation distinguishes selected optimization, conservative fallback,
  invalid planning, and DuckDB-pruned execution without overstating authority.
- The semantic decision is reusable and independently proven rather than
  embedded in GitHub request construction or the DuckDB adapter.

### Reserved product decisions

The product manager approved completing the `0.6.0` semantic-trust boundary
without adding public mappings or remote projection, ordering, or bound
pushdown. Those remain separate product decisions.

## Agent commitment

### Observable interpretation

A user runs an ordinary composed query such as:

```sql
SELECT full_name
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'authenticated_repositories',
    secret := 'github_default'
)
WHERE visibility = 'private'
  AND archived = false
ORDER BY id DESC
LIMIT 2;
```

The supported conjunct may select the existing `visibility=private` request,
while DuckDB retains final filter, projection, ordering, and bound ownership.
`OR`, `NOT`, incompatible safe candidates, missing adapter capabilities, and
unsupported leaves remain unrestricted unless the complete encoding is proven.
Invalid shared contracts fail before Runtime entry. DuckDB may independently
simplify or prune a scan.

### Acceptance evidence

- Demonstration: actual DuckDB evaluation compares composed optimized queries
  with a forced-local baseline over duplicate rows and `TRUE`, `FALSE`, and
  `NULL` cases, using SQL-appropriate bag, sequence, and tie-group assertions.
- Automated oracle: the production Semantics decision function covers exact,
  superset, unsupported, ambiguous, and invalid cases; Boolean composition;
  occurrence preservation; operation and encoding selection; ownership;
  projection closure; capability fallback; immutable copies; and safe reasons.
- Execution evidence: selected and unrestricted request shapes, one exhausting
  fallback trace, local early close, DuckDB pruning with zero Runtime opens, and
  planning failure with zero I/O.
- Quality gates: focused responsibility targets, `make build`, `make test`,
  `make demo`, source and dependency identity checks, a fresh native product
  cell, agent-asset validation, and staged and unstaged whitespace checks.
- Independent review: Query/DuckDB lifecycle and explanation, Connector
  declaration integrity, Relational implication and composition, Runtime plan
  admission and lifecycle, test-oracle quality, and two fresh adversarial
  perspectives.

### Contract and invariant impact

- Propagate RFC 0010 into `docs/ARCHITECTURE.md`, the private native-mapping
  note in `docs/CONNECTOR_SPECIFICATIONS.md`, `docs/RUNTIME_CONTRACTS.md`,
  diagnostics, examples, tests, release notes, and this completion record.
- Evolve the private pre-`1.0` `CompiledConnector`, `ScanRequest`, `ScanPlan`,
  plan-fixture, and explanation interfaces coherently. Connector-package syntax
  remains inactive and no public C++ ABI is promised.
- Remote predicate `R` remains safe only when DuckDB predicate `D` implies `R`;
  exactness additionally proves three-valued equivalence and exact occurrence
  preservation. Every residual has one owner, and bounds follow required local
  filtering and ordering.
- Planning remains deterministic and network-free. Execution remains immutable,
  bounded, cancelable, credential-safe, strictly converting, and sequentially
  paginated.

### Team and RFC routing

- Accountable stream: Query Experience.
- Relational Semantics — **Collaboration, then X-as-a-Service:** establish the
  bounded candidate algebra and production composition/law oracle. Exit when
  every classification and conservative fallback passes through the public
  planning service and consumers perform no semantic reclassification.
- Connector Experience — **X-as-a-Service with bounded collaboration:** provide
  validated immutable proof, base-domain, occurrence-preservation, and encoding
  facts, including a distinct controlled exact fixture. Exit when Semantics
  consumes only the public declaration and fixture APIs.
- Remote Runtime — **X-as-a-Service with bounded collaboration:** admit and
  execute typed plans without deriving semantic authority. Exit when focused
  Runtime tests consume public plan fixtures and prove fail-closed, zero-I/O,
  resource, cancellation, and close behavior.
- RFC: [RFC 0010](../rfcs/0010-prove-conservative-relational-composition.md) is
  Accepted and authorizes the shared contract.

### Unknowns and first trial

None identified. RFC 0010 resolved the decision-critical boundary. Delivery
begins with the independently testable provider contracts before the composed
DuckDB product oracle.

### Delivery path

1. Produce the four charter-owned plans and freeze disjoint source, oracle,
   dependency, and interaction-exit ownership.
2. Deliver Connector's validated semantic facts and Relational Semantics'
   bounded decision service with an executable law matrix.
3. Deliver Runtime admission and Query translation, explanation, and actual-
   DuckDB differential evidence against those provider APIs.
4. Propagate durable contracts, run independent review and complete gates,
   audit interaction exits, commit the coherent outcome, and record completion
   evidence here.

## Responsibility and dependency map

| Workstream | Primary source ownership | Oracle ownership | Consumes | Provides | Interaction exit |
| --- | --- | --- | --- | --- | --- |
| [Connector Experience](trustworthy-composed-remote-queries/connector-experience/plan.md) | Validated immutable predicate proof, domain, occurrence, and encoding facts | Declaration validation and controlled exact fixture | Accepted RFC and native catalog invariants | Bounded `CompiledConnector` semantic facts | Consumers use the public declaration and fixture APIs only |
| [Relational Semantics](trustworthy-composed-remote-queries/relational-semantics/plan.md) | Candidate decision, composition, classification, ownership, and explanation facts | Production-function law matrix and counterexamples | Connector facts and Query request | Complete immutable `ScanPlan` | Consumers perform no semantic reclassification |
| [Remote Runtime](trustworthy-composed-remote-queries/remote-runtime/plan.md) | Fail-closed typed-plan admission and unchanged bounded execution | Public-plan-fixture admission and lifecycle tests | Complete `ScanPlan` | Bounded `BatchStream` | Runtime requires no Connector, Query, or planner internals |
| [Query Experience](trustworthy-composed-remote-queries/query-experience/plan.md) | Structured DuckDB translation, capability profile, plan explanation, and product composition | Adapter and actual-DuckDB differential oracles | Connector, Semantics, and Runtime provider APIs | Trustworthy composed SQL result | Product narrative passes without cross-team internals |

Connector facts precede semantic classification. Once the candidate and plan
interfaces compile, Runtime admission and Query integration can proceed in
parallel against public fixtures. The lead agent owns cross-workstream
integration, root build registration, authoritative contract propagation,
version and release records, final dependency audit, Git history, and goal
closure. No workstream edits another team's plan or duplicates a provider in a
consumer.

## Completion record

Implementation, permanent oracles, contract propagation, topology interaction
exits, independent review, and cached product gates are complete. The active
goal remains open only for the commit-bound fresh native product cell required
by `AGENTS.md`; its command, result, and final commit identity will be appended
here after the implementation commit.
