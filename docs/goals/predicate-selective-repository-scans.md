# Goal: Predicate-selective repository scans

Follow `docs/PRODUCT_DELIVERY.md`.

Status: **Active as of 2026-07-19**. Product approval is recorded in the
project task. A required RFC must be Accepted before implementation establishes
the public or shared predicate-pushdown contract.

## PM brief

### Outcome

For a DuckDB user querying `github.authenticated_repositories`, enable
the relation to expose GitHub's repository `visibility` and enable
`WHERE visibility = 'private'` to constrain the remote repository scan while
preserving the exact DuckDB result, so the user does not have to traverse
unrelated repositories.

### Why now

The permanent product proves authenticated, bounded, paginated repository
traversal. The next fundamental product risk is whether DuckDB relational
intent can safely reduce remote work without changing SQL meaning. Resolving
that risk is the first independently valuable part of the relational-trust
milestone.

### Product guardrails

- Must: use ordinary SQL on the existing authenticated-repositories relation,
  its additive `visibility` column, and its explicitly named temporary secret.
- Must: return the same row bag as complete remote traversal followed by the
  DuckDB predicate, while reducing the controlled request sequence when the
  supported predicate selects a narrower remote domain.
- Must: explain the remote restriction, its accuracy, and the single owner of
  any residual predicate truthfully.
- Must: fall back to the existing complete traversal and DuckDB filtering when
  a filter, capability profile, or upstream contract cannot support the remote
  restriction safely.
- Must not: add other predicate mappings, projection, ordering, limit or offset
  pushdown, retries, caching, GraphQL, connector-package loading, YAML authoring,
  or distribution work.
- Preserve: offline bind and planning, immutable metadata and plans, exact
  credential and network authority, bounded sequential pagination, strict
  conversion, cancellation, close, and existing query behavior.

### Success signals

- `WHERE visibility = 'private'` returns exactly the same rows as complete
  traversal followed by local DuckDB filtering in a controlled
  public/private/internal fixture.
- The controlled request oracle observes `visibility=private` on every page and
  a smaller bounded page sequence.
- Unsupported or structurally ambiguous predicates retain the existing full
  traversal and local evaluation.
- Explain evidence distinguishes remote restriction, accuracy, and residual
  ownership without network or secret resolution.
- Existing unfiltered and differently filtered queries remain compatible.

### Reserved decisions

The product manager approved adding the upstream `visibility` fact and using
`visibility = 'private'` as the first relational-trust capability after the
`private = TRUE` candidate could not prove safe treatment of internal
repositories. Other relational operations remain separate product goals. This
goal alone does not establish that every `0.6.0` roadmap gate is complete.

## Agent commitment

### Observable interpretation

The user creates the existing named temporary secret and executes:

```sql
SELECT id, full_name, private, visibility, fork, archived
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'authenticated_repositories',
    secret := 'github_default'
)
WHERE visibility = 'private'
ORDER BY id;
```

The DuckDB adapter consumes only supported structured filter metadata; it does
not reconstruct SQL text. Relational Semantics may translate the supported
predicate into GitHub's fixed `visibility=private` input only when the
connector declaration and proof make the mapping safe. The output value and
remote restriction come from the same REST contract; an internal row neither
satisfies the SQL predicate nor belongs in the restricted response. The
original predicate remains owned by DuckDB as a residual, or the scan falls
back to the complete base relation. Ordering remains local.

### Acceptance evidence

- Demonstration: a controlled multi-page mixed-visibility relation produces
  the same result as a forced-local baseline while the optimized request trace
  excludes unrelated repository pages. Explain and prepare remain offline.
- Automated oracle: structured DuckDB-filter conversion; private/internal/public,
  unsupported, ambiguous, conjunction, constant, and `NULL` counterexamples;
  operation/input selection; residual ownership; immutable plan snapshots;
  request construction; pagination; fallback; lifecycle; redaction; and
  existing-relation regressions.
- Quality gates: focused team targets, `make build`, `make test`, `make demo`,
  source/dependency identity checks, a fresh native product cell, agent-asset
  validation, and staged and unstaged diff checks.
- Independent review: final Query filter/lifecycle/FFI, Connector declaration,
  Relational implication/residual, Runtime request/security/pagination, test
  oracle, and adversarial perspectives.

### Contract and invariant impact

- The accepted RFC rationale must propagate through `docs/ARCHITECTURE.md`,
  `docs/CONNECTOR_SPECIFICATIONS.md`, `docs/RUNTIME_CONTRACTS.md`, diagnostics,
  examples, fixtures, tests, release notes, and the roadmap evidence when the
  milestone is complete.
