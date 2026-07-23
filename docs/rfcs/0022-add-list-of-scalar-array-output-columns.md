# RFC 0022: Add list-of-scalar ARRAY output columns to duckdb_api/v1

```yaml
rfc: "0022"
title: "Add list-of-scalar ARRAY output columns to duckdb_api/v1"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Connector Experience"
technical_decision_owner: "Lead agent"
product_approver: "Nic Galluzzo"
authors:
  - "Lead agent"
required_reviewers:
  - "Connector Experience"
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
affected_teams:
  - "Connector Experience"
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
linked_outcome_or_objective: "Active product goal: a connector author can expose a JSON array field as one native DuckDB list-valued output column; the repository Rick and Morty character_search relation proves the outcome with its episode URL array."
supersedes: "Not applicable"
```

## Summary

Add one output-column-only collection shape, `ARRAY`, whose elements have one
declared existing `duckdb_api/v1` scalar type. An author declares `type:
ARRAY`, `element_type`, and `element_nullable`; the existing `nullable` field
continues to govern the whole value. A present JSON array becomes one native
variable-length DuckDB `LIST` value, retaining source order, empty arrays,
NULL elements when declared, and base-row cardinality. Objects, nested arrays,
list-valued inputs or predicates, and implicit type inference remain excluded.

## Sponsorship and context

- **RFC type:** Product. This changes the public connector-package column
  grammar and the SQL-visible output type.
- **Sponsoring team:** Connector Experience, which owns the path from an API
  definition to a validated, testable connector package.
- **Linked outcome or objective:** A connector author can represent common
  JSON arrays as columns. The first end-to-end proof adds the real `episode`
  array already present in the repository's Rick and Morty response fixtures
  to `character_search`.
- **Why now:** `duckdb_api/v1` currently requires every output cell to be one
  scalar. The Rick and Morty package therefore omits a real upstream field
  even though its checked-in fixtures already contain ordered URL arrays.
  Tags, categories, coordinate collections, and other multivalue fields are
  common among the free and hobby APIs relevant to the ten-provider `1.0.0`
  gate, making this a higher-leverage gap than many provider-specific features.

## Problem

The public column grammar is exactly:

```yaml
- id: name
  type: BOOLEAN | BIGINT | VARCHAR | DOUBLE
  nullable: false
  extract: $.name
```

The schema, compiler, immutable column model, `ScanPlan`, Runtime typed rows,
fixture expected cells, and Query vector writer all assume one scalar payload
per output cell. A JSON value such as:

```json
{"episode":["https://rickandmortyapi.com/api/episode/1"]}
```

cannot be declared as `VARCHAR`: strict decoding correctly rejects an array
where a string was declared. Encoding the raw JSON as a string would discard
native list semantics and require upstream stringification that the real API
does not perform. Exploding the array would change base-row cardinality and is
not an output-column type.

This is not a defect in the current implementation. RFC 0013 deliberately
closed v1 around scalar columns, and RFC 0020 added a fourth scalar without
creating a structural type system. This RFC makes the first bounded structural
extension.

## Decision drivers and invariants

- **Must preserve:** static author-declared schemas; deterministic offline
  validation, bind, and planning; immutable compiled generations and plans;
  strict existing scalar conversion; source array order; base-row cardinality;
  complete-page failure on schema drift; redacted structural diagnostics;
  bounded memory, string bytes, response bytes, time, cancellation, and
  backpressure; and conservative behavior for unsupported protocol profiles.
- **Must enable:** homogeneous variable-length lists of `BOOLEAN`, `BIGINT`,
  `VARCHAR`, or `DOUBLE` values as output columns, including empty lists and a
  precise independent policy for whole-list and element NULLs.
- **Must not introduce:** objects, structs, maps, arbitrary JSON, nested lists,
  fixed-length DuckDB arrays, dynamic schema inference, row explosion,
  list-valued relation inputs/defaults/predicate literals, remote list
  predicates, or new protocol scalar capabilities.

## Proposed decision

### Public behavior

An array output column has this exact source shape:

```yaml
columns:
  - id: episode
    type: ARRAY
    element_type: VARCHAR
    element_nullable: false
    nullable: false
    extract: $.episode
```

`type: ARRAY` is admitted only for output columns. `element_type` is required
and is exactly one of the existing scalar values `BOOLEAN`, `BIGINT`,
`VARCHAR`, or `DOUBLE`. `element_nullable` is a required failsafe-YAML Boolean.
Both fields are unknown and rejected on a scalar column. `ARRAY` remains
invalid for relation inputs, defaults, predicate literals, and conditional
inputs.

