# Relational Semantics plan: package-independent scan planning

## Outcome and authority

Status: **Planned; supporting subsystem, provider interaction in
Collaboration**.

Relational Semantics will turn generalized compiled package facts and a typed
`ScanRequest` into the same conservative immutable `ScanPlan` regardless of
whether metadata came from the native catalog or `duckdb_api/v1`. The team
alone owns input resolution, default application, operation eligibility and
selection, predicate proof use, projection closure, budget intersection, and
residual ownership.

It does not parse package source, derive meaning from generated SQL names,
bind DuckDB arguments, construct requests, resolve credentials, or execute a
protocol.

## Permanent service boundary

The planner accepts structural typed values with three distinct states:
omitted, present NULL, and present value. It resolves operation-independent
explicit values first and applies a compiled default only where no non-default
relation-input source exists. For each operation it separately derives only
that operation's proved conditional predicate bindings. Relation-input and
operation-local conditional namespaces remain structurally distinct; neither
is inferred from a string. Only then does the planner determine eligibility,
fallback, ranking, and ambiguity. RFC 0013 narrows RFC 0012's future connection,
partition, provider, and author-constant placeholders; those unreachable
sources do not enter the v1 API.

The closed plan copies every Runtime-authoritative fact into immutable typed
values. Connector identity, relation name, package version, source location,
and explanation text are never execution-profile shortcuts. Native metadata
and package generations travel through one semantic model and one law suite.

Responsibilities remain separated between typed input resolution, operation
selection, predicate implication/classification, conservative projection and
ownership, plan construction/validation, and safe fixtures. No module may
reconstruct package syntax or couple all decisions to a fixed GitHub profile.
`input_resolution` and `operation_selection` remain internal Semantics modules;
the public surface stays the typed planning service and immutable plan value.

## Acceptance evidence

The deterministic law matrix proves:

- omission/value/NULL/default behavior for `BOOLEAN`, `BIGINT`, and `VARCHAR`,
  including nullable defaults, non-null NULL rejection, conflict precedence,
  and operation-local eligibility;
- required-input selectors, fallback, unique winner, tie, ambiguity, missing
  input, and no operation, without Query binder inference;
- exact and superset implication, false/NULL counterexamples, duplicates and
  occurrence preservation, unknown restricted rows, changed rows, exact extra
  rows, lost true rows, and mandatory DuckDB residual retention;
- projection closure, full local ownership of unproved filtering, ordering,
  limit and offset, cardinality preservation, checked resource intersections,
  and conservative behavior for missing DuckDB structure; and
- structural equality of native and package plans for all four GitHub
  relations plus a controlled package that exercises every input and selector
  state.

Focused Semantics fixtures expose only immutable valid plans and explicit
invalid executable-boundary cases. Consumers link the fixture service; they do
not import private plan builders or compile Semantics sources directly.

## Dependencies and interaction exits

- **Connector Experience — Collaboration to X-as-a-Service.** Exit when
  Semantics consumes generalized compiled facts through one public service and
  no longer branches on native origin, connector ID, package version, raw type
  or extractor spelling, or a fixed GraphQL identity.
- **Query Experience — X-as-a-Service.** Query supplies only the complete
  typed request and consumes only the final plan or structured planning error.
  Exit when generated and dispatcher plans are equivalent without SQL-name
  interpretation.
- **Remote Runtime — X-as-a-Service.** Runtime consumes the immutable plan and
  fixture service. Exit when it neither reclassifies predicates nor imports a
  planner/Connector dependency.

Semantics owns `src/semantics/**`, the plan construction facade and its focused
tests. Shared plan/request representation changes serialize with their
provider and consumers. Adjacent documentation states semantic authority,
input precedence, implication laws, residual ownership, immutability,
resource arithmetic, error ownership, and the absence of I/O.
