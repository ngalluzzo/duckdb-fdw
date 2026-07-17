# Query Experience team charter

Query Experience is the stream-aligned team for the path from a DuckDB SQL
question to a trustworthy remote result or actionable diagnostic. This charter
inherits `docs/TEAM_TOPOLOGY.md` and cannot override `AGENTS.md`, an accepted
RFC, or the product and engineering contracts.

## Mission and customers

Serve DuckDB users by making remote API data behave like a correct,
explainable, bounded DuckDB relation without requiring users to understand
connector internals or remote protocol mechanics.

The team is accountable when the primary acceptance narrative ends with a
DuckDB user querying, inspecting, explaining, cancelling, or diagnosing remote
data.

## Responsibilities

- Own DuckDB registration, connection-facing integration, bind and scan
  lifecycle, DuckDB-facing FFI safety, adapter capability profiles, and
  user-visible query ergonomics.
- Translate only metadata the active DuckDB integration exposes into a
  protocol-neutral `ScanRequest`; never reconstruct unavailable structure from
  SQL text.
- Consume `ScanPlan` and `BatchStream` through pull-oriented, bounded,
  cancelable adapter behavior.
- Own query-visible explanation, diagnostics, progress, compatibility evidence,
  and conservative behavior when DuckDB capabilities are unavailable.
- Sponsor product RFCs whose outcome primarily changes SQL, adapter, lifecycle,
  explanation, or query-visible behavior.

## Explicit non-responsibilities

- Connector package syntax, validation, or package compilation.
- Relational operation selection or proof of pushdown correctness.
- Remote transport, authentication execution, pagination, retry, caching, or
  provider internals.
- Product-policy decisions reserved to the product manager.

The team may contribute across these boundaries but does not acquire their
accountability.

## Team API and service expectations

Query Experience provides the DuckDB adapter capability profile and
protocol-neutral `ScanRequest` to Relational Semantics. It exposes the accepted
`ScanPlan`, structured diagnostics, and bounded `BatchStream` behavior to the
DuckDB integration.

The team expects:

- Connector Experience to provide validated immutable `CompiledConnector`
  snapshots and author-facing diagnostics;
- Relational Semantics to return a complete, conservative, explainable
  `ScanPlan`; and
- Remote Runtime to return a bounded, cancelable `BatchStream` with progress
  and redacted structured failures where supported.

An interface change follows `docs/RFC_PROCESS.md` and requires affected
provider review.

## Decision rights and review lens

Agents operating in this charter are delegated reversible, charter-local
adapter and query-experience implementation decisions that preserve accepted
contracts and team APIs. The lead agent retains all other technical decision
authority. Public SQL behavior, compatibility, exclusions, and other reserved
choices require the product manager under `AGENTS.md`.

When consulted, evaluate whether:

- behavior is DuckDB-native and visible through ordinary SQL or explicit
  connector functions;
- bind and planning remain deterministic and free of network I/O;
- missing capabilities fall back conservatively without SQL-text inference;
- cancellation, progress, diagnostics, and lifecycle behavior are coherent;
  and
- an end-to-end DuckDB demonstration proves both success and meaningful
  failure paths.

## Success evidence

- The accepted SQL narrative returns the correct result or an actionable
  redacted error.
- Capability-profile tests prove conservative behavior across supported DuckDB
  integration profiles.
- Bind, initialization, repeated scans, cancellation, shutdown, and the
  DuckDB-facing FFI boundary have deterministic lifecycle evidence.
- Explain output distinguishes remote and residual work without overstating
  optimization.

## Cognitive-load limits

Keep connector-language design, protocol-specific request construction,
relational proof, and transport policy behind explicit team APIs. Repeated
adapter workarounds for those internals indicate a misplaced boundary.

## Supported interactions

| Partner | Mode | Purpose | Exit condition |
| --- | --- | --- | --- |
| Relational Semantics | Collaboration, then X-as-a-Service | Prove capability-profile and `ScanRequest → ScanPlan` behavior | Conservative plans and explain output pass a deterministic oracle |
| Remote Runtime | Collaboration, then X-as-a-Service | Prove `BatchStream`, cancellation, progress, and lifecycle behavior | The adapter consumes a documented low-friction runtime service |
| Connector Experience | Collaboration for public end-to-end behavior | Align author declarations with query-visible results | Author and query acceptance narratives agree in one fixture |
| Engineering Enablement | Facilitation | Transfer DuckDB compatibility and lifecycle testing practice | Query Experience maintains the matrix and gates independently |

## Consultation disposition

Return `Approved`, `Objected`, or `Needs evidence` with the affected SQL,
adapter, lifecycle, or query-experience contract, concrete evidence, required
action, and the interaction exit condition. Preference for a different adapter
mechanism is not an objection without a user, compatibility, correctness, or
lifecycle consequence.
