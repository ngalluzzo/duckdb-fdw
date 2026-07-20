# RFC 0012: Define local-package SQL registration and migration

```yaml
rfc: "0012"
title: "Define local-package SQL registration and migration"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Query Experience"
technical_decision_owner: "Lead agent"
product_approver: "Product manager"
authors:
  - "Lead agent"
required_reviewers:
  - "local_package_sql_query_review"
  - "local_package_sql_connector_review"
  - "local_package_sql_runtime_review"
  - "local_package_sql_semantics_review"
  - "local_package_sql_enablement_review"
affected_teams:
  - "Query Experience"
  - "Connector Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
linked_outcome_or_objective: "Author and query a local connector package through the complete accepted v1 subset"
supersedes: "none"
```

## Summary

Give every loaded connector relation one deterministic DuckDB table-function
name, derived as `<connector_id>_<relation_id>`, and expose typed relation
inputs directly as named SQL arguments. Add explicit load and reload statements
for local packages, reject all naming or publication collisions atomically, and
retain `duckdb_api_scan` only as a documented `0.8.0` migration surface for the
four native preview relations before removing it for the `0.9.0` API candidate.

## Sponsorship and context

- **RFC type:** Product.
- **Sponsoring team:** Query Experience.
- **Linked outcome:** A connector author can validate, fixture-test, explicitly
  load, and query a complete local package through ordinary DuckDB SQL.
- **Why now:** `0.7.0` proved REST and GraphQL through the permanent native
  product boundaries. Package registration cannot begin until users and
  authors know the exact SQL identity, collision behavior, and migration path
  that `0.8.0` will implement.

RFC 0009 requires this decision before package registration depends on SQL
naming. It also requires the `0.8.0` preview to exercise the chosen behavior
before `0.9.0` freezes the candidate. This RFC chooses Query-visible behavior;
the separate Connector Experience RFC chooses the successor to
`duckdb_api/draft` and the complete author declaration subset.

## Problem

The native product currently exposes one fixed dispatcher:

```sql
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'authenticated_repositories',
    secret := 'github_default'
);
```

That function has exactly three declared named parameters. DuckDB rejects an
unknown named argument before the extension binder runs, so it cannot provide
typed, package-defined relation inputs. Adding an untyped map or JSON argument
would make every author and user learn an escape-hatch encoding and would hide
input types from DuckDB and nullability, defaults, and possible value sources
from extension introspection.

The design documents also contain an inactive optional `sql_name` declaration,
generated-name suggestions, package loading, collision refusal, and atomic
reload rules without selecting which of them is public. Implementing a loader
before resolving those choices would let the first code path silently define:

- whether packages claim arbitrary SQL names;
- whether one SQL name can merge overloads from several packages;
- how existing DuckDB functions, other packages, and reloads collide;
- whether prepared statements retain the old or new package generation;
- how the four existing preview relations migrate; and
- which statements load, replace, inspect, or remove packages.

Those are public compatibility and lifecycle decisions, not reversible loader
internals.

## Decision drivers and invariants

- **Must preserve:** Ordinary bind, `DESCRIBE`, `EXPLAIN`, and `PREPARE` are
  deterministic and perform no network I/O.
- **Must preserve:** DuckDB owns relational filtering, projection, ordering,
  limits, and offsets unless the shared planner proves a safe remote action.
- **Must preserve:** Package reload never mutates an in-flight or already-bound
  immutable connector snapshot.
- **Must preserve:** Credentials remain logical names at bind and become opaque
  capabilities only during execution.
- **Must enable:** DuckDB can type-check every accepted SQL argument by name
  and type, while extension introspection displays its nullability, default,
  and ownership without a generic JSON/map escape hatch.
- **Must enable:** Initial publication and replacement of a multi-relation
  package are all-or-nothing across SQL names and the active package registry.
- **Must enable:** A connector author can predict SQL names from stable package
  identifiers without inspecting local load options.
- **Must not introduce:** Caller-selected SQL aliases, automatic discovery,
  attached catalogs, package fetching, lockfiles, signing, dependency
  resolution, persistent package installation, or unload behavior.
- **Must not introduce:** A public C++ ABI or compatibility promise beyond the
  exact tested DuckDB native profile.

## Proposed decision

### Public behavior

#### Relation functions

Every active relation is a DuckDB table function in `system.main` with the
lower-case name:

```text
<connector_id>_<relation_id>
```

