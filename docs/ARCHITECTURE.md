# Architecture

DuckDB API is a DuckDB-native relational adapter for well-structured HTTP and
GraphQL APIs. A local declarative package defines typed relations; the
extension compiles one immutable generation, plans conservatively, executes
through a bounded remote runtime, and returns ordinary DuckDB rows.

The central contract is:

```text
package source
    -> immutable CompiledPackageGeneration
    -> typed ScanRequest
    -> immutable ScanPlan
    -> bounded BatchStream
    -> DuckDB vectors
```

Each transition has one owner. Source syntax cannot become runtime authority,
SQL names cannot become relational meaning, and received network data cannot
widen a compiled plan.

## Product surface

An author explicitly loads an absolute local package root:

```sql
CALL duckdb_api_load_connector(
    package_root := '/absolute/path/to/github'
);
```

An accepted package publishes one table function per relation in
`system.main`, named:

```text
<connector_id>_<relation_id>
```

The repository GitHub package therefore exposes:

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

Relation inputs become named arguments. Authenticated relations additionally
receive the reserved `secret VARCHAR` Query argument. Connector and relation
identity are captured by the registered function and are not caller
arguments.

Reload uses the active connector's retained canonical root:

```sql
CALL duckdb_api_reload_connector(connector := 'github');
```

Load and reload return connector, package version, spec version, package
digest, relation count, and whether publication changed. Three read-only table
functions expose active connector, relation, and argument metadata without
package roots or credentials:

```sql
FROM duckdb_api_loaded_connectors();
FROM duckdb_api_loaded_relations();
FROM duckdb_api_relation_arguments();
```

The `0.8.0` product retains the native `duckdb_api_scan` dispatcher for its
four `0.7.0` preview relations as a deprecated migration surface. It is not an
active package and does not appear in package introspection. `0.9.0` removes
that dispatcher before the public API candidate is frozen.

## Responsibility boundaries

| Boundary | Owns | Does not own |
| --- | --- | --- |
| Connector Experience | Source custody, YAML, closed validation, compilation, package identity, diagnostics, compatibility, fixtures, immutable metadata | DuckDB catalog mutation, relational implication, transport, credentials |
| Query Experience | DuckDB names and arguments, bind, catalog MVCC publication, management/introspection SQL, vector output, user diagnostics | Package parsing, defaults, operation selection, request policy |
| Relational Semantics | Input resolution, operation selection, predicate implication, residual ownership, budget intersection, `ScanPlan` | Package syntax, DuckDB catalog state, network execution |
| Remote Runtime | Plan admission, authorization, requests, transport, decode, pagination, resources, streams, generation retention | YAML, SQL names, relational proof, catalog timestamps |
| Lead composition | Connects bounded team services and owns one observable publication point | A new catch-all subsystem or second semantic model |

Source modules follow those responsibilities rather than a team-directory
facade. Focused targets link provider services; consumers do not compile a
provider's production sources or import its private builders.

## Immutable generations

Package compilation yields one shared `CompiledPackageGeneration`:

```text
identity
├── spec identifier
├── connector identifier
├── package SemVer
└── source digest

ordered relations
├── structural outputs and nullability
├── structural inputs and typed default presence
├── authentication shape
├── operations and selectors
├── predicates and proof facts
├── network and resource envelope
└── safe source coordinates and explanation
```

The generalized compiled model is the only semantic model for package and
native migration behavior. The compiler does not retain an independently
authoritative YAML-shaped model.

Query receives only package identity, ordered structural outputs and inputs,
typed defaults, authentication shape, safe source references, and an opaque
generation handle. It cannot inspect protocol, predicate, policy, or compiler
state. Semantics receives the complete compiled facts. Runtime receives only
the resulting plan.

Generation state is immutable after construction. A bound function, prepared
plan, transaction, introspection scan, or active remote scan retains shared
ownership of the exact generation it observed. Reload never changes an
accepted plan or redirects execution to a newer generation by name.

## Package compilation

Compilation performs local work only:

1. Open the explicit absolute root and every admitted leaf without following
   links.
2. Capture deterministic directory and file identity before the first read.
3. Read a bounded immutable semantic-source snapshot.
4. Repeat directory and file identity capture and reject any change.
5. Compute the length-framed source digest.
6. Parse the bounded YAML failsafe subset with source spans.
7. Apply the exact byte-copied v1 schemas.
8. Validate identifiers, references, types, selectors, auth, policy, resources,
   predicates, REST, and generated GraphQL.
9. Construct and validate the complete immutable generation.
10. Classify compatibility against any active generation.

Compilation performs no network I/O and resolves no credential. Unsupported,
unknown, unsafe, stale, or over-budget source produces bounded deterministic
diagnostics and no candidate generation.

