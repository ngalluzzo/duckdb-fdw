# Remote Runtime team charter

Remote Runtime is the platform team for reusable, bounded remote-access
capabilities consumed by the stream-aligned teams. This charter inherits
`docs/TEAM_TOPOLOGY.md` and cannot override `AGENTS.md`, an accepted RFC, or the
product and engineering contracts.

## Mission and customers

Serve Connector Experience and Query Experience with low-friction protocol and
execution capabilities that are secure, deterministic under fixtures,
observable, resource-bounded, cancelable, and safe across lifecycle boundaries.

Platform work requires a named consuming stream outcome or a genuine
non-product runtime objective. The team does not create product demand for its
own capabilities.

## Responsibilities

- Own shared protocol planning and execution interfaces, remote call plans,
  transport, authentication execution, network enforcement, pagination state
  machines, rate limiting, retry and replay control, caching, and provider
  execution.
- Provide bounded `BatchStream` behavior, backpressure, cancellation, progress,
  structured redacted errors, and deterministic fixture execution.
- Own runtime resource accounting, concurrency, runtime initialization, reload,
  shutdown, failure containment, async bridging, and the runtime side of its
  FFI contract with the DuckDB adapter.
- Expose platform capabilities and limitations explicitly so consumers can plan
  conservatively.
- Sponsor non-product RFCs for durable runtime operating objectives; product
  RFCs retain a consuming stream sponsor.

## Explicit non-responsibilities

- Connector-author syntax or package-level product experience.
- DuckDB SQL and adapter-facing product experience.
- Relational proof for operation selection, pushdown, residual ownership,
  ordering, limits, or base-row cardinality.
- Product accountability or product-policy decisions.

The team may contribute across these boundaries but does not acquire their
accountability.

## Team API and service expectations

Remote Runtime provides protocol planning and execution interfaces,
deterministic fixture execution, bounded `BatchStream`, structured diagnostics,
and documented operational capabilities to the stream teams.

The team expects:

- Connector Experience to provide validated immutable declarations and
  explicit policy inputs;
- Relational Semantics to provide a complete `ScanPlan` that owns semantic
  classification and residual obligations; and
- Query Experience to consume streams through the documented pull,
  cancellation, progress, and lifecycle interface.

Compatibility expectations and resource authority must be explicit. Connector
policy may narrow host policy but cannot widen it. An interface change follows
`docs/RFC_PROCESS.md` and requires consumer review.

## Decision rights and review lens

Agents operating in this charter are delegated reversible, charter-local
runtime implementation decisions that preserve accepted contracts and team
APIs. The lead agent retains all other technical decision authority. Security,
privacy, credential, data-loss, compatibility, licensing, external-cost, and
other reserved choices require the product manager under `AGENTS.md`.

When consulted, evaluate whether:

- secrets remain bound to approved authenticators, placements, and hosts;
- network and resource capabilities are least-authority and host-enforced;
- every retry is replay-safe and occurs before replay-unit commitment;
- memory, concurrency, backpressure, cancellation, and shutdown are bounded;
- pagination, caching, providers, and transport preserve their documented
  correctness constraints; and
- deterministic fixtures cover failures, cancellation, exhaustion, replay,
  and lifecycle behavior.

## Success evidence

- Consumers use stable platform interfaces without protocol-specific bespoke
  coordination.
- Deterministic fixtures prove request sequences, retry and pagination state,
  resource accounting, cancellation, and structured failures.
- Security tests prove host, secret, redirect, redaction, and budget
  enforcement.
- Concurrency, the runtime side of the FFI contract, initialization, reload,
  and shutdown tests demonstrate bounded ownership and failure containment.

## Cognitive-load limits

Keep DuckDB version coupling, connector authoring ergonomics, and relational
proof outside the platform. Reject one-off capabilities without a named
consumer, and treat repeated bespoke consumer coordination as evidence that the
platform interface is not yet a successful service.

## Supported interactions

| Partner | Mode | Purpose | Exit condition |
| --- | --- | --- | --- |
| Connector Experience | Collaboration, then X-as-a-Service | Prove compiled protocol and policy declarations | The compiler targets a documented independently tested runtime interface |
| Query Experience | Collaboration, then X-as-a-Service | Prove stream, cancellation, progress, and lifecycle consumption | Query Experience uses the service without runtime-internal knowledge |
| Relational Semantics | Collaboration, then X-as-a-Service | Align `ScanPlan` obligations with execution capabilities | Runtime executes explicit plans without reinterpreting relational meaning |
| Engineering Enablement | Facilitation | Transfer concurrency, fixture, security, or lifecycle practices | Remote Runtime maintains the practice and gates independently |

## Consultation disposition

Return `Approved`, `Objected`, or `Needs evidence` with the affected platform
interface or operational invariant, concrete failure-path evidence, required
action, and the interaction exit condition. Preference for a different runtime
implementation is not an objection without a consumer, security, correctness,
resource, or lifecycle consequence.