Both identifiers already use lower-case snake case. The generated SQL name is
not author-overridable and is not affected by the package directory, load path,
publisher, package version, or a load-time alias. The accepted connector-spec
successor therefore omits the inactive draft `sql_name` field. A later aliasing
or namespace policy would require its own product RFC and migration analysis.

The public identity is therefore
`system.main.<connector_id>_<relation_id>`. Normal examples use DuckDB's
unqualified convenience spelling. After explicitly loading the
repository-owned GitHub package, those calls are:

```sql
FROM github_duckdb_login_search_page();

FROM github_authenticated_user(
    secret := 'github_default'
);

FROM github_authenticated_repositories(
    secret := 'github_default'
);

FROM github_viewer_repository_metrics(
    secret := 'github_default'
);
```

Each declared relation input becomes one named argument with its declared
DuckDB type, but every relation-origin argument may be omitted at Query bind.
Query places only explicitly supplied relation arguments into `ScanRequest`.
It preserves an explicit SQL NULL as a present typed value rather than treating
it as omission. Relational Semantics resolves operation-independent explicit,
connection, and partition values first. For each operation candidate, it then
applies only that candidate's predicate mappings and operation-declared
constants, rejects conflicting non-default sources, and applies a compiled
default only when no non-default source supplied the input. A nullable explicit
NULL therefore suppresses a default; a non-nullable explicit NULL is rejected.
Only after these candidate-local bindings exist does Semantics decide
eligibility, rank, fallback, or ambiguity. Provider parent-row and pagination
bindings belong to their post-selection subplans and never make a base
operation eligible. Missing operation inputs are planning diagnostics, not
Query binder errors. `secret VARCHAR` is a required Query argument only on
relations that declare an accepted authenticator. Anonymous relations do not
list or accept it. Connector and relation identity are captured by the
registered function and are no longer caller arguments.

`secret` is reserved against declared relation inputs for every accepted
package relation, including anonymous relations. Connector validation rejects
an input with that identifier at its exact source location before compilation,
fixture execution, or publication. Reserving it uniformly keeps a later
authentication change from silently altering an existing function signature.

The function binds an immutable package generation. The result schema,
nullability, cardinality metadata, explanation, diagnostics, and relational
behavior come from that generation. Package source is never read during an
ordinary bind or execution.

DuckDB's native table-function catalog exposes named-argument names and types,
but it does not expose their nullability or compiled defaults. Query converts
supplied values to their declared DuckDB types and preserves presence;
Relational Semantics owns nullability validation, default application, and
eligibility. The extension introspection below exposes the complete argument
contract without inventing relation-global requiredness.

#### Package lifecycle statements

The extension adds exactly two state-changing statements:

```sql
CALL duckdb_api_load_connector(
    package_root := '/absolute/package/root'
);
CALL duckdb_api_reload_connector(
    connector := 'connector_id'
);
```

Both functions have no positional parameters. `package_root VARCHAR` and
`connector VARCHAR` are required, non-null named parameters with no defaults.
`duckdb_api_load_connector` compiles the explicit local package root and fails
if the connector identifier is already active. `duckdb_api_reload_connector`
recompiles the active local connector from its recorded canonical root and
fails for an unknown connector. Neither statement accepts `replace`, alias,
discovery, URL, version-selection, or credential arguments. There is no unload
statement in the v1 candidate.

Success publishes the entire package generation and all its relation functions
once. Failure returns one source-aware, redacted diagnostic and changes
nothing. Each successful invocation returns exactly one row with non-null
`connector VARCHAR`, `package_version VARCHAR`, `spec_version VARCHAR`,
`package_digest VARCHAR`, `relation_count UBIGINT`, and `changed BOOLEAN`
columns. Load returns `changed = true`. Reload of byte-identical package
content is a successful no-op with `changed = false`; another successful reload
returns `changed = true`.

DuckDB lowers `CALL` to a materialized query over the same table function.
Consequently a direct
`FROM system.main.duckdb_api_load_connector(...)` and the reload equivalent
have identical arguments, output, and effects when DuckDB executes their scan;
`CALL` is the documented management spelling. Like every DuckDB table
function, a composed `FROM` follows relational demand semantics. If the
optimizer proves the function's result unnecessary, as in `WHERE FALSE`, it
may omit the scan and no package operation occurs. Callers must not use
relational pruning or short-circuiting to conditionally schedule management
work.

Bind, `DESCRIBE`, `EXPLAIN`, and `PREPARE` perform no package compilation or
mutation. Each materialized execution of a direct prepared management call
attempts the operation exactly once. A statement containing more than one load
or reload invocation is rejected before local work starts.