The complete author syntax and resource ceilings are defined in
[CONNECTOR_SPECIFICATIONS.md](CONNECTOR_SPECIFICATIONS.md).

## Query binding and planning

Ordinary bind, `DESCRIBE`, `EXPLAIN`, and `PREPARE` read no package source,
resolve no secret, and perform no network I/O.

The generated relation function captures connector and relation identity from
its immutable registration descriptor. Query converts only explicitly supplied
named relation arguments into ordered structural inputs:

- absence means omitted;
- a present SQL NULL remains a typed present NULL; and
- a present value retains its `BOOLEAN`, `BIGINT`, or `VARCHAR` type.

Query does not apply defaults or binder-requiredness to relation-origin
arguments. It synthesizes and validates the separate logical secret selector
for authenticated relations, but never resolves the secret during bind.

Query supplies the full output-schema closure and every bounded DuckDB
predicate structure it can observe. Missing DuckDB capabilities are false
facts, not permission to reconstruct SQL text.

Relational Semantics then:

1. validates generation, relation, request, and adapter capabilities;
2. resolves omitted, NULL, and typed relation-input values;
3. applies a default only to an omitted input;
4. derives candidate-local conditional predicate inputs;
5. selects one eligible operation or fails a tie/missing fallback;
6. classifies every possible remote predicate as exact, superset, unsupported,
   or ambiguous from structured proof facts;
7. computes the complete projection closure and intersects all budgets; and
8. produces one immutable `ScanPlan` with exact residual ownership.

Planning is deterministic and side-effect free.

## Relational correctness

Remote work is an optimization. DuckDB owns correctness.

For DuckDB predicate `D` and remotely applied predicate `R`, safe pushdown
requires:

```text
D implies R
```

Exact pushdown additionally requires:

```text
R implies D
```

The proof domain is an occurrence bag, not merely a set of equal row values.
Duplicate multiplicity, SQL three-valued logic, unknown restricted
occurrences, changed rows, extra rows, and lost true rows are part of the law.

Every residual has exactly one owner. The extension retains every DuckDB
predicate offered to it, including predicates whose remote mapping is exact.
Projection, ordering, limit, and offset remain DuckDB-owned in the v1 package
contract. Limit and offset apply only after required filtering and ordering.

Provider or pagination values cannot make a base operation eligible. A
superset restriction retains the complete DuckDB residual. Unsupported or
ambiguous structure uses the unrestricted fallback only when that fallback is
complete and eligible; invalid state produces no plan.

## Execution and authorization

Runtime admission validates the complete closed `ScanPlan` before credential
materialization, DNS, socket creation, or transport observation. Admission is
based on typed executable facts and content identity, never connector ID,
relation name, package version, source path, SQL name, or explanation prose.

An authenticated scan carries a logical DuckDB secret reference until global
execution initialization. Query resolves exactly that name through the pinned
Secret Manager integration and moves an opaque authorization capability into
Runtime. Runtime validates authenticator, placement, destination, and network
policy before decorating a request. The credential value never enters package
source, compiled explanation, `ScanPlan`, diagnostics, fixtures, digests, or
catalog introspection.

The host and package network policies are intersected. Package policy can
narrow but never widen exact HTTPS origin, redirect, proxy, private/link-local/
loopback address, cookie, netrc, TLS, header, or response authority.

## Requests, responses, and pagination

REST and GraphQL have distinct compiled and planned operation values. They
share authorization, policy, resource accounting, transport, cancellation,
and stream machinery; they do not share an untyped request language.

Runtime constructs the initial request from the admitted plan. Received Link
metadata and GraphQL cursors are untrusted continuation data. A continuation
may change only the validated next-page component within the compiled exact
target. It cannot change scheme, host, port, path, credential placement, fixed
query fields, document, operation, or resource policy.

Pagination is sequential unless independence and consistency are proven. The
v1 package contract supports only sequential mutable Link or forward Relay
cursor traversal with one page in flight. No prefetch, parallel page work,
retry, cache, resume, deduplication, stable ordering, or snapshot claim is
implied.

Response decoding is strict and lossless for the planned structural type.
Every output batch has the planned arity and kind. Missing required fields,
wrong JSON types, lossy integers, schema drift, GraphQL errors, invalid
pagination, and resource exhaustion are terminal failures, not partial success
or fallback.

## Bounded streaming lifecycle

`ScanExecutor::Open` is deterministic and performs no network I/O. The first
pull begins the single scan deadline and the first possible request. A
`BatchStream` is pull-driven:

- `Next == true` yields one nonempty schema-aligned batch;
- `Next == false` alone means clean exhaustion; and
- any error is terminal and stable on repeated pulls.