The author spelling `ARRAY` describes the JSON collection shape. DuckDB sees a
variable-length `LIST`, rendered using DuckDB's native child-type spelling such
as `VARCHAR[]`; this RFC does not expose DuckDB's distinct fixed-length
`ARRAY(child, length)` type and declares no length.

The existing `nullable` field governs the whole extracted value:

- a missing selected field remains a schema failure for every column, matching
  the existing scalar decoder contract;
- JSON null fails when `nullable: false` and produces a NULL list when
  `nullable: true`; and
- a present empty JSON array always produces a valid empty list, distinct from
  a NULL list.

`element_nullable` independently governs JSON null inside a present array:

- `false` rejects any NULL element as a Runtime `schema` failure on the column;
- `true` retains each NULL at its exact position as a typed DuckDB NULL child;
  and
- non-NULL elements must strictly match `element_type`, including the existing
  lossless `BIGINT` and finite-range `DOUBLE` rules.

Element order and duplicates are preserved. The adapter produces exactly one
list cell for one source record; it never unnests, sorts, deduplicates, or drops
elements. Any object, nested array, or incompatible scalar element fails the
page and scan before that page is published. Diagnostics name only the safe
column identifier and stage; response values and child contents remain
redacted.

The structural collection shape is protocol-neutral, but it cannot widen a
protocol's scalar support. REST accepts all four element types. GraphQL accepts
ARRAY columns for its currently admitted `STRING`/`INT64`/`BOOLEAN` response
scalars, corresponding to `VARCHAR`/`BIGINT`/`BOOLEAN`. A GraphQL ARRAY with
`element_type: DOUBLE` fails compilation at the element-type source location,
matching RFC 0020's existing rejection of scalar GraphQL `DOUBLE`; GraphQL
Float support remains a separate decision.

Offline fixture expected rows represent a valid array cell as a YAML sequence
in source order. Scalar entries use the same typed scalar grammar as existing
cells, and a NULL element uses `{kind: null}`. A whole NULL list remains the
existing cell-level `{kind: null}`. Fixture parsing validates the sequence
against the compiled element type and element nullability; it never infers a
column type from fixture syntax.

The change is additive for existing valid packages. The exact source accepted
before this RFC remains valid with identical meaning; only packages that opt
into the new closed column alternative use ARRAY. RFC 0013 requires a new exact
spec identifier for an *incompatible* author grammar. This RFC deliberately
classifies ARRAY as a compatible, opt-in pre-`1.0.0` extension to
`duckdb_api/v1`, following RFC 0020's accepted addition of `DOUBLE` to the same
closed v1 set. Keeping v1 rather than introducing `duckdb_api/v2` is a reserved
product decision and is not implied merely by implementation feasibility.

The Rick and Morty package adds a trailing `episode` column to an existing
relation, which is an intentional incompatible package-schema change and
therefore advances that connector's own major SemVer. Existing loaded
generations and prepared plans remain immutable; reload from the old Rick and
Morty generation rejects the incompatible descriptor rather than changing an
active schema in place.

### Shared interfaces

Connector Experience replaces the assumption that every `CompiledColumn` is a
scalar with an explicit immutable column type containing:

```text
CompiledColumnType
├── shape: SCALAR | ARRAY
├── scalar_or_element_type: BOOLEAN | BIGINT | VARCHAR | DOUBLE
└── element_nullable: false for SCALAR; declared value for ARRAY
```

The type is built only after closed schema validation. `logical_type` remains
a derived explanation/compatibility spelling (`VARCHAR` or `VARCHAR[]`), never
authority. Extractor segments, outer nullability, and declaration order remain
unchanged. Compatibility compares shape, scalar/element type, element
nullability, outer nullability, and extractor exactly. Source-safe compiled
schema explanation exposes the derived list type and both nullability levels;
it never includes response values or relies on the derived spelling as
authority.

The Connector-owned `CompiledQueryRegistrationView` publication boundary
carries the same immutable structural output facts in every
`CompiledRegistrationColumn`: shape, scalar/element type, element nullability,
outer nullability, and derived logical type. Query never recovers structure by
parsing `logical_type` or reaches back into `CompiledColumn`. The focused
Connector provider target proves exact publication; a focused Query consumer
target proves ARRAY registration and bind through this bounded view without
compiling Connector implementation sources.