Load and reload are DatabaseInstance-scoped administrative operations, not
transactional DDL. They require autocommit and fail before local work inside an
explicit `BEGIN`; caller `COMMIT` and `ROLLBACK` therefore never control an
active package. Successful publication is visible to autocommit statements and
transactions that begin afterward on every connection in the same
DatabaseInstance. A transaction whose DuckDB catalog snapshot predates
publication continues to bind and introspect the complete older package
registry until that transaction ends. Active packages are not persisted into a
DuckDB database file and must be loaded again in a new DatabaseInstance. The
coordinator's exact catalog machinery is implementation detail, not a public
extension ABI.

Three read-only table functions expose the active state:

```sql
FROM duckdb_api_loaded_connectors();
FROM duckdb_api_loaded_relations();
FROM duckdb_api_relation_arguments();
```

`duckdb_api_loaded_connectors()` returns non-null `connector VARCHAR`,
`package_version VARCHAR`, `spec_version VARCHAR`, `package_digest VARCHAR`,
and `relation_count UBIGINT` columns. `duckdb_api_loaded_relations()` returns
non-null `connector VARCHAR`, `relation VARCHAR`, `sql_name VARCHAR`, and
`package_version VARCHAR` columns. `duckdb_api_relation_arguments()` returns
non-null `connector VARCHAR`, `relation VARCHAR`, `argument VARCHAR`,
`duckdb_type VARCHAR`, `nullable BOOLEAN`, `has_default BOOLEAN`, and
`argument_origin VARCHAR` columns plus nullable
`default_value VARCHAR`. `default_value` is a canonical SQL literal and is NULL
exactly when `has_default` is false; this distinguishes an explicit NULL
default. `argument_origin` is `relation` or `query`, so an authenticated
relation's reserved `secret` argument is visible without being mistaken for
author metadata. No function exposes a package root or credential value. Rows
are deterministically ordered by connector, relation, and argument identifier
where applicable.

All five management and introspection functions have canonical identities in
`system.main`, just like generated relation functions. Unqualified calls are
the normal spelling; qualification remains available if a later user macro
shadows one.

#### Naming and collision rules

SQL identifiers are compared using DuckDB's case-insensitive catalog rules.
Before publication, Query verifies the complete generated-name set against:

- every relation in the candidate package;
- every relation in every active package generation; and
- every existing DuckDB table function or table macro visible in the target
  function namespace.

Any collision fails the whole operation. DuckDB's ordinary extension-loader
helper alters or merges some same-named function sets, so it is not the
collision oracle: delivery must use explicit error-on-conflict creation or an
equivalent coordinator and recheck under the publication lock. The loader
never merges overloads,
replaces an unrelated function, publishes a suffix such as `_2`, or lets a
load-time option rename the package. Tables and scalar functions occupy
different call namespaces and do not collide merely because their text name is
equal; the collision oracle follows DuckDB's actual table-function lookup.

A table macro created in a user catalog after publication may shadow the
unqualified convenience spelling without replacing or mutating the package
function in `system.main`. This follows DuckDB's normal catalog resolution and
is not treated as package reload. Introspection continues to show the active
package, and `system.main.<generated_name>(...)` always selects its canonical
function. Documentation and diagnostics provide that qualified escape hatch;
an after-load or concurrent-DDL oracle proves that shadowing never produces a
mixed package generation.

A reload may replace and remove only the SQL names owned by the same active
connector generation. Its complete old-to-new name set is validated before the
single publication point. A new name that collides with another owner, or a
late failure after any registration preparation, leaves every old function and
registry entry unchanged.

#### Preview migration

`0.8.0` loads a repository-owned local GitHub package, registers the four
generated functions above, and uses them in all current documentation,
examples, fixtures, and product demonstrations. They do not exist in a new
DatabaseInstance until that explicit load succeeds. The existing
`duckdb_api_scan` dispatcher remains available in `0.8.0` only for the four
native preview relations with its existing arguments and behavior. Its legacy
native catalog is not an active package generation, does not appear in package
introspection, and does not dispatch arbitrary local packages. The dispatcher
is recorded as deprecated in the canonical public inventory and release notes.

`0.9.0` removes `duckdb_api_scan` before freezing the API candidate. The
migration is mechanical:

