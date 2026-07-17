# Relational Semantics team charter

Relational Semantics is the complicated-subsystem team for remote optimization
that preserves DuckDB meaning. This charter inherits
`docs/TEAM_TOPOLOGY.md` and cannot override `AGENTS.md`, an accepted RFC, or the
product and engineering contracts.

## Mission and customers

Serve Connector Experience and Query Experience with an explainable,
deterministic planning service that uses remote capabilities only when their
relationship to DuckDB semantics is proven and otherwise falls back
conservatively.

The team reduces specialist cognitive load for the stream teams; it does not
create a separate product value stream.

## Responsibilities

- Own the relational contract from protocol-neutral `ScanRequest` to complete,
  immutable, explainable `ScanPlan`.
- Own operation eligibility and selection, predicate translation accuracy,
  residual ownership, projection closure, ordering and limit safety,
  cardinality estimates, provider cardinality constraints, and conservative
  fallback.
- Define semantic proof obligations that connector validation and protocol
  capabilities must expose.
- Maintain deterministic examples, counterexamples, and property tests against
  DuckDB evaluation.
- Sponsor non-product RFCs for durable semantic objectives; product RFCs retain
  a consuming stream sponsor.

## Explicit non-responsibilities

- Connector package ergonomics, syntax design, or author-facing product
  decisions.
- DuckDB adapter lifecycle or query-interface product decisions.
- Transport, authentication, network policy enforcement, retries, caching,
  pagination mechanics, or runtime ownership.
- Product accountability or permission to trade correctness for performance.

The team may contribute across these boundaries but does not acquire their
accountability.

## Team API and service expectations

Relational Semantics consumes immutable `CompiledConnector`, the active DuckDB
capability profile, protocol-neutral `ScanRequest`, and explicit protocol
planning capabilities. It provides a semantically explicit immutable
`ScanPlan`, classification reasons, residual obligations, and conservative
fallback to Connector Experience, Query Experience, and Remote Runtime.

Consumers can expect no network I/O during planning and no semantic decision to
be deferred implicitly to execution. An interface change follows
`docs/RFC_PROCESS.md` and requires producer and consumer review.

## Decision rights and review lens

Agents operating in this charter are delegated reversible, charter-local
planning and proof-mechanism decisions that preserve accepted contracts and
team APIs. The lead agent retains all other technical decision authority.
Public behavior, compatibility, and other reserved choices require the product
manager under `AGENTS.md`.

When consulted, require evidence that:

- remote predicate `R` is safe only when DuckDB predicate `D` implies `R`, and
  exactness additionally proves `R` implies `D`;
- every residual predicate has exactly one owner;
- filtering and required ordering precede limit or offset where semantics
  demand it;
- providers preserve base-row cardinality;
- unavailable metadata becomes conservative behavior, never SQL-text
  reconstruction; and
- optimization properties are checked against DuckDB over the same fixture
  rows, including nulls, errors, and counterexamples.

The team must object to an unproven correctness shortcut even when it improves
performance or simplifies another component.

## Success evidence

- `ScanRequest → ScanPlan` tests cover exact, superset, unsupported, ambiguous,
  and failure classifications with explainable reasons.
- Property tests compare remote-plus-residual behavior with DuckDB evaluation.
- Counterexamples demonstrate conservative fallback for unavailable or
  insufficient capabilities.
- Consumers can use the planning service without reimplementing relational
  reasoning.

## Cognitive-load limits

Keep protocol transport mechanics, connector authoring workflow, DuckDB FFI
lifecycle, and product prioritization outside the subsystem. Express required
facts as small explicit interfaces rather than importing whole neighboring
domains.

## Supported interactions

| Partner | Mode | Purpose | Exit condition |
| --- | --- | --- | --- |
| Connector Experience | Collaboration, then X-as-a-Service | Define author-declared semantic facts and validation | Declarations compile into an executable semantic oracle with conservative fallback |
| Query Experience | Collaboration, then X-as-a-Service | Align capability profiles and `ScanRequest` construction | Planning properties pass across supported capability profiles |
| Remote Runtime | Collaboration, then X-as-a-Service | Align plan obligations and execution capabilities | Runtime consumes `ScanPlan` without reclassifying relational meaning |
| Engineering Enablement | Facilitation | Transfer property-testing and counterexample practices | Relational Semantics maintains the practice and gates independently |

## Consultation disposition

Return `Approved`, `Objected`, or `Needs evidence` with the affected semantic
law, proof or counterexample, required action, and the interaction exit
condition. An optimization preference is never sufficient evidence to weaken a
relational invariant.