Relational Semantics carries a distinct, protocol-neutral planned column type
with the same three facts into each `PlannedColumn`. It does not parse the
logical-type string, infer a list predicate, or change projection/residual
ownership. ARRAY columns are ineligible for the scalar equality-predicate
mapping admitted by v1; an offered DuckDB predicate over a list remains wholly
DuckDB-owned and does not make another operation eligible.

REST and GraphQL compiled/planned result-column values carry shape, element
kind, outer nullability, element nullability, and structural response path.
Protocol admission independently correlates those facts with the relation's
planned output schema. Runtime receives no Connector-private object and does
not reinterpret a type string.

Remote Runtime's `TypedBatch` column schema changes from a vector of scalar
kinds to a vector of immutable structural value types. One Runtime value is
either:

- a scalar value with one existing typed scalar payload; or
- an ARRAY value with validity for the whole list and an ordered vector of
  typed scalar elements, each with independent validity.

The element representation cannot contain another collection. Factories and
consumer validation require every valid list element to match the column's
declared element kind, require NULL elements to be declared nullable, and
require inactive scalar payloads and collection storage to be empty. A batch
with shape, kind, arity, NULL, or nested-state drift is an internal provider
contract failure before Query changes its `DataChunk`.

Query Experience maps a planned ARRAY column to
`duckdb::LogicalType::LIST(child_type)`. It validates the complete Runtime
batch first, computes child cardinality with checked arithmetic, reserves the
list child vector before publishing cardinality, writes the parent
`list_entry_t` values, and applies parent and child validity separately. The
writer checks the call-scoped cancellation state between bounded groups of
child values so one large list cannot make Query's copy phase uninterruptible.
Runtime's wall-time deadline remains Runtime-owned and is checked during
decode; the finite decoded-memory and batch authority bounds Query's admitted
copy work. Query owns the only DuckDB `ListVector` writes; Runtime remains
DuckDB-free.

Fixture services extend expected cells and controlled Runtime observations
through their existing bounded public test interfaces. Focused consumer tests
must not compile provider production sources or import provider-private
builders.

### Operational behavior

No new author resource field is added. The existing effective plan and host
ceilings already bound response bytes, decompressed bytes, extracted string
bytes, JSON nesting, decoded retained memory, records, wall time, and batch
rows. Runtime must charge:

- array element vector capacity and each retained scalar payload against
  `decoded_memory_bytes` before the allocation or append;
- every contained VARCHAR value against the existing per-extracted-string
  ceiling, and its retained bytes against decoded memory using checked
  addition;
- all Runtime-owned parent/list, child-vector, structural schema, decoded-page,
  and handed-off batch capacity against retained decoded memory, including
  storage that is temporarily co-live during transfer; and
- parsing work against the existing response-byte and wall-time ceilings with
  cancellation checkpoints during array traversal.

The number of elements is therefore bounded by both wire bytes and the
explicit decoded-memory ceiling; zero never means unlimited. All count,
capacity, offset, and byte arithmetic uses checked unsigned operations and
rejects before state mutation or allocation when the next value would exceed
authority. No separate author-chosen element-count ceiling is needed for this
first shape.

The decoder debits row-slot and typed-value staging before those allocations
while retained child storage is live and exposes the observed decode peak
separately from retained page bytes. Executor handoff preflights copied schema
and outer-row-vector capacity against the same admitted page allowance while
the decoded page remains live. Permanent REST plans carry an explicit
structural-schema authority mode; an empty structural result schema is invalid
and cannot silently select the legacy compatibility path.

Retained GraphQL strings debit before each possible capacity growth. Temporary
REST body-continuation and GraphQL end-cursor storage contributes to the decode
peak, is separated from retained row bytes, and transfers or releases at the
pagination boundary. A moved cursor is counted once in cursor state rather
than once in both the decoded page and cursor owner.