| `0.7.0` dispatcher identity | `0.8.0` and later canonical function |
| --- | --- |
| `github`, `duckdb_login_search_page` | `github_duckdb_login_search_page()` |
| `github`, `authenticated_user` | `github_authenticated_user(secret := ...)` |
| `github`, `authenticated_repositories` | `github_authenticated_repositories(secret := ...)` |
| `github`, `viewer_repository_metrics` | `github_viewer_repository_metrics(secret := ...)` |

The `duckdb_api/config` temporary-secret contract and the relation schemas do
not change merely because the function name changes. Project SemVer classifies
the `0.9.0` dispatcher removal as an intentional pre-1.0 incompatibility with
curated migration guidance. No deprecated alias enters the `1.0.0` inventory.

### Shared interfaces

Connector Experience provides one immutable `CompiledConnector` generation
with stable connector and relation identifiers, typed inputs, schemas, and
package identity. Connector validation rejects Query-reserved input names
before producing that generation. It provides no caller-selected SQL name and
does not register DuckDB objects.

Query Experience owns generated SQL names, DuckDB catalog registration, typed
argument binding, preview migration, and safe Query diagnostics. Binding turns
the registered relation identity plus typed arguments into a protocol-neutral
`ScanRequest`; it does not parse package syntax or select a protocol.

Relational Semantics remains the only owner of input eligibility, operation
selection, predicate classification, and residual obligations. A generated
function does not bypass `ScanRequest -> ScanPlan`, and Query does not infer
remote meaning from the SQL function name.

Remote Runtime owns the immutable active-generation registry and reload-side
lifecycle service. The lead-owned product composition coordinates Connector
compilation, Runtime generation staging, and Query catalog publication at one
observable commit point. The RFC does not publish an internal C++ type or fix
the reversible mechanics of that coordinator.

Runtime retains each registry snapshot for as long as a DuckDB transaction,
bound plan, prepared plan, or scan can observe its matching catalog generation.
The introspection functions resolve through that same transaction snapshot,
never through an unversioned global name lookup. Runtime does not interpret
DuckDB catalog timestamps; Query's coordinator supplies the opaque registry
generation selected by the catalog owner.

Every successful relation-function bind retains shared ownership of the exact
immutable generation it observed. A `PREPARE` whose relation-function
arguments are fully resolvable at prepare time performs that bind then and pins
the generation. If a relation-function argument contains an unresolved
parameter, pinned DuckDB defers its table-function bind; each `EXECUTE` then
binds against the generation active at that execution. Parameters elsewhere in
the query do not change a generation already captured by the relation-function
binder. In all cases, once one execution has accepted its plan, reload cannot
change that scan. Runtime never looks up a newer generation by connector name
while executing an accepted plan.

### Operational behavior

Package compilation, collision checking, and publication perform local file
and catalog work only; they acquire no network or credential authority. Load
and reload use bounded file, document, relation, column, input, operation, and
diagnostic budgets chosen by the connector-spec RFC and host policy.

Publication is serialized, but scans of immutable generations continue without
holding the publication lock. A failed load or reload closes and discards every
candidate resource. On the pinned DuckDB profile, every connection and active
query retains shared ownership of its `DatabaseInstance`; actual instance
destruction therefore begins only after publication work is quiescent. A
DatabaseInstance-owned lifecycle sentry closes the coordinator during that real
destruction path and releases registry generations only after their final
transaction, scan, or prepared-plan owner ends. The coordinator's explicit
close primitive rejects queued and future publications while allowing a
lock-owning publication to drain, but this RFC does not claim that DuckDB 1.5.4
supplies an active-shutdown callback—it does not. Dynamic extension DSO unload
remains unsupported.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsor and owner of the DuckDB-user outcome | Own generated relation functions, management SQL, bind arguments, collisions, and migration | Collaboration, then normal stream ownership | Actual DuckDB registration, migration, prepare, repeat, collision, and diagnostic oracles pass without Query parsing package or protocol internals |
| Connector Experience | Author-workflow and compiled-metadata provider | Remove author-selected SQL naming from the candidate and provide stable identifiers and typed inputs | Collaboration, then X-as-a-Service | Query consumes one immutable compiled generation and Connector author fixtures predict every generated SQL name |
| Remote Runtime | Registry and lifecycle platform provider | Stage immutable generations and preserve old owners across atomic reload and shutdown | Collaboration, then X-as-a-Service | Query publishes and consumes generations through a bounded lifecycle service without Runtime-private state |
| Relational Semantics | Planner and correctness provider | Accept resolved typed relation inputs without deriving meaning from SQL names | X-as-a-Service with bounded review | Generated-function and dispatcher requests produce equivalent plans, and Query performs no operation selection |
| Engineering Enablement | Public-inventory facilitator | Add schema-backed SQL and migration classifications, then transfer entries | Facilitation | Query independently maintains the SQL inventory and compatibility oracle without Enablement approval |

