# Query Experience plan: generated package relations

## Outcome and authority

Status: **In progress as of 2026-07-21; supporting stream.** The Relational
Semantics, Remote Runtime, and Engineering Enablement interactions are
confirmed exited to X-as-a-Service by source/test-dependency audit. The
Connector interaction is open:
`src/query/duckdb/typed_value_adapter.cpp`'s `ValueKindForLogicalType`
re-derives a type enum from Connector's `logical_type` string instead of
consuming a closed enum Connector already computes, duplicating the same
string-to-enum mapping across three layers — a `$contract-change`-scoped
decision (see [the goal's Completion record](../stable-local-connector-packages.md)).

Query Experience will project an accepted immutable package generation into
DuckDB exactly as approved by
[RFC 0012](../../../rfcs/0012-define-local-package-sql-registration.md):
generated relation functions, explicit load and reload, snapshot-aligned
introspection, atomic publication, and the one-release dispatcher migration.
Connector Experience remains accountable for the author outcome.

Query owns DuckDB names, typed named-argument binding, system-catalog
publication, collisions, catalog MVCC, management and introspection functions,
prepared/bound generation retention, user diagnostics, and database lifecycle
integration. It does not compile packages, apply defaults, select operations,
interpret predicates, admit requests, resolve credentials, or execute remote
protocols.

## Permanent service boundary

Query production is separated by reason to change:

- a catalog-generation coordinator for publication serialization, ownership,
  collision checks, system-catalog transactions, rollback, and close;
- a generated-relation adapter that consumes one structural descriptor and
  retains its opaque generation owner;
- management functions whose bind is offline and whose first materialized
  pull stages exactly one local operation;
- snapshot-aligned introspection functions; and
- a DatabaseInstance-owned lifecycle sentry.

The existing dispatcher callbacks become a reusable relation execution path;
the generated function's SQL name never becomes execution or semantic
authority. `ScanRequest` gains ordered structural values that distinguish
omission, present NULL, and present typed value. Defaults remain Connector
facts interpreted only by Semantics.

Publication follows one observable commit point: compile and stage outside the
lock; acquire a cancelable publication guard; reject stale state and every
case-insensitive table-function or macro collision; replace only owned names
inside one DuckDB system-catalog transaction; and hold the guard through
commit or rollback. Catalog entries retain the exact immutable generation or
registry snapshot they expose. Query never maintains a separately visible
mutable name registry.

## Acceptance evidence

Actual-DuckDB tests prove:

- the exact four generated GitHub functions, two management functions, three
  introspection functions, named types, result schemas, defaults/nullability
  metadata, deterministic inventory order, and absence of roots or secrets;
- offline bind, `DESCRIBE`, `EXPLAIN`, literal and deferred `PREPARE`, repeated
  execution, joins, materialization, export, filtering, ordering, and limits;
- `CALL`/`FROM` equivalence, relational demand, exact-once materialized work,
  multiple-invocation and explicit-transaction rejection, and cross-connection
  visibility;
- duplicate candidate, active owner, overload, table-macro, late conflict,
  concurrent publisher, cancellation, and failure after every staged catalog
  mutation with complete rollback;
- identical no-op and every accepted/rejected reload transition, old
  transaction/prepared/in-flight ownership, waiter cancellation, close,
  instance destruction, and unsupported DSO-unload boundaries; and
- dispatcher/generated catalog, plan, request, result, failure, and explanation
  differentials for all four relations while package inventory excludes the
  dispatcher.

The permanent coordinator target is part of the normal gate; RFC 0012's
`EXCLUDE_FROM_ALL` trial is feasibility evidence and is not copied as a fake
production registry.

## Dependencies and interaction exits

- **Connector Experience — X-as-a-Service.** Requires the immutable
  generation, narrow registration descriptors, compiler result, safe source
  diagnostic, and compatibility result. Exit when Query imports no YAML,
  schema, type-string, selector, protocol, or Connector-private API.
- **Relational Semantics — X-as-a-Service.** Requires typed
  `ScanRequest -> ScanPlan`, including defaults and operation selection. Exit
  when Query performs no eligibility or relational reasoning.
- **Remote Runtime — Collaboration to X-as-a-Service.** Requires immutable
  registry staging/retention and the generalized executor. Exit when Query
  carries only opaque handles and Runtime knows no DuckDB catalog API.
- **Engineering Enablement — Facilitation.** Exit when Query maintains the
  exact SQL inventory and migration oracle as an ordinary domain gate.

Query owns `src/query/**`, Query tests, and the typed request boundary. Shared
provider headers, root composition, version identities, and release gates are
serialized under the lead. Adjacent code documents pinned DuckDB coupling,
callback state, transaction and rollback invariants, lock lifetime, deferred
bind behavior, cancellation, close/destruction, and exception ownership.