Decoding remains single-pass for selected row data and validates the complete
JSON document. One request and one decoded page remain active; decoded array
state is released with its page before pagination advances. Cancellation,
terminal failure, early close, and destruction release parent and child
storage through the existing idempotent cleanup path. ARRAY support grants no
network, authentication, pagination, retry, replay, cache, concurrency, or
provider authority.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Connector Experience | Sponsor and author-workflow owner | Column grammar, schema diagnostics, immutable column type, compatibility, expected fixture cells, coverage derivation, Rick and Morty package | Collaboration, then X-as-a-Service | Authors can declare, validate, fixture-test, explain, and maintain ARRAY columns without Runtime or DuckDB internals |
| Relational Semantics | Planned-schema provider | Protocol-neutral planned column type, scalar-predicate exclusion, plan/result-column correlation, cardinality proof | Collaboration, then X-as-a-Service | Deterministic planning oracles preserve shape, projection, residual ownership, and one base row without consumers reinterpreting types |
| Remote Runtime | Nested-value service provider | Strict REST/GraphQL array decoding, structural typed batches, checked child resource accounting, cancellation and close | Collaboration, then X-as-a-Service | Independent decode/stream fixtures prove success and failures; Query consumes only the documented typed-value service |
| Query Experience | DuckDB-facing consumer | LIST registration, parent/child validity, checked vector writing, SQL-visible diagnostics and lifecycle | Collaboration | One real author-to-query fixture produces native lists and meaningful failures without Connector or Runtime internals in the adapter |
| Engineering Enablement | Evidence facilitator | Closed array fixture variants and compatibility-freeze mutation checks | Facilitation | Domain teams run and maintain their own ARRAY gates without Enablement approval |

No accountability boundary moves. The change makes existing team interfaces
structural rather than scalar-only, but retains the same provider-consumer
direction. Collaboration remains open until focused targets consume bounded
provider APIs and the end-to-end Rick and Morty proof passes.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** Affected. One JSON
  record remains one base row regardless of list length. ARRAY columns cannot
  participate in scalar predicate mappings, operation eligibility, or remote
  equality pushdown. DuckDB retains list predicates and all existing residual,
  ordering, limit, and offset ownership.
- **Authentication, credentials, network policy, and privacy:** Not affected.
  Lists are response values, never authority. Child values remain absent from
  diagnostics, explain, digests, and request construction.
- **Resource budgets, backpressure, and cancellation:** Affected. Every child
  and vector capacity is charged before retention; contained strings charge
  the same byte ledger as scalar strings. Array traversal checkpoints
  cancellation/deadline. Page-at-a-time release and pull backpressure remain.
- **Replay, retries, caching, and duplicate prevention:** Not affected.
  Element duplicates are data and are preserved; request replay and cache
  states stay disabled.
- **Concurrency, immutability, and state ownership:** Affected only in value
  shape. Compiled/planned types and completed batches own immutable copies;
  each stream retains isolated mutable decode state. No child references point
  into response buffers after decode.
- **FFI, initialization, reload, shutdown, and failure containment:** The
  Query vector writer is affected, but the extension boundary and callback
  lifecycle do not change. Complete-batch validation precedes output mutation;
  errors cross the existing DuckDB exception boundary. Incompatible package
  reload publishes nothing.
- **Diagnostics, redaction, metrics, and progress reporting:** Affected only by
  new structural failure categories. Errors identify the column and stable
  stage without values or array contents. Progress remains record/page based;
  no element count is presented as row progress.

## Compatibility and migration

Existing packages using scalar columns require no migration and retain
byte-for-byte logical behavior. The scalar column source form is unchanged,
and `element_type`/`element_nullable` are rejected there rather than silently
ignored. Unknown type spellings continue to fail closed.

`ARRAY` is an additive pre-`1.0.0` connector-language capability and requires a
project minor release classification. The `1.0.0` candidate freeze retains
`scalar_types.authored = {BOOLEAN, BIGINT, VARCHAR, DOUBLE}` and adds a
separate closed output-collection authority for `ARRAY`, its admitted element
types, and required element-nullability declaration. ARRAY is not mislabeled
as a fifth scalar.

RFC 0013's content-addressed evidence is immutable historical decision
evidence: its accepted files and digests under `docs/rfcs/evidence/0013/` must
not be rewritten by later contract propagation.
This RFC creates a new `docs/rfcs/evidence/0022/` authority containing the
complete post-decision connector-package schema, fixture-index schema,
fixture-coverage mapping, and digest manifest. Accepted production assets are
byte-copied from that 0022 evidence, and source-identity tests move their
current-authority comparison from 0013 to 0022 while continuing to verify the
0013 historical manifest independently. This is an amendment to the current
v1 contract, not a retroactive claim that RFC 0013 originally decided ARRAY.