No charter or accountability boundary moves. The change removes connector
authors' need to understand DuckDB registration while preventing Query from
learning author syntax or Runtime lifecycle internals.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** Function naming changes
  no row domain. Typed arguments enter the existing request and planner path;
  unsupported or ambiguous operation selection fails or falls back exactly as
  the accepted semantic contract requires.
- **Authentication, credentials, network policy, and privacy:** A package
  cannot contain a secret value. `secret` remains an explicit logical name;
  load, reload, naming, introspection, and bind perform no credential lookup or
  network I/O. Introspection omits package roots and secret data.
- **Resources, backpressure, and cancellation:** Load and reload gain separate
  bounded local-work budgets. Scan budgets, stream backpressure, cancellation,
  and close behavior are unchanged and remain generation-local.
- **Replay, retries, caching, and duplicates:** Not affected. Publication does
  not execute a remote operation, and a reload never resumes or repeats a scan.
- **Concurrency, immutability, and state ownership:** Publication is one
  serialized all-or-nothing state transition. Old scans and fully bound
  prepared plans own their generation; an already-open DuckDB transaction also
  retains its matching catalog and registry snapshot. Deferred parameterized
  table-function binds choose the complete generation visible to their
  execution transaction. No bind or introspection query observes a mixture.
- **FFI, initialization, reload, shutdown, and failure containment:** The
  selected native profile may use DuckDB's internal C++ catalog APIs only on
  the exact compatibility matrix. Every DuckDB callback contains exceptions.
  Late catalog, allocation, or lifecycle failure rolls back the candidate and
  keeps the old generation. DatabaseInstance destruction is quiescent under
  pinned shared ownership; the database-owned sentry closes and releases the
  coordinator on that actual path, while an independently tested close
  primitive defines queued and active publication behavior for any earlier
  product teardown.
- **Diagnostics, redaction, metrics, and progress:** Load diagnostics carry
  safe connector, relation, generated name, source location, and failure stage
  fields. They omit source contents, credential material, remote data, and
  unapproved local paths. No metrics or progress surface is added.

## Compatibility and migration

This is an intentional public-SQL expansion in `0.8.0` followed by the planned
removal of a preview dispatcher in `0.9.0`. The migration table above covers
every current relation. The generated names, argument types, schemas, logical
secret behavior, diagnostics, and lifecycle surfaces enter the schema-backed
public inventory when implemented.

A package version change may add a relation function compatibly. Removing or
renaming a relation or argument, changing an argument's type or nullability, or
incompatibly changing a schema is an incompatible package-SQL change and fails reload while an
incompatible active generation remains unless the later accepted connector
version and migration contract authorizes that transition. Project
compatibility rules govern project-owned management functions and
repository-owned package functions; the Connector RFC decides the exact
independent package SemVer classification, including default and operation-
eligibility changes.

