# Remote Runtime plan: immutable package-generation execution

## Outcome and authority

Status: **In progress as of 2026-07-21; supporting platform.** The Relational
Semantics and Query interactions are confirmed exited to X-as-a-Service by
source/test-dependency audit. The Connector interaction is open pending
confirmation: the generation owner is opaque throughout the real
`Open()`/execution path, but `src/query/package_generation_composition.cpp`
downcasts it back to a concrete Connector type for reload. This is confined
to the lead-owned composition root and may be an accepted exception rather
than a defect — not yet confirmed against `docs/ARCHITECTURE.md` (see
[the goal's Completion record](../stable-local-connector-packages.md)).

Remote Runtime will execute package-derived plans through the existing bounded
stream without admitting by connector ID, relation name, package version, or
source provenance. It will also retain immutable active-generation snapshots
for the coordinator while old transactions, prepared plans, and scans still
own them.

Runtime owns plan admission, request profiles, credential materialization,
network policy, transport, decoding, pagination, resource accounting,
cancellation, stream lifecycle, generation staging/retention, close, and
shutdown. It does not read packages, inspect DuckDB catalogs, select
operations, classify predicates, or derive request authority from explanation
text.

## Permanent service boundary

Runtime provides:

```text
StageGeneration(compiled_generation, active_snapshot, cancellation)
    -> immutable staged generation and registry snapshot

Open(scan_plan, moved_authorization, execution_control)
    -> bounded cancelable BatchStream
```

The registry service owns canonical roots and opaque generation retention but
does not publish DuckDB objects. Query supplies the catalog-selected opaque
registry snapshot; Runtime never interprets a DuckDB timestamp or looks up a
newer generation by name during execution.

Admission validates a complete closed execution profile before credential
materialization or transport. The profile is derived from typed compiled plan
facts and content identity, not repository-specific identities. REST and
GraphQL request construction, response decoding, pagination, and accounting
remain separate responsibilities with one primary reason to change.

## Acceptance evidence

Runtime evidence proves:

- anonymous and bearer package plans for all accepted REST and GraphQL shapes,
  including fixed/input/conditional query fields, form encoding, root object,
  root array, terminal collections, Link next targets, generated GraphQL
  bodies, and cursor progression;
- plan/name/version/source invariance: equivalent native and package plans
  admit to identical request profiles, while any changed executable fact is
  either independently supported or rejected before side effects;
- host and connector policy intersection, exact origin and credential
  placement, no proxy/redirect/cookie/netrc widening, redaction, strict typed
  conversion, and zero credential or transport observation on failed
  admission;
- checked page/scan/string/document/body budgets, sequential backpressure,
  actual-use accounting, one-attempt replay, cancellation at every boundary,
  terminal late failure, early close, repeated close, and stream isolation;
  and
- atomic staged-generation replacement, exact no-op, candidate discard,
  old-owner retention, stale publication rejection, queued publication
  cancellation, close draining, final release, and DatabaseInstance shutdown.

Controlled fixture execution is a bounded Runtime service consumed by
Connector's fixture runner. Runtime owns request/body/result observations;
Connector owns fixture coverage and author reporting.

## Dependencies and interaction exits

- **Relational Semantics — Collaboration to X-as-a-Service.** Requires a
  complete immutable plan and closed fixture variants. Exit when Runtime links
  only plan services and never reclassifies or imports private construction.
- **Connector Experience — indirect X-as-a-Service.** Runtime receives no
  package source or compiled Connector object in execution. The coordinator
  may retain an opaque generation owner but cannot inspect compiler internals.
- **Query Experience — Collaboration to X-as-a-Service.** Query coordinates
  publication and passes catalog-selected opaque snapshots. Exit when Runtime
  contains no DuckDB catalog dependency and Query contains no registry
  internals.

Runtime owns `src/runtime/**`, Runtime tests, and the registry/executor service
headers. Shared plan and generation handles are frozen before parallel edits.
Adjacent documentation covers validation-before-side-effects, authorization
ownership, immutability, concurrency, cancellation, resources, terminal
failure, close/destruction, and exception containment.