Engineering Enablement's review discovered that the baseline violated that
premise before this proposal: later deliveries had edited nine 0013 evidence
files, inserted four unmanifested payloads, removed its accepted verifier, and
left the source-identity mutation suite on `0.9.0` after the authority advanced
to `0.10.0`. This RFC's pre-decision containment restores the 0013 directory
and verifier byte-for-byte from its accepted `ffbe28f` snapshot, including the
RFC-recorded manifest digest
`sha256.49407e412bd0863fd9d14d881e067e3653bd66dc39e901bf8cceb7f76888f128`.
It adds `scripts/rfc_evidence_authorities.py`, whose manifest digests live
outside both mutable manifests, and `scripts/verify-rfc-evidence.py`, which
checks exact regular-file inventory, manifest identity, artifact bytes,
historic verifier identity, and the selected current production mirrors. The
mutation suite covers byte change, add/remove/rename, joint manifest re-pin,
historic verifier drift, and production/current-authority mismatch; the
repaired `test/python/source_identity_contract.py` runs against `0.10.0` while
retaining `0.9.0` as history. These become always-run product/release-evidence
gates in `AGENTS.md`.

The proposed 0022 manifest is externally anchored at
`sha256.f55f12c6550a21778d6177a64da82ae26848a3d8654bf1be435157cb7aa56f2b`.
It binds the amended connector schema at
`sha256.589774ff75876c13d6bd52a243fd470c172e67d8121bc6ba8f11e2af0b451d41`,
fixture-index schema at
`sha256.fae0e6f22ca0c8ed2ea8ffae61a4ac7d982907be1ce033270a93f0024cc04841`,
and fixture-coverage mapping at
`sha256.6107a7131196bfcfb359b410b10f28c2f20f3ca3ddbc690bc72b874cd744e5da`.
The mapping requires structural success and failure variants per relation
operation, so independent REST and GraphQL decoders cannot satisfy it with a
REST-only negative matrix.

The Rick and Morty source package advances its package major version because
adding `episode` changes an existing relation schema. A database with the old
generation active cannot reload this incompatible schema in place; a new
database can load the new package. Bound and running owners of an old
generation remain valid until released.

Rollback before publication is removal of the new syntax and implementation.
After publication, removing ARRAY or changing its NULL/type semantics is a
public connector-contract break requiring a superseding RFC and migration.
Unsupported GraphQL DOUBLE collection profiles fail during compilation and
produce no executable generation.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| The pinned DuckDB cell natively supports variable-length lists and parent/child vector validity | Primary-source capability evidence | Inspect pinned DuckDB `LogicalType::LIST`, `ListVector::{Reserve,SetListSize,GetEntry}`, `list_entry_t`, and validity APIs; compile a focused Query writer test | Confirmed: focused Query tests write parent/child validity, duplicates, empty and NULL lists directly through `ListVector`; cancellation leaves output cardinality unpublished |
| One flat structural type is sufficient without a recursive general type tree | Contract and counterexample review | Schema alternatives plus negative fixtures for objects/nested arrays | Confirmed by all affected-team reviews and compiler/Runtime counterexamples; no recursive value type or nested-list state is admitted |
| Existing budgets can bound child count and bytes without a new author field | Boundary evidence | Exact decoded-memory and extracted-string boundary/one-over Runtime fixtures, including many NULL/empty-string elements | Confirmed: REST and GraphQL debit child, retained-string, and row-staging capacity before allocation, expose the observed co-live decode peak separately from retained page bytes, and reject one byte below that peak; response-next storage is included; the production GraphQL stream counts moved cursor storage once; batch handoff independently accepts its exact co-live page/schema/row-vector sum and rejects one byte below it; cancellation and late invalid elements publish no page |
| Protocol-neutral ARRAY can reuse current GraphQL scalar capability limits | REST/GraphQL differential | Same BOOLEAN/BIGINT/VARCHAR list fixture through both decoders; compile-time GraphQL ARRAY<DOUBLE> rejection | Confirmed: both decoders preserve the three shared element kinds; REST additionally admits DOUBLE and GraphQL ARRAY<DOUBLE> fails at compilation |
| Rick and Morty episode proves author and user value | Deterministic end-to-end fixture | Updated relation/index plus a controlled Runtime response and real DuckDB SQL | Confirmed: package `2.0.0` publishes trailing `episode VARCHAR[]`; real DuckDB DESCRIBE, list indexing, empty lists, ordered duplicates, filter, ordering, nonzero offset, and limit pass through Connector, Semantics, Runtime, and Query. The checked-in package fixture index is schema/coverage/digest evidence, not the executed response source; real built-in package fixture execution remains the pre-existing `release/1.0.0/freeze.json` fast follow. |
| Historical and amended RFC evidence cannot be silently re-pinned | Externally anchored identity and mutation evidence | Restored RFC 0013 verifier; `scripts/verify-rfc-evidence.py`; `test/python/rfc_evidence_identity_tests.py`; repaired `test/python/source_identity_contract.py` | Confirmed: both manifest generations pass; byte, inventory, re-pin, historic-verifier, production-mirror, and current-source mutations fail closed |
| The proposed schemas remain backward-compatible while closing ARRAY branches | Candidate-schema validation using the accepted RFC 0013 failsafe parser and schema-subset validator | Validate all eight checked-in connector/relation sources and both fixture indexes; admit the proposed ARRAY column and flat scalar/NULL sequence cells; reject missing/invalid element types, collection-only fields on scalars, nested element types, and nested/object cells | Confirmed: production assets are exact RFC 0022 byte mirrors; native compiler diagnostics and compatibility mutation tests pass while unchanged scalar packages retain their behavior |