Rollback from a successful reload is a new reload of an explicitly retained
older package source, not mutation of an in-flight snapshot. The v1 product
does not promise automatic rollback storage or a historical package resolver.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| The generic dispatcher cannot accept arbitrary package named inputs | Pinned DuckDB binder and installed-function evidence | Inspect `bind_named_parameters.cpp`; call `duckdb_api_scan(..., owner := 'duckdb')` in the pinned product CLI | Confirmed: DuckDB rejects `owner` before the extension binder and lists only `connector`, `relation`, and `secret` |
| Generated native relation functions can expose typed named arguments | Pinned product and catalog evidence | Inspect and query native `duckdb_api_scan`; inspect `TableFunction` and `duckdb_functions()` | Confirmed for argument names and types. Native table-function metadata has no nullability or default fields, so Query preserves typed presence, introspection reports metadata, and Semantics owns nullability, defaults, and eligibility |
| Defaulted named-call syntax is usable SQL | Actual DuckDB SQL evidence | Create and call a typed table macro with a default named argument in DuckDB 1.5.4 | Confirmed only for call ergonomics. It does not prove native table-function default metadata or generation ownership |
| A catalog transaction can roll back several user macros | Transactional catalog analogy | Create one existing table macro, begin a transaction, create a new relation, trigger a duplicate-name error, and roll back | Confirmed only for user-catalog DDL. It does not prove system-catalog table-function and Runtime-registry atomicity |
| A later user macro can shadow an extension table function | Actual name-resolution evidence | Load `duckdb_api_scan`, create a zero-argument table macro with the same name, query unqualified and `system.main` forms, and inspect `duckdb_functions()` | Confirmed: both catalog rows remain, the unqualified macro wins, and the qualified extension function remains callable; preflight protects initial convenience but qualification is the durable identity |
| Prepared generation choice depends on whether table-function arguments bind at `PREPARE` | Pinned binder and CLI evidence | Prepare literal macro input and separately prepare parameterized `duckdb_api_scan` inputs, change the catalog, then execute | Confirmed: the fully bound plan retained the old definition; unresolved table-function parameters forced an execution-time rebind that observed the new macro. Actual generated functions must repeat both cases across reload |
| Ordinary extension registration is not the chosen collision/publication primitive | Pinned DuckDB source | Inspect `ExtensionLoader::RegisterFunction`, `DuckSchemaEntry`, and `TableFunctionCatalogEntry` | Confirmed: the helper uses a system transaction with alter-on-conflict and may merge an overload. Delivery requires an explicit error-on-conflict coordinator plus actual race, overload, late-failure, and multi-name atomicity oracles |
| `CALL` is a materialized query over a table function | Primary DuckDB documentation, pinned binder, and CLI | [DuckDB `CALL`](https://duckdb.org/docs/current/sql/statements/call), `bind_call.cpp`, and existing `CALL enable_logging()`/`FROM enable_logging()` | Confirmed: `CALL` and `FROM` reach the same function and bind-defined result schema. Call-only behavior cannot be claimed on this profile |
| The selected management transaction and repetition rules are implementable | Actual pinned native table-function evidence | Build the opt-in `duckdb_api_rfc0012_native_coordinator_trial` target and run it directly; repeat the binary 20 times | Confirmed on pinned DuckDB 1.5.4: direct `CALL`/`FROM`, autocommit refusal, repeated and duplicate prepared invocation, every initial-load and reload mutation failure boundary, injected interruption, concurrent publishers, uncommitted invisibility, old transactions, fully bound/deferred prepared plans, late function/overload/macro collisions, waiter cancellation, explicit coordinator close, in-flight scan ownership, actual DatabaseInstance-owned lifecycle close, and final release all pass. `WHERE FALSE` proves that a pruned table-function scan performs no management effect, so the decision now states DuckDB's demand semantics explicitly |
| A catalog-qualified canonical call survives user shadowing | Actual qualification evidence | Query `system.main.duckdb_api_scan(...)` after creating a same-named user macro | Confirmed on the supported profile; generated and management functions must repeat this oracle |

The bounded trials establish decision feasibility but do not implement package
loading, compilation, registry state, or product registration.

## Alternatives considered

### Retain only `duckdb_api_scan`

This preserves every preview query and centralizes registration. It cannot
accept unknown typed named arguments because DuckDB validates the fixed
signature before bind. Adding `inputs := MAP(...)`, JSON, or `VARIANT` would
hide the relation contract from SQL tooling and make the primary authoring path
an escape hatch. It is retained only as a bounded migration alias.

### Let packages choose `sql_name`

This can produce shorter or domain-specific names. It creates a second public
identity for every relation, lets a package claim arbitrary global functions,
and makes package docs, migration, and collisions depend on author taste.
Stable connector and relation identifiers already provide a deterministic
name, so the extra authority is rejected.

### Register functions under one schema per connector

Calls such as `github.repository()` visually preserve the logical identity.
They also claim arbitrary user schema names, introduce schema creation and
ownership conflicts, and complicate resolution between the system catalog and
attached database catalogs. Flat generated names preserve ordinary DuckDB
function behavior without adding an attached-catalog or schema-lifecycle
contract.

### Put every function in a `duckdb_api` schema

This clearly marks extension ownership but still needs an encoding of connector
and relation identity inside one flat schema, and it makes ordinary calls more
verbose without eliminating collisions. The extension prefix on management
functions plus deterministic relation names is sufficient.

### Generate table macros over the dispatcher

Macros provide attractive typed signatures and transactional catalog behavior.
They duplicate Query-visible SQL expansion, require a second registry-to-macro
identity protocol, and make explanation and prepared-plan ownership depend on
macro expansion. Direct table functions already support typed named arguments
and immutable function information.

### Remove `duckdb_api_scan` immediately in `0.8.0`

This yields the cleanest surface but provides no release in which users can run
the old and new forms side by side. One explicit migration release is worth the
temporary duplicate surface; the alias is excluded from local packages and has
a fixed removal point before API freeze.

### Permit load-time aliases or overwrite

Aliases can resolve local collisions without editing a package. They make SQL
environment-specific, weaken package documentation and migration fixtures, and
create accidental replacement authority. The loader instead refuses the
collision without state change.

## Drawbacks and failure modes

Generated relation functions occupy DuckDB's global table-function namespace.
Long connector and relation identifiers produce long names, and a local user
function can prevent package loading. Deterministic refusal is safer than
silent renaming, but the user may need to rename or remove their conflicting
function before loading the package.

`0.8.0` temporarily carries both canonical generated functions and the old
dispatcher for native preview relations. Query Experience must keep their results,
errors, and plans differential until removal. That cost is deliberately bounded
to one migration release.

Atomic catalog and registry publication couples delivery to the exact tested
DuckDB native profile. A registration API that commits each function
independently is insufficient. Delivery must prove one transaction or an
equivalent invisible staging protocol across late collision, allocation,
compilation, registration, and replacement failures before claiming reload.

Fully bound prepared plans can intentionally outlive package reload. This
consumes old-generation memory and means a long-lived literal-input prepared
statement does not automatically receive a package fix. A prepared statement
with unresolved relation-function arguments instead binds the active
generation on each execution. Introspection and diagnostics must make the
active generation clear without mutating either owner.

## Acceptance and verification

- **End-to-end demonstration:** Explicitly load a repository-owned local
  package, inspect its active connector and relation rows, call its generated
  REST and GraphQL functions with typed arguments, and obtain correct DuckDB
  results. Run the four dispatcher-to-function migration pairs. A conflicting
  package load and a late multi-relation reload failure leave the old state
  completely usable.
- **Automated oracle:** Naming properties and collision counterexamples;
  actual-DuckDB typed bind, `DESCRIBE`, `EXPLAIN`, prepare, repeat, join,
  materialize, export, fully-bound and deferred-parameter old/new generation,
  and migration differentials; package/package, package/user-function,
  late-macro, overload, and concurrent-DDL collisions; all-or-nothing load and
  replacement; management `CALL`/`FROM`, output, autocommit, explicit-
  transaction, repetition, multi-invocation, and cross-connection visibility;
  a two-operation relation covering omission/default, nullable explicit NULL,
  non-nullable NULL rejection, equal and conflicting explicit/predicate
  sources, candidate-scoped predicate mappings and constants, connection and
  partition values, fallback, ties, and post-selection provider/pagination
  bindings;
  unsupported argument and secret cases; a source-located
  Connector validation fixture for a declared `secret` input on authenticated
  and anonymous relations; structural `ScanRequest` and `ScanPlan` equality for
  every dispatcher/generated migration pair; proof that registered SQL names
  are never parsed for connector, relation, input, or operation meaning;
  shutdown and failure containment.
- **Quality gates:** Focused team targets, canonical public-inventory
  classification, `make build`, `make test`, `make demo`, source and native
  dependency identities, a fresh native product cell, agent-asset validation,
  and staged and unstaged whitespace checks.
- **Independent review:** Query/DuckDB lifecycle, Connector authoring and
  compatibility, Runtime publication and shutdown, Relational input ownership,
  Enablement inventory/migration, and adversarial catalog/lifecycle review.
- **Interaction exit:** The final source/target audit shows Connector compiling
  but not registering, Query binding but not parsing package syntax or
  selecting operations, Semantics owning all input/operation meaning, Runtime
  preserving immutable generations without DuckDB catalog knowledge, and each
  domain team maintaining its public-inventory rows independently.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Replace preview SQL with generated relation functions; specify the bounded compatibility dispatcher and native registration profile | Pending delivery |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected | Remove `sql_name` from the accepted candidate, distinguish author IDs from Query-generated names, reserve `secret` against every relation input with a source-located diagnostic, and link the accepted spec-version RFC | Pending connector-spec decision and delivery |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Specify typed generated binds, active generations, atomic publication, prepared ownership, reload, and shutdown | Pending delivery |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | Existing accountability and team APIs cover the work; audit interaction exits | Pending audit |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing RFC, contract-change, delivery, topology, and review workflows apply | Current agent-asset validation |
| Public inventory, README, changelog, release notes, examples, diagnostics, fixtures, and tests | Affected | Record every management and relation function, the one-release dispatcher migration, collisions, schemas, arguments, and failure behavior | Schema-backed candidate inventory is complete: content-addressed shapes, Query-owned exact RFC transition contract, accepted-decision resolution, classifications, release views, and fail-closed mutations pass. Artifact, documentation, diagnostic, and behavioral propagation remains delivery work |

## Unresolved questions

- The connector-spec successor RFC decides its identifier, package-version
  relationship, exact accepted declaration subset, and package-root rules. It
  cannot reintroduce author-controlled SQL names without superseding this RFC.
- Unload, aliases, attached catalogs, persistent installation, and historical
  rollback remain excluded v1 options rather than unresolved delivery details.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| `local_package_sql_query_review` | Query Experience | Approved | Independent re-review passed the pinned native trial once and for 20 repetitions. It confirms typed generation binding, every catalog-failure boundary, old transaction and prepared-plan ownership, collisions and interleavings, cancellation, uncommitted invisibility, explicit close, actual DatabaseInstance lifecycle close, and the documented no-effect behavior of a pruned management scan | Decision evidence is complete. Actual package registration, migration, diagnostics, and final source-dependency evidence remain Query delivery and interaction-exit work |
| `local_package_sql_connector_review` | Connector Experience | Approved | Re-review confirms explicit per-DatabaseInstance loading, compiled nullability/default introspection, Semantics-owned input resolution, and exclusion of native preview metadata preserve the immutable `CompiledConnector` boundary. Initial `secret` input collision objection is also resolved | Connector requires no further RFC change; source-located reserved-name fixtures, the two-operation default/source oracle, and final Query dependency audit remain delivery evidence for interaction exit |
| `local_package_sql_runtime_review` | Remote Runtime | Approved | Focused lifecycle re-review confirms generation-bound function metadata, serialized cancelable publication, rollback at every mutation boundary, in-flight scan ownership, explicit close behavior, actual DatabaseInstance-owned lifecycle notification, and final generation release on the pinned profile | Decision feasibility is proven without publishing a Runtime-private type. Production lifecycle service and final dependency audit remain delivery interaction-exit work |
| `local_package_sql_semantics_review` | Relational Semantics | Approved | Re-review confirms the amended operation-independent, candidate-scoped, default/NULL, fallback/tie, and post-selection provider/pagination rules match the authoritative planning flow and forbid SQL-name inference | No further RFC change; complete typed-input and structural dispatcher/generated request-and-plan oracles remain delivery evidence for interaction exit |
| `local_package_sql_enablement_review` | Engineering Enablement | Approved | Independent re-review confirms content-addressed shapes, Query's exact RFC-to-identity and transition contract, accepted-decision resolution, active/removed release views, and 18 fail-closed mutation tests including coordinated omission/extra and recomputed-digest bypasses | Query independently owns the domain contract and normal gate; Enablement retains only reusable schema and verifier maintenance, so the facilitation interaction exits |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Approved by the product manager on 2026-07-20 for the
  generated `system.main` SQL identity, non-overridable names, explicit
  per-DatabaseInstance load/reload and introspection surface, snapshot reload
  behavior, and one-release dispatcher migration.
- **Rationale:** Direct generated table functions expose typed package inputs
  through ordinary DuckDB SQL while stable package identifiers keep naming and
  collision behavior deterministic. Explicit local activation proves the
  package mechanism without adding installation or discovery policy, and one
  bounded dispatcher release provides a mechanical pre-1.0 migration.
- **Material objections:** Resolved. Query Experience and Remote Runtime
  approved the decision-critical native coordinator, snapshot, pruning,
  cancellation, failure, and lifecycle evidence. Engineering Enablement
  approved the schema-backed exact-surface gate after content-addressed shapes,
  Query-owned expectations, accepted-RFC resolution, and coordinated mutation
  tests closed its fail-open counterexamples.
- **Superseded by:** Not applicable.

Acceptance is not implementation completion.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Decide the stable connector-spec identity and complete v1 subset | Connector Experience | Query Experience, Remote Runtime, and Relational Semantics — Collaboration; Engineering Enablement — Facilitation | RFC 0012 Accepted; RFC 0009 already Accepted |
| Author and query a complete local connector package | Connector Experience | Query Experience and Remote Runtime — Collaboration then X-as-a-Service; Relational Semantics — X-as-a-Service; Engineering Enablement — Facilitation | RFC 0012 and the connector-spec successor RFC Accepted; canonical public-inventory gate complete; product goal activated |
