# Relational Semantics plan: immutable fixed scan plan

## Outcome and status

Own the offline semantic handoff for the first live REST proof. The completed
plan represents one full remote base-relation scan with a fixed non-null schema
and no claimed predicate, projection, ordering, limit, offset, pagination, or
retry capability.

## Relational-owned workstream

| Artifact | Responsibility |
| --- | --- |
| `src/include/live_rest/plan.hpp` | Meaning and immutable fields of the trial scan plan |
| `src/plan.cpp` | Sole offline construction path, fixed authority and URL, schema, capabilities, budgets, and safe snapshot |
| `test/plan_tests.cpp` | Exact snapshot, authority rejection, and absence of planning side effects |

The plan names `id BIGINT`, `login VARCHAR`, and `site_admin BOOLEAN` as strict
non-null outputs from the response item object. It makes no pushdown claim:
DuckDB owns every relational operator above the table function. Runtime may
validate the plan against its canonical snapshot but may not reclassify or
infer relational behavior from the URL or response.

## Dependencies and parallel boundary

Relational Semantics publishes the small plan header before Query Experience
and Remote Runtime begin implementation. After that handoff, this team edits
only plan construction and plan tests. Query owns DuckDB bind/copy behavior;
Remote owns execution and resource enforcement. A request to add a pushed
predicate, ordering, limit, projection, or cardinality-changing provider would
be new semantic work rather than an adapter or transport tweak.

## Acceptance evidence

- Plan construction accepts only the exact public authority and the explicit
  numeric loopback test authority.
- The snapshot contains every schema, capability, and resource invariant but
  no network-derived value.
- Bind can copy the complete plan for optimizer and prepared-statement paths.
- Runtime canonicalization rejects any mutated URL, capability, schema, or
  budget before acquiring network authority.
- DuckDB applies relational operators locally because the plan declares no
  remote pushdown capability.

## Interaction and RFC exit

The Query and Remote Runtime collaborations remain open while the plan is
trial evidence. They exit only after the promotion RFC accepts a durable
`ScanRequest -> ScanPlan` responsibility boundary and both consumers execute
it without duplicating semantic decisions. Connector authoring is not involved
in this workstream; a later compiler must target the proven plan contract, not
define it retroactively.