No bounded trial must precede the RFC decision: the pinned DuckDB list API and
the motivating response data already exist. After acceptance, the first thin
delivery trial carries the Rick and Morty `episode VARCHAR[]` field through
schema, compilation, planning, REST decode, typed batch, and real DuckDB SQL
before generalizing the oracle matrix.

## Alternatives considered

1. **`type: ARRAY` plus required `element_type` and `element_nullable`
   (proposed).** Keeps existing scalar source unchanged, makes both nullability
   levels explicit, and remains a closed non-recursive schema. It adds two
   fields to array columns and requires an explicit schema branch.
2. **Use a DuckDB-like scalar spelling such as `type: VARCHAR[]`.** Compact and
   familiar to DuckDB users, but requires parsing a type mini-language,
   obscures element nullability, and creates pressure for nesting or arbitrary
   DuckDB types. Rejected.
3. **Make `type` a recursive object, for example `{kind: array, items: ...}`.**
   Extensible to future structs/nesting, but changes the source shape of every
   column or creates two parallel grammars and establishes a recursive type
   system the approved outcome explicitly excludes. Rejected.
4. **Allow arrays only for REST.** Narrower implementation, but makes the
   protocol-neutral output schema unnecessarily protocol-dependent. Rejected
   for scalar types GraphQL already supports; GraphQL DOUBLE remains rejected
   because that scalar capability is independently absent.
5. **Always allow NULL elements without declaring them.** Simpler syntax but
   weakens the static schema and prevents authors from detecting upstream
   drift. Rejected in favor of required `element_nullable`.
6. **Always reject NULL elements.** Simpler Runtime invariant but cannot
   represent common GraphQL/JSON lists containing typed NULL positions and
   needlessly narrows DuckDB's native list behavior. Rejected.
7. **Expose raw JSON as VARCHAR or JSON.** Avoids nested typed values but does
   not solve the requested native list capability and permits a broader
   arbitrary-JSON contract. Rejected.
8. **Unnest arrays into rows.** Useful as a separate relational operation, but
   changes cardinality and limit/filter semantics and is not an output-column
   type. Rejected.
9. **Retain scalar-only behavior.** Leaves a field in the repository's second
   connector unrepresentable and preserves a broad provider blocker. Rejected.

## Drawbacks and failure modes

- This is wider than RFC 0020's scalar addition because shared value schemas
  become structural. Missing a shape or element-nullability comparison at any
  layer could accept a plan/batch that another layer interprets differently.
  Explicit structural types and cross-layer counterexamples are required.
- Nested child storage makes decoded-memory accounting and Query list offsets
  more complex. Capacity growth, integer overflow, or partial `DataChunk`
  mutation are the main Runtime/Query implementation hazards.
- Supporting GraphQL lists requires extending its independent result-column
  and decoder paths. ARRAY<DOUBLE> remains unavailable there, reflecting the
  existing GraphQL Float exclusion rather than silently widening it.
- Fixture expected cells and coverage mutation services must understand
  sequences and element NULLs without becoming a general YAML/JSON value tree.
- Adding `episode` changes the bundled Rick and Morty relation schema and its
  package major version. That is deliberate user value but incompatible with
  in-place reload from its old generation.
- Always requiring `element_nullable` is more verbose than assuming a default,
  but prevents an omitted policy from changing meaning in a future release.

## Acceptance and verification

- **End-to-end demonstration:** Load the updated Rick and Morty package and
  query `rickandmorty_character_search`; `episode` is `VARCHAR[]`, each row
  contains the fixture's ordered URL list, and row count is unchanged. A
  controlled invalid element produces a redacted schema error before the page
  is published.