Work is bounded by page, scan, response, record, extracted-string, generated
document/body, retained-memory, and elapsed-time ceilings. Arithmetic is
checked unsigned arithmetic; values are never wrapped, clamped, or treated as
unlimited.

Pagination respects backpressure and has at most one request in progress.
Decoded page state is released before requesting the next page. Cancellation
is checked during request construction, transport, decode, page transition,
batch transfer, compilation, and publication waiting. `Cancel`, `Close`, and
destructors are non-throwing and idempotent. Terminal failure, cancellation,
early close, or destruction releases transport, response, decoded page,
continuation, authorization, and admitted-plan state.

V1 performs one attempt. A declaration of replay safety is necessary but not
sufficient for a future retry; an uncommitted replay unit would also be
required.

## Atomic catalog publication

Package compilation and Runtime staging happen outside the Query publication
lock. At the publication point, Query:

1. acquires a cancelable DatabaseInstance-scoped publication guard;
2. rejects a stale base generation;
3. checks the complete candidate name set case-insensitively against candidate,
   active-package, table-function/overload, and table-macro owners;
4. begins one DuckDB system-catalog transaction;
5. replaces only names owned by the same connector and refreshes management
   and introspection functions against the same immutable registry snapshot;
6. commits or rolls back while still holding the guard; and
7. releases an unpublished candidate on every failure path.

There is one observable commit point: DuckDB catalog MVCC. The system does not
publish a second independently visible mutable registry.

Load fails if the connector ID is already active. Reload uses Connector's
normalized compatibility result. An identical digest is a successful no-op.
Any collision, stale publisher, cancellation, incompatibility, catalog error,
or late failure leaves every old function and registry entry usable.

Management calls are DatabaseInstance-scoped administrative operations. They
require autocommit, reject multiple invocations in one statement, perform work
only on the first materialized pull, and follow DuckDB relational demand. A
transaction that predates publication continues to observe its complete old
catalog and registry snapshot until it ends.

The DatabaseInstance-owned lifecycle sentry rejects queued and future
publication after close, lets a lock owner drain, and releases generations
after their final owners. The pinned DuckDB profile does not provide an
active-shutdown callback; dynamic extension DSO unload remains unsupported.

## Diagnostics and explanation

Connector compilation, Semantics planning, Runtime execution, and Query
publication own distinct structured errors. Query maps them at one DuckDB
exception boundary.

Safe diagnostics may contain stable codes, phases, structural identifiers,
bounded field names, and safe package-relative source coordinates. They never
contain absolute roots, YAML scalar content, secret names or values, generated
GraphQL documents, request bodies, response bodies, rows, cursors, or remote
messages.

Explanation is derived from typed immutable facts and is never parsed for
authority. It distinguishes operation/protocol identity, predicate accuracy,
residual owner, pagination strategy, resource envelope, and safe provenance
without claiming unsupported ordering, snapshot, retry, cache, or remote
relational behavior.

## Compatibility and support boundary

The implementation is a native C++ DuckDB table-function extension on the
exact tested DuckDB and platform dependency cell. The compiled types and
private builders are not a public binary plugin ABI.

`duckdb_api/v1` is the stable local-package syntax family defined by
[CONNECTOR_SPECIFICATIONS.md](CONNECTOR_SPECIFICATIONS.md). Package SemVer
does not grant execution authority. Reload compatibility is a normalized
compiled-descriptor decision, not an inference from version alone.

The product is read-only and static-schema. It does not promise writes,
cross-source transactions, snapshot consistency, remote ordering, arbitrary
GraphQL, dynamic schema discovery, package distribution, automatic connector
discovery, OpenAPI import, custom native/WASM code, providers, partitions,
retries, caching, or a custom DuckDB storage catalog. Adding those capabilities
requires an accepted contract and executable evidence; implementations must
reject unsupported declarations rather than silently ignore them.

## Verification boundary

Correctness is proven primarily with deterministic controlled fixtures:

- closed schema and YAML mutation tests;
- source identity and digest vectors;
- input/selector and relational law matrices;
- exact request, response, pagination, resource, and failure observations;
- native/package differentials for all four GitHub relations;
- actual DuckDB bind, prepare, repeat, composition, collision, reload, MVCC,
  cancellation, and shutdown tests; and
- clean-host build and direct-load evidence on the pinned product cell.

Live services are compatibility probes, not correctness oracles. They never
record credentials, personal data, response samples, or remote messages.

The internal value and lifecycle contracts are specified in
[RUNTIME_CONTRACTS.md](RUNTIME_CONTRACTS.md). Team accountability and delivery
process are defined separately in `docs/TEAM_TOPOLOGY.md` and
`docs/PRODUCT_DELIVERY.md`; they do not change runtime behavior.
