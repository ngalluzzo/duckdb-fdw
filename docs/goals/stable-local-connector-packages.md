# Goal: Load stable local connector packages

Follow `docs/PRODUCT_DELIVERY.md`.

Status: **Active**. [RFC 0012](../rfcs/0012-define-local-package-sql-registration.md)
and [RFC 0013](../rfcs/0013-define-connector-package-v1-contract.md) are
Accepted and authorize this delivery.

## PM brief

### Outcome

For connector authors, enable a complete `duckdb_api/v1` package to be
validated, compiled, explicitly loaded, and queried through generated DuckDB
relations so useful REST and GraphQL connectors no longer require native C++
composition.

### Why now

The native four-relation product and its SQL and package decisions are proven.
The remaining product risk is whether the accepted author contract works as
permanent product code rather than privileged native metadata.

### Product guardrails

- Must implement the entire accepted v1 subset and preserve the relational,
  credential, network, resource, and lifecycle invariants in `AGENTS.md`.
- Must not ship a partial compiler, trial-only product shape, distribution
  workflow, or declaration that validates but is ignored.
- Preserve the native `0.7.0` behavior and RFC 0012's one-release dispatcher
  migration until an accepted package load publishes atomically.

### Success signals

- The repository GitHub package loads into DuckDB and exposes all four
  generated relations with the accepted schemas, inputs, execution behavior,
  explanation, and diagnostics.
- Invalid, incompatible, unsafe, or stale packages fail before publication;
  an active generation and its bound or in-flight scans remain usable.
- Offline package fixtures predict compiler, planner, request, decoding,
  relational, and failure behavior without live-service dependence.

### Reserved product decisions

RFC 0012 records the approved SQL, naming, lifecycle, and migration behavior.
RFC 0013 records the approved package identity, complete v1 declaration
subset, compatibility rules, exclusions, and local-package trust boundary.

## Agent commitment

### Observable interpretation

An author points `duckdb_api_load_connector` at the repository v1 package,
inspects the generated functions, and queries the same controlled anonymous
REST, authenticated REST, predicate, Link-pagination, and GraphQL-cursor
behaviors already proven natively. Failure is source-located, redacted,
atomic, and leaves the prior catalog generation intact.

### Acceptance evidence

- Demonstration: load, inspect, query, prepare, compose, reload, and reject the
  complete GitHub package through DuckDB.
- Automated oracle: schema and source-identity mutation tests; fixture
  execution; native/package catalog, plan, request, result, and error
  differentials; registration collision and lifecycle tests; compatibility,
  boundary, cancellation, and resource-exhaustion cases.
- Quality gates: focused team targets, `make build`, `make test`, `make demo`,
  source and dependency identities, RFC 0013 evidence verification, a fresh
  native product cell, agent-asset and public-inventory validation, and staged
  and unstaged whitespace checks.
- Independent review: Connector author-contract, relational-semantics,
  transport/policy/lifecycle, Query catalog/lifecycle, and test-oracle
  perspectives plus the final topology interaction-exit audit.

### Contract and invariant impact

- Propagate RFC 0013 through `docs/ARCHITECTURE.md`,
  `docs/CONNECTOR_SPECIFICATIONS.md`, `docs/RUNTIME_CONTRACTS.md`, RFC 0012's
  implementation references, the canonical public inventory, examples,
  diagnostics, fixtures, product source, and tests.
- Planning remains deterministic and network-free. Compiled generations and
  accepted plans remain immutable. DuckDB owns every unproven relational
  operation. Secrets and host authority never enter packages, plans, fixtures,
  diagnostics, or digests. Resource arithmetic is checked and host policy can
  only narrow author declarations.

### Team and RFC routing

- Accountable stream: Connector Experience.
- Query Experience — **Collaboration, then X-as-a-Service:** publish generated
  functions and management/introspection surfaces from a bounded immutable
  generation view. Exit when Query parses no source and retains no Connector,
  Semantics, or Runtime internals.
- Relational Semantics — **Collaboration, then X-as-a-Service:** generalize
  typed inputs, operation selection, predicate facts, and plan parity. Exit
  when package/native differentials pass through the documented planner API.
- Remote Runtime — **Collaboration, then X-as-a-Service:** admit and execute
  name-independent REST and GraphQL plans and retain immutable active
  generations. Exit when its services consume no package syntax or DuckDB
  catalog state.
- Engineering Enablement — **Facilitation:** establish reproducible package,
  mutation, fixture, and dependency gates. Exit when domain teams own and run
  those gates without Enablement approval.
- RFC: RFCs 0012 and 0013 are Accepted; no decision gate is open.

### Unknowns and first trial

- Unknown: whether the current native team APIs are sufficiently
  name-independent for one package-compiled relation without consumers
  importing compiler internals.
- Trial: compile the repository package into a bounded immutable generation and
  run one generated relation end to end through the existing planner, runtime,
  and adapter. Perform the production responsibility pass before extending
  that path to the remaining relations.

### Delivery path

1. Each affected charter records its workstream, file ownership, dependencies,
   deterministic oracle, and observable interaction exit.
2. Propagate the accepted contract and establish the permanent compiler and
   immutable-generation provider interfaces with negative and identity
   evidence.
3. Prove one generated relation through the real team interfaces, then
   complete all retained REST, predicate, pagination, GraphQL, fixture,
   reload, registration, and migration behavior on that production shape.
4. Audit the actual source/test dependencies, complete independent review and
   repository gates, and record the user-visible completion evidence here.

## Responsibility and dependency map

| Workstream | Primary responsibility | Provides | Interaction exit |
| --- | --- | --- | --- |
| [Connector Experience](stable-local-connector-packages/connector-experience/plan.md) | Source admission, failsafe YAML, schema and semantic validation, deterministic diagnostics, compilation, identity, compatibility, fixtures, and immutable generation metadata | Bounded compiled generation and safe author evidence | Consumers use only documented immutable views and opaque generation ownership |
| [Relational Semantics](stable-local-connector-packages/relational-semantics/plan.md) | Typed input resolution, operation selection, predicate proof use, conservative plan construction, and package/native semantic parity | Complete immutable `ScanPlan` | Query and Runtime neither select operations nor reinterpret relational meaning |
| [Remote Runtime](stable-local-connector-packages/remote-runtime/plan.md) | Active-generation service, name-independent plan admission, fixture execution, protocol, policy, resource, cancellation, reload, and shutdown behavior | Registry snapshots and bounded `BatchStream` | Consumers coordinate only through the generation, executor, stream, authorization, and diagnostic services |
| [Query Experience](stable-local-connector-packages/query-experience/plan.md) | Generated relation registration, typed argument binding, management/introspection functions, atomic catalog publication, migration, and DuckDB lifecycle | Accepted SQL and query experience | Query parses no package or protocol source and pins the exact immutable generation observed at bind |
| [Engineering Enablement](stable-local-connector-packages/engineering-enablement/plan.md) | Reusable evidence, mutation, dependency, and release-gate facilitation | Deterministic package-delivery gates owned by domain teams | Connector, Query, Semantics, and Runtime maintain their own entries without an Enablement approval queue |

Connector publishes the immutable generation and Semantics publishes the
general plan boundary before Query and Runtime integrate the vertical slice.
After those provider APIs and oracles are frozen, disjoint team-owned modules
may proceed in parallel in separate worktrees. The lead agent owns integration,
accepted-contract propagation, cross-team dependency auditing, Git history,
final review, and goal closure.

## Completion record

Pending delivery evidence.