- **Automated oracle:** closed schema/compiler tests for scalar-vs-array field
  alternatives; compiled/registration/planned type and compatibility tests;
  focused Connector-provider and Query-consumer registration tests; fixture index
  sequences; all four REST element kinds; GraphQL BOOLEAN/BIGINT/VARCHAR and
  ARRAY<DOUBLE> rejection; empty, singleton, ordered duplicate, whole NULL,
  nullable/nonnullable element NULL; outer-type, mixed-type, object, nested,
  numeric conversion, string-byte, decoded-memory, overflow, cancellation,
  repeated pull, close, and incompatibility cases; scalar/list planning and
  predicate counterexamples; Query parent/child validity and actual LIST
  vector tests; existing package compatibility.
- **Quality gates:** `python3 -I -B scripts/validate-agent-assets.py`, public
  surface and contract-freeze verifiers/tests, `git diff --check`, cached diff
  check after staging, `make build`, `make test`, `make demo`,
  `scripts/verify-source-identities.py`, native-dependency tests, and a fresh
  `scripts/run-native-product-tests.sh` build root.
- **Independent review:** Required topology review from all five affected
  perspectives before acceptance; implementation-time `$adversarial-review`
  with independent relational/resource-lifecycle, Runtime/Query nested-value,
  and test-oracle perspectives.
- **Interaction exit:** Focused provider and consumer targets pass through
  bounded documented APIs; no consumer compiles provider sources, imports
  private builders, parses provider type strings, or reclassifies another
  team's semantics. The exact committed tree passes provider and consumer
  tests plus the end-to-end integration target.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Replace scalar-only output/value statements with the flat structural ARRAY contract, cardinality, and ownership | Complete; architecture names the flat shape, cardinality preservation, provider boundaries, and Query LIST ownership |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected | Define column branches, two nullability levels, protocol support, fixture syntax, compatibility, and exclusions | Complete; schema/compiler tests cover both branches and GraphQL's DOUBLE exclusion |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Define compiled/planned structural types, nested typed batches, strict decode, budgets, cancellation, and Query list writing | Complete; REST/GraphQL decode, resource, cancellation, batch-alignment, and ListVector tests pass |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | No accountability or boundary move; existing interfaces are extended in place | Complete; focused provider/consumer targets use bounded documented APIs, provider production sources remain private, and whole-graph composition is confined to named integration targets |
| `docs/PRODUCT_DELIVERY.md` and skills | Not affected | Existing delivery and contract-change rules already govern the work | Not applicable |
| `AGENTS.md` | Affected by evidence containment | Add the multi-generation RFC evidence verifier and both identity mutation suites to the authoritative product/release-evidence gates | Implemented before decision; independent review confirmed the commands pass |
| `ROADMAP.md` and `CHANGELOG.md` | Affected | Record the next additive pre-1.0 capability and user-visible Rick and Morty field | Complete; recorded as the unreleased `0.14.0` capability and package-major change |
| `release/1.0.0/freeze.json` and `.md` | Affected | Add distinct closed ARRAY output-collection authority without changing the scalar set | Complete; freeze verifier and mutation tests distinguish scalar types, column shapes, and ARRAY element types |
| `docs/rfcs/evidence/0022/` | New authority | Freeze the complete amended connector-package schema, fixture-index schema, fixture-coverage mapping, and evidence manifest without editing RFC 0013 evidence | Complete; externally anchored manifest and artifacts pass the multi-generation identity verifier |
| Connector and fixture schemas/assets | Affected | Byte-copy the accepted 0022 schemas/mapping into production; add the closed column alternative and sequence-valued expected cells; retain an independent verifier for historical 0013 evidence | Complete; production mirrors and embedded asset identities pass, while the restored 0013 verifier remains independent |
| Rick and Morty package, examples, fixtures, diagnostics, and tests | Affected | Add episode with a package-major change and controlled end-to-end/failure evidence | Complete; `2.0.0` package digest, fixture coverage, supported 32-page envelope, Query surface, and controlled whole-graph SQL pass; checked-in fixture execution remains the recorded project-wide fast follow |

## Unresolved questions

- Non-blocking: exact C++ names and source-file placement for the structural
  column/value types. Delivery must preserve the provider-consumer boundaries
  above; naming is lead-agent implementation authority.