- `CompiledConnector`, `ScanRequest`, `ScanPlan`, plan explanation, the DuckDB
  table-function capability profile, and the fixed GitHub request may change as
  private pre-`1.0` team interfaces. No connector-package syntax or public
  native ABI is activated.
- For DuckDB predicate `D` and remote predicate `R`, remote work requires
  `D => R`; exactness additionally requires `R => D`. Every residual has one
  owner, and filter evaluation remains before local ordering, limit, or offset.
- Capability absence changes optimization only. Bind and planning remain
  network-free, plans remain immutable, and execution remains bounded,
  cancelable, sequential, credential-safe, and strictly typed.

### Team and RFC routing

- Accountable stream: Query Experience.
- Connector Experience — **Collaboration, then X-as-a-Service:** provide one
  immutable native predicate-to-input declaration and deterministic explanation.
  Exit when consumers use its public read-only API and do not infer mapping
  semantics from GitHub request construction.
- Relational Semantics — **Collaboration, then X-as-a-Service:** provide
  structured classification, implication evidence, input binding, residual
  ownership, conservative fallback, and complete immutable plans. Exit when
  property and counterexample oracles pass and consumers do not reclassify the
  plan.
- Remote Runtime — **Collaboration, then X-as-a-Service:** execute the selected
  typed request input without interpreting DuckDB predicates, while preserving
  authorization, pagination, budgets, cancellation, and close. Exit when
  DuckDB-free request and lifecycle evidence passes.
- RFC: required by public-behavior, shared-interface, relational-correctness,
  operation-selection, and explanation triggers. Acceptance requires product
  approval and affected-team reviews before implementation establishes the
  contract.

### Unknowns and first trial

- Decision-critical: whether pinned DuckDB exposes enough stable structured
  filter information to recognize only the approved predicate without SQL-text
  reconstruction and without transferring ownership prematurely.
- Resolved by product approval: expose the upstream `visibility` field and map
  only `visibility = 'private'` to `visibility=private`. This avoids treating
  the broader `private` boolean as a visibility category. The plan retains the
  DuckDB residual even though the same-field mapping is exact under the pinned
  endpoint contract.
- Trial: the RFC evidence will use pinned DuckDB and deterministic GitHub
  counterexamples. It is not a delivery; accepted behavior will be implemented
  in permanent team-owned modules without exposing an incomplete public path.

### Delivery path

1. Produce the four charter-owned plans and decision-critical RFC evidence;
   accept the RFC with product and affected-team review.
2. Implement the permanent Connector and Relational Semantics provider APIs and
   their independent semantic oracles.
3. Implement Runtime request execution and Query adapter/explain consumption
   against those APIs, then prove the controlled end-to-end result.
4. Propagate durable contracts, run independent review and the complete gates,
   audit interaction exits, commit the coherent goal, and record delivery
   evidence here.

## Responsibility and dependency map

| Workstream | Primary source ownership | Oracle ownership | Consumes | Provides | Interaction exit |
| --- | --- | --- | --- | --- | --- |
| [Connector Experience](predicate-selective-repository-scans/connector-experience/plan.md) | Native predicate mapping metadata and deterministic explanation | Catalog declaration, validation, and snapshot tests | Accepted RFC and native catalog conventions | Immutable predicate capability facts | Consumers use the public metadata API and do not infer mapping semantics |
| [Relational Semantics](predicate-selective-repository-scans/relational-semantics/plan.md) | Structured predicate model, classification, input binding, residual ownership, and plan explanation | Implication properties, counterexamples, capability fallback, and plan snapshots | Connector facts and Query request | Complete immutable `ScanPlan` | Query and Runtime do not reclassify relational meaning |
| [Remote Runtime](predicate-selective-repository-scans/remote-runtime/plan.md) | Typed request-input application and existing bounded execution lifecycle | DuckDB-free request, security, pagination, cancellation, and close tests | Complete `ScanPlan` and authorization capability | Bounded `BatchStream` | Query contains no request-input or transport policy internals |
| [Query Experience](predicate-selective-repository-scans/query-experience/plan.md) | Structured DuckDB filter conversion, capability reporting, local residual contract, and public explain/product evidence | Adapter, SQL, offline lifecycle, fallback, and black-box equivalence tests | Connector, Semantics, and Runtime provider APIs | User-visible predicate-selective query | Product narrative passes without cross-team internals |

Connector facts precede semantic classification. Once those interfaces are
accepted, Runtime request-input work and Query's structured-filter conversion
may proceed in parallel because they consume opposite ends of the immutable
plan. The lead agent owns the RFC integration, shared documentation, root build
registration, version and release records, final dependency audit, Git history,
and goal closure. No team edits another team's plan or duplicates a provider in
its consumer.