- Non-blocking: exact rendered fixture-coverage key spellings for ARRAY
  element/order/nesting variants. The typed variant set and behavior are
  decision requirements; stable key names follow existing derivation rules.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Connector Experience perspective | Connector Experience | Approved | The public grammar is closed and decidable. Implementation must make both nullability levels visible in source-safe compiled explanation and pin diagnostic code/phase/field/source coordinates for every column branch and GraphQL ARRAY<DOUBLE> rejection. | Accepted as required implementation evidence; no RFC text objection remained. |
| Query Experience perspective | Query Experience | Objected, then Approved after amendment | The initial proposal omitted the scalar-only `CompiledQueryRegistrationView` publication boundary and overpromised a Query wall-time deadline not present in `ExecutionControl`. | Corrected Shared interfaces to carry structural facts through `CompiledRegistrationColumn`, require focused provider/consumer tests, limit Query checkpoints to cancellation, and keep deadline ownership in Runtime. Re-review confirmed both blockers resolved. |
| Remote Runtime perspective | Remote Runtime | Approved | The decision closes strict REST/GraphQL decode, immutable flat nested values, two validity levels, redaction, resource accounting, cancellation, and provider direction. Review emphasized debit-before-allocation and temporarily co-live page/batch storage. | Clarified all Runtime-owned structural/page/batch capacity and co-live transfer memory; retained the exact implementation evidence as interaction exits. |
| Relational Semantics perspective | Relational Semantics | Approved | One source record remains one row; planned shape is explicit; list predicates remain DuckDB-owned. Review requires mismatch counterexamples for shape, kind, both nullabilities, order/arity/path and real filter/order/limit/offset oracles. | Accepted as required implementation evidence; no RFC text objection remained. |
| Engineering Enablement perspective | Engineering Enablement | Objected, then Approved after containment | Review found the baseline's RFC 0013 evidence and source-identity mutation suite had drifted, so the proposal's original immutability premise was false. | Restored accepted 0013 bytes/verifier, added externally anchored 0013/0022 identity verification and six mutation categories, repaired the 18-case current source-identity suite, made the gates authoritative in `AGENTS.md`, and corrected propagation. Re-review confirmed the blocker resolved. |

Implementation-time adversarial review used independent relational,
Runtime/resource-lifecycle, and Query/test-oracle contexts. Their findings on
schema correlation, zero-arity REST fallback, decoded staging and pagination
memory, typed-batch handoff, cancellation, and SQL-oracle completeness were
repaired. Each exact repair delta was re-reviewed; the final three perspectives
reported no actionable findings.

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Received — Nic Galluzzo, 2026-07-22. Approval covers
  the reviewed `type: ARRAY` source shape with required element fields, the
  two-level NULL contract, REST/current-GraphQL scalar parity, retention in
  `duckdb_api/v1`, existing resource-ceiling treatment, and the Rick and Morty
  package-major adoption.
- **Decision:** **Accepted.** Add the reviewed output-column-only ARRAY shape
  to `duckdb_api/v1` and propagate the externally anchored 0022 schemas,
  structural provider interfaces, Runtime decode/value behavior, and native
  DuckDB LIST output defined above.
- **Rationale:** The reviewed shape is the smallest static extension that
  represents the motivating field without widening scalar inputs, predicates,
  protocols, cardinality, or arbitrary JSON. Explicit structural descriptors
  at every provider boundary prevent type-string reinterpretation; native
  DuckDB LIST preserves the intended SQL value. The pre-decision evidence
  containment removes the systemic drift that would otherwise make this
  contract unverifiable.
- **Material objections:** Query's missing registration boundary and
  Engineering Enablement's historical-evidence drift were both accepted,
  corrected, and independently re-reviewed as resolved. No material objection
  or decision gate remains.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Deliver list-of-scalar ARRAY output columns and the Rick and Morty episode field | Connector Experience | Relational Semantics and Remote Runtime (Collaboration then X-as-a-Service), Query Experience (Collaboration), Engineering Enablement (Facilitation) | Activated 2026-07-22 after this RFC was Accepted; completion requires the recorded adversarial review and exact committed-tree gates |
| Add GraphQL Float/DOUBLE scalar and ARRAY<DOUBLE> result support | Connector Experience | Relational Semantics, Remote Runtime, Query Experience | Separate product priority and accepted RFC; not activated here |
| Add nested collections, structs, maps, JSON, or unnesting semantics | Connector Experience or Query Experience according to the acceptance narrative | Relational Semantics and Remote Runtime | Separate product outcome and accepted RFC; explicitly excluded here |
