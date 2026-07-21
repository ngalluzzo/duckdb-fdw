# RFC 0013: Define the connector-package v1 contract

```yaml
rfc: "0013"
title: "Define the connector-package v1 contract"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Connector Experience"
technical_decision_owner: "Lead agent"
product_approver: "Product manager"
authors:
  - "Lead agent"
required_reviewers:
  - "connector_package_v1_connector_review"
  - "connector_package_v1_query_review"
  - "connector_package_v1_runtime_review"
  - "connector_package_v1_semantics_review"
  - "connector_package_v1_enablement_review"
affected_teams:
  - "Connector Experience"
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
linked_outcome_or_objective: "Author and query a complete local connector package through the accepted v1 subset"
supersedes: "none"
```

## Summary

Adopt exact `api_version: duckdb_api/v1` as the first connector-package
contract. It is a closed, local, read-only, static-schema authoring surface
that can express the repository's four GitHub relations without exposing the
broader draft. The contract includes ordered typed schemas and inputs,
deterministic base-operation selection, bounded REST GET and structured
GraphQL query profiles, anonymous or capability-scoped bearer authentication, one
positive predicate input, sequential Link or GraphQL cursor pagination,
mandatory resource and network narrowing, deterministic source identity,
offline fixtures, and source-located diagnostics. Unsupported syntax is
rejected, not ignored.

The spec identifier, connector-package SemVer, project SemVer, and immutable
package digest are independent identities. `duckdb_api/draft` is never a
migration source. RFC 0012 continues to own SQL names, registration, reload,
introspection, and dispatcher migration.

## Sponsorship and context

- **RFC type:** Product.
- **Sponsoring team:** Connector Experience.
- **Linked outcome:** A connector author can express, validate, fixture-test,
  and load a complete local package, and a DuckDB user can query its relations
  through the RFC 0012 SQL surface.
- **Why now:** RFC 0009 requires every retained declaration to be implemented
  in `0.8.0`. RFC 0012 cannot proceed from its proven registration coordinator
  to a real package until Connector Experience defines the exact source
  contract and immutable handoff that Query, Semantics, and Runtime consume.

The current permanent product proves four native GitHub relations through the
accepted team boundaries. It deliberately does not parse package syntax,
create package compatibility, or admit arbitrary REST and GraphQL declarations.
This RFC converts that evidence into the smallest independently authorable
contract instead of treating the broad design proposal as implementation work.

## Problem

`docs/CONNECTOR_SPECIFICATIONS.md` currently identifies itself as
`duckdb_api/draft`. It mixes intended v1 behavior with dynamic schemas, broad
type and extractor grammars, generated GraphQL, providers, partitions, retries,
caching, importers, connection-derived hosts, multiple authenticators, and
distribution concepts. Relabeling it would stabilize unimplemented behavior.

The opposite shortcut is also invalid. The current native catalog admits only
four exact repository-owned profiles, and Runtime still recognizes GitHub
connector, version, relation, request, and GraphQL-profile identities as a
closed set. Publishing those fixed identities as the authoring language would
let a user copy the bundled connector but would not let an independent author
describe another conforming API.

A concrete failure is the GraphQL relation. Its permanent plan and execution
path are proven, but Connector accepts only the exact
`GITHUB_VIEWER_REPOSITORY_METRICS_V1` bytes and identity. A package field named
`query` would be misleading unless the stable contract also defines the closed
query structure, query-only restrictions, variable sources, response paths,
generated digest, resource bounds, and failure behavior that make another
profile admissible.

The current `CompiledConnector` also has only one ambiguous `Version()` and no
package digest, spec identity, ordered input descriptors, source map, or
package-compiled origin. Query cannot derive typed generated functions from
it, and Runtime cannot safely use package SemVer or explanation text as request
authority.

## Decision drivers and invariants

- **Must preserve:** Bind, `DESCRIBE`, `EXPLAIN`, and `PREPARE` remain
  deterministic and perform no network I/O or package-source reads.
- **Must preserve:** A remote predicate `R` is admitted only when the declared
  source contract establishes that DuckDB predicate `D` implies `R`; exact
  pushdown additionally requires `R` to imply `D`. Every residual has one
  owner.
- **Must preserve:** DuckDB owns filtering, projection, ordering, limit, and
  offset unless a later accepted contract proves an exact remote capability.
- **Must preserve:** Network and credential policy only narrow host authority;
  credentials become opaque execution capabilities and never enter compiled
  source, plans, fixtures, diagnostics, or digests.
- **Must preserve:** Pagination is sequential, bounded, cancelable, and
  duplicate-preserving unless independence and consistency are separately
  proven.
- **Must preserve:** One replay-safe request receives one attempt. This
  contract creates no retry, wait, cache, or single-flight authority.
- **Must preserve:** Compiled generations and plans are immutable. Publication
  and reload are atomic and never mutate in-flight owners.
- **Must enable:** Another author can describe a useful fixed-origin REST API
  and a query-only Relay-style GraphQL API without connector-specific native
  code.
- **Must enable:** The repository-owned GitHub package reproduces all four
  native relations, including mixed auth, the superset predicate, both
  pagination modes, nullable GraphQL data, and resource ceilings.
- **Must enable:** Query receives structural types, values, order, source
  references, and auth shape without parsing YAML, type strings, request
  templates, or protocol alternatives.
- **Must enable:** Unsupported fields, versions, dialects, and capability
  profiles fail at their exact source locations before publication or I/O.
- **Must not introduce:** A partial compiler whose accepted syntax is ignored,
  hard-coded to package identity, or executable only through a test path.
- **Must not introduce:** Discovery, fetching, distribution, dependencies,
  lockfiles, signing, trust, aliases, unload, connection profiles, or a public
  C++ ABI.

## Proposed decision

### Public behavior

#### Stable identities

Every machine-readable source file carries exact
`api_version: duckdb_api/v1`. The value is one frozen compatibility family,
not the extension version and not a package version. Validators reject absent,
unknown, and `duckdb_api/draft` identifiers without attempting inference or
migration.

The identities are:

| Identity | Form | Meaning |
| --- | --- | --- |
| Spec | exact `duckdb_api/v1` | Closed author syntax and semantics selected by this RFC |
| Project | project SemVer, such as `0.8.0` | Extension implementation and release compatibility |
| Package | canonical core SemVer `MAJOR.MINOR.PATCH` in manifest `version` | Author-owned release lineage for one connector ID |
| Generation | spec, connector ID, package version, SHA-256 digest | One immutable compiled package source generation |

The project advertises which exact spec identifiers it implements. Accepting
this RFC commits the `0.8.0` implementation to the entire `duckdb_api/v1`
surface; `0.9.0` broadens independent-author and migration evidence rather
than adding fields. The stable project line supports the accepted v1 family
according to the project's release and support policy. A future incompatible
author grammar uses a new exact spec identifier.

Package version components use exact decimal
`0|[1-9][0-9]*`, fit unsigned 32-bit integers, and are compared
lexicographically as integers. Leading zeroes, signs, omitted components,
prerelease labels, and build metadata fail. Project releases continue to use
full SemVer independently. Package SemVer never grants execution authority and
never overrides compiled compatibility checks. The connector ID is the logical
owner used by RFC 0012; the directory name and absolute path are not identity.

#### Canonical package root and source digest

A package root contains exactly one manifest entry point and externally stored
relations:

```text
connector.yaml
relations/<relation_id>.yaml
fixtures/index.yaml                              # author evidence, not runtime identity
fixtures/...                                     # author evidence, not runtime identity
README.md                                        # human documentation, not runtime identity
```

The manifest contains an ordered list of relation IDs. Each ID resolves only
to `relations/<id>.yaml`; inline relations and arbitrary relation paths are not
part of v1. Compilation never scans a directory to discover semantic input.

`package_root` is an explicit absolute local directory. The loader performs no
tilde, environment, URL, search-path, or default-directory expansion. It opens
every absolute root component without following symbolic links, retains the
opened canonical root privately for reload, then opens `relations` and semantic
package-relative leaves from those directory handles without following links.
Ordinary load neither opens nor enumerates `fixtures`; explicit fixture tooling
opens it under the separate fixture-leaf budget. Symbolic links, hard-linked
leaves, special files, `.` or `..`, absolute
child paths, backslashes, duplicate normalized paths, case-colliding paths, and
files whose identity changes during the read fail.

Source files are one UTF-8 YAML 1.2 document using the failsafe schema: every
scalar is initially text, and field-role validation performs all Boolean,
integer, enum, identifier, and string interpretation. UTF-8 BOM, CR, tabs used
as indentation, explicit tags, single-quoted and block scalars, duplicate keys,
aliases, anchors, merge keys, and multi-document streams fail. Plain and
double-quoted scalars are admitted; double-quoted scalars accept only JSON
string escapes. The plain token `null` is text and is admitted only as the
exact `kind` discriminator of a documented null-value object; `~` has no valid
role. Boolean fields accept only plain
`true` or `false`; integer fields accept only their documented canonical
decimal grammar; identifiers and enums accept only their documented token
grammar. All remaining scalar fields are text after failsafe decoding, so a
plain header value `true`, `+443`, or `01` remains text and cannot be resolved
as another YAML type. Unknown fields fail.

Two normative, content-addressed decision artifacts close every nested source
shape after that lexical pass:

- [`connector-package-v1.schema.json`](evidence/0013/connector-package-v1.schema.json),
  raw-byte digest
  `sha256.f094044c9f2e46d25ad379bf89b16730d1779090150cf0cc278ff5fcc4e1d0f6`,
  defines every connector, credential, policy, relation, column, input,
  resource, selector, operation, REST, GraphQL, pagination, and predicate key,
  requiredness, structural alternative, enum, collection bound, and lexical
  form.
- [`fixture-index-v1.schema.json`](evidence/0013/fixture-index-v1.schema.json),
  raw-byte digest
  `sha256.3dd9ec84a5e0cbaddf23dba187e983d80aecfda83d8075c1109f6f032b0cf7c9`,
  does the same for offline evidence.

The schemas validate the failsafe decoded tree; the cross-reference,
type-dependent scalar, numeric-range, uniqueness, policy-intersection, and
relational laws stated in this RFC run afterward. A schema digest mismatch is
an implementation/source-identity failure. The schemas are decision evidence,
not semantic package members, and must be copied into permanent Connector-owned
product source byte-for-byte during accepted-contract propagation.

The complete bounded decision-evidence file set is indexed by
[`evidence-manifest.json`](evidence/0013/evidence-manifest.json) at raw-byte
digest
`sha256.49407e412bd0863fd9d14d881e067e3653bd66dc39e901bf8cceb7f76888f128`.
The manifest binds every evidence artifact and the repository verifier by
SHA-256. Running
`ruby scripts/verify-rfc-0013-evidence.rb` checks that closed file set, each
content identity and RFC citation, both normative schemas against the failsafe
decoded examples, semantic package and payload identities, GraphQL document
and request-body vectors, the independently derived GitHub coverage golden,
and every declared unsigned 64-bit resource envelope.

The semantic source set is `connector.yaml` plus exactly the listed relation
files. Unlisted relation YAML fails rather than becoming stale shadow source.
Before the first source read, the loader enumerates the opened root and
`relations` directories and records their sorted entry sets plus every admitted
entry's device, inode, file type, link count, size, modification time, and
change time. It opens leaves relative to those directories without following
links, compares `fstat` before and after each bounded read, then repeats the
complete directory enumeration and identity capture after the last read. Any
entry-set or identity difference across that interval fails
`DUCKDB_API_PACKAGE_IDENTITY`; the loader does not assemble a candidate from
multiple observed filesystem states. Compilation and publication consume only
the resulting bounded immutable byte snapshot.

`package_digest` uses the repository's named
`sha256-length-prefixed-path-and-bytes-v1` algorithm. Normalize admitted paths
to UTF-8 relative POSIX paths, sort them bytewise, then hash, for every file,
`u64be(path_length) || path_bytes || u64be(content_length) || raw_content`.
The public value is `sha256.<64 lowercase hex>`. Absolute root, file metadata,
README, fixtures, editor files, and unreferenced files are excluded. Copies at
different roots therefore retain identity; any byte change to a semantic
source, including comments, line endings, or key ordering, intentionally
changes source identity. YAML is never re-emitted or normalized for hashing;
semantically equivalent source may still compile to identical normalized IR
and explanation.

#### Management and compiler budgets

V1 chooses finite ceilings for source admission and author evidence. A host may
configure a lower ceiling for any row; the effective value is the minimum of
the host and spec values. A package above either ceiling fails before the
corresponding allocation, parse, or publication step. Zero never means
unlimited.

Every positive page, record, byte, document, body, and pagination declaration
uses canonical decimal in the closed unsigned 64-bit range
`1..18446744073709551615`. The schema rejects more than 20 digits and the
post-schema numeric pass rejects a 20-digit value above that maximum. Compiler
and Runtime arithmetic uses checked unsigned 64-bit addition and
multiplication for every page increment, page-times-count envelope,
request/response accumulation, and derived scan maximum. Source-time overflow
fails `DUCKDB_API_POLICY_WIDENING`; a received continuation that would overflow
fails Runtime `policy` at `pagination.next` before another request. No value is
clamped, wrapped, promoted to implementation-dependent precision, or treated
as unlimited.

| Resource | Exact v1 ceiling |
| --- | ---: |
| Semantic files | 65: one manifest plus at most 64 relations |
| Root and `relations` directory entries | 4,096 and 1,024; 5,120 aggregate |
| One semantic YAML file | 1 MiB raw bytes |
| Aggregate semantic snapshot | 16 MiB raw bytes |
| Package-relative path | 255 UTF-8 bytes |
| YAML nesting | 32 mapping/sequence levels |
| YAML nodes | 100,000 across the semantic snapshot |
| Decoded YAML scalar | 1 MiB UTF-8 bytes |
| Entries in one YAML mapping or sequence | 4,096 |
| Credentials and network origins | 64 of each per manifest |
| Columns, relation inputs, operations, predicates | 256, 128, 64, and 64 per relation |
| Request query fields, fixed arguments, or fixed headers | 64, 64, and 32 per operation |
| Generated GraphQL document | 64 KiB, further narrowed by the declared relation ceiling |
| Stable compiler diagnostics | 256 records per management call |
| Fixture cases and response pages | 1,024 cases and 32 pages per case |
| Fixture index and one payload | 4 MiB and 8 MiB raw bytes |
| Aggregate fixture payloads and fixture leaves | 256 MiB and 4,096 leaves |

The YAML event reader enforces byte, depth, node, scalar, and container-entry
limits before constructing the semantic tree. The compiler enforces entity
counts while building each immutable relation. Directory enumeration checks
cancellation before the first entry and after each group of at most 64 entries,
and rejects before retaining an entry beyond the per-directory or aggregate
ceiling; only the resulting bounded entry set is sorted. On the 256th detail
candidate, the compiler retains the first 255 details and uses the final slot
for one `DUCKDB_API_RESOURCE_EXHAUSTED` record at the package-root coordinate;
later details are discarded. Management still returns the first canonically
ordered diagnostic. Fixture
limits apply only to explicit fixture tooling; ordinary load never reads
`fixtures/`. Boundary, one-over, allocation-failure, and cancellation cases
are mandatory compiler and fixture-runner evidence.

#### Closed manifest schema

The v1 manifest admits only:

| Field | Required | Contract |
| --- | ---: | --- |
| `api_version` | yes | Exact `duckdb_api/v1` |
| `kind` | yes | Exact `connector` |
| `id` | yes | Explicit v1 identifier; never derived from path |
| `version` | yes | Canonical core package SemVer defined above |
| `extractor_dialect` | yes | Exact `duckdb_api/json_path_v1` |
| `credentials` | no | Named bearer capabilities described below |
| `network_policy` | yes | HTTPS origins and deny-only address/redirect policy |
| `relations` | yes | Non-empty ordered unique relation-ID list |

Human display name, publisher, license, and description belong in README for
v1. The manifest rejects `base_url`, `connection`, `constants`, shared HTTP
defaults, rate limits, retry policies, cache, inline relations, package
dependencies, signatures, and author-controlled SQL names.

`duckdb_api/json_path_v1` is structural, not a general JSONPath or JQ engine.
Column extracts use exact
`\$(\.[A-Za-z_][A-Za-z0-9_]*)+`; a terminal-collection response uses that
grammar followed by exact `[*]`. Field names use decoded JSON string identity
and are represented as explicit path-segment sequences in compiled data.
Bracket member syntax, filters, interior arrays, recursive descent, unions,
slices, functions, arithmetic, scripts, and JQ are invalid.

Every connector, relation, column, input, operation, credential, predicate,
query-field, and fixture-case ID uses ASCII
`[a-z][a-z0-9_]{0,62}` and is unique in its containing scope. The exact
63-byte bound applies before Query derives an unquoted SQL name. GraphQL
operation, argument, variable, and field names instead use the GraphQL `Name`
grammar, may not begin with reserved `__`, and never become SQL identifiers.

#### Closed relation and type schema

Each relation file admits only:

| Field | Required | Contract |
| --- | ---: | --- |
| `api_version` | yes | Exact `duckdb_api/v1` |
| `kind` | yes | Exact `relation` |
| `id` | yes | Must equal the manifest ID and file-derived ID |
| `schema` | yes | Exact `static` |
| `columns` | yes | Non-empty ordered column sequence |
| `inputs` | no | Ordered independent relation-input sequence |
| `auth` | yes | `anonymous` or one manifest credential reference |
| `resources` | yes | Positive relation page/scan ceilings |
| `operations` | yes | Non-empty ordered named base-operation sequence |
| `predicates` | no | Closed positive mapping sequence described below |

Columns and inputs admit only structural `BOOLEAN`, `BIGINT`, and `VARCHAR`
types in v1. A column has explicit `id`, `type`, `nullable`, and structural
`extract` path. An input has explicit `id`, `type`, `nullable`, and optional
typed `default`. Column defaults, hidden columns, nested types, numeric
coercion, timestamps, JSON values, patterns, ranges, enums, and sensitivity
flags are excluded. Conversion is strict: type mismatch, overflow, missing
non-null data, and JSON null for a non-null column fail.

An absent `default` key means no default. A present default is exactly one of:

```yaml
default: {kind: null}
default: {kind: value, value: true}
default: {kind: value, value: -42}
default: {kind: value, value: "private"}
```

`kind: null` is valid only for a nullable input and is the only null-default
spelling. `kind: value` must carry one scalar matching the declared type:
`BOOLEAN` accepts only plain YAML 1.2 `true` or `false`; `BIGINT` accepts only
plain canonical decimal `-?(0|[1-9][0-9]*)` in the signed 64-bit range, with
no plus sign, leading zero, separator, exponent, or base prefix; and `VARCHAR`
accepts only a double-quoted scalar using JSON string escapes and containing no
NUL or other C0 control character. Bare `null`, `~`, YAML tags, implicit
numeric coercion, and a scalar default without the discriminated map fail.

Introspection renders defaults as `NULL`, `TRUE`/`FALSE`, canonical signed
decimal, or a single-quoted DuckDB string literal with each single quote
doubled. Default presence is carried separately, so no rendered value is used
as planning authority. An explicit SQL `NULL` remains a caller value: it
suppresses a default when the input is nullable and fails when non-nullable.

The identifier `secret` is reserved and rejected at the input source location
for every relation, including anonymous relations. Query synthesizes the
separate `secret VARCHAR` SQL argument only for an authenticated relation, as
decided by RFC 0012.

All relation-origin SQL arguments remain omittable at Query bind. Query
preserves omission separately from explicit typed SQL `NULL`. Semantics owns
nullability, default application, source conflicts, candidate eligibility, and
operation selection. Missing operation inputs are planning diagnostics, not
Query binder-requiredness.

Semantics carries each declared input as exactly `UNBOUND`, `BOUND_NULL`, or
`BOUND_VALUE`. An omitted argument starts `UNBOUND`; a default applies only to
that state. Explicit SQL `NULL` produces `BOUND_NULL` and suppresses a default.
Non-nullable `BOUND_NULL` fails planning. `when.required_inputs` is satisfied
only by `BOUND_VALUE`; a nullable null is present for introspection but cannot
make a remote operation eligible.

A REST query binding to a declared relation input has exact source spelling
`input: <id>` plus required
`omit_when_unbound: true` and `omit_when_null: true`. V1 has no emit-null
encoding. `UNBOUND` and `BOUND_NULL` omit that query field; `BOUND_VALUE` uses
the type encoding below. A nullable/default-null input referenced by a request
is valid only through this omission profile. Any other null disposition or a
required selector that can only receive null fails compilation or candidate
eligibility before a plan exists.

#### Base operations and REST

An operation has an ID, cardinality (`one` or `many`), optional required
inputs, at most one relation-wide fallback, protocol request, response declaration,
and replay safety. V1 selection admits only `when.required_inputs` plus the
sole fallback. Semantics resolves every non-fallback using candidate-local
bindings, ranks candidates by satisfied required-input count, evaluates the
fallback only when no non-fallback is eligible, and fails a highest-rank tie or
the absence of an eligible fallback. Author ordering never resolves a tie.
Alternative input sets, forbidden-input selectors, and author priorities are
excluded.

Each required-input reference is explicitly `input.<id>` for a declared
relation input or `conditional.<id>` for an operation-local predicate input.
For each candidate, Semantics first resolves explicit values and defaults,
then applies only that operation's predicate mappings to derive typed
conditional inputs, then evaluates required references. A conditional
reference is satisfied only when that mapping emitted one value for the same
operation. Equal top ranks fail; the fallback is considered only after all
non-fallback candidates are ineligible. Connector compiles these names to
tagged references, so consumers never infer namespace from a string.

REST v1 admits only:

- HTTPS `GET`, always replay-safe and always one attempt;
- a typed exact origin admitted by manifest network policy;
- a fixed structural path with no interpolation;
- ordered fixed non-sensitive headers;
- ordered query fields whose values are either fixed UTF-8 literals or one
  typed relation input encoded as one query value, plus at most one omitted-
  when-unbound conditional input targeted by the selected predicate mapping;
- `root_object`, `root_array`, or terminal collection-path response sources;
- `one` only with a root object and `many` with a collection source; and
- disabled pagination or sequential mutable `Link: rel=next` traversal bounded
  to the exact operation origin and path.

Fixed header names are ASCII `[A-Za-z][A-Za-z0-9-]{0,62}`, compared
case-insensitively, unique, and serialized exactly once. Values are 0 to 1024
bytes of visible ASCII plus horizontal tab; leading/trailing whitespace, CR,
LF, NUL, other controls, and obsolete folding fail. At most 32 fixed headers
and 16 KiB of combined name/value bytes are admitted before the lower host
header budget is applied.

The following case-insensitive names are Runtime-owned and invalid as fixed
author headers: `Authorization`, `Proxy-Authorization`, `Host`, `Connection`,
`Content-Length`, `Transfer-Encoding`, `Trailer`, `TE`, `Upgrade`,
`Keep-Alive`, `Proxy-Connection`, `Expect`, `Range`, `Cookie`, `Set-Cookie`,
`Accept-Encoding`, and any name containing `token`, `secret`, `api-key`, or
`apikey`. GraphQL `Content-Type: application/json` is compiler-generated and
cannot be overridden. Fixed values are public package source, never credential
bindings; credential-looking mutation cases fail before compilation, while
the explicit local-package trust boundary still holds for deliberately
misleading third-party literals.

REST query names use exact ASCII `[A-Za-z0-9._~-]{1,63}` and are unique per
request. V1 has one exact encoding,
`form_urlencoded`: input is UTF-8; ASCII alphanumeric plus `-._~` is emitted
unchanged, space becomes `+`, and every other byte becomes uppercase `%HH`.
An author supplies decoded fixed/input values, never pre-encoded bytes.
`BOOLEAN` becomes `true`/`false`, `BIGINT` canonical decimal, and `VARCHAR` its
exact UTF-8 value before encoding. Encoded names/values are bounded by the
host request-target ceiling; malformed UTF-8, NUL, control characters, or an
unsupported encoding fails compilation.

Link pagination declares the initial page-size and page-number query fields,
first page, increment, and maximum pages. A received target is structural
untrusted state: Runtime rejects any scheme, authority, path, user-info,
fragment, or target-scope change before sending it. Resume, totals, parallel
pages, other link relations, token, offset, page-number synthesis, request
bodies, status remapping, and arbitrary templates are excluded.

The accepted `next` target query is exact. It contains the same query-field
multiset and exact encoded values as the admitted current request except that
the unique page-number field is canonical decimal
`current_page + page_increment`. The page-size, fixed, explicit-input, and
selected conditional-input fields must remain present and unchanged; unbound
omitted fields must remain absent. Non-page names and values compare byte for
byte with the compiler's canonical encoded request, including any required
uppercase `%HH`; malformed escapes, lower-case or alternate encodings, and
decoded-then-reencoded equivalence fail. The page-number value alone must be
unescaped canonical decimal. Duplicate, missing, extra, replayed, or
skipped-page targets fail. Runtime uses only the validated next-page integer
and reconstructs the request from the compiled operation; it never sends the
received target. Positive and mutation evidence includes percent-encoded
fixed, explicit-input, and selected conditional-input values.

#### Structured GraphQL query profile

GraphQL v1 admits one structured, fixed-full-selection Relay query profile,
not an opaque author document and not projection-driven generation. The author
declares a query operation name, endpoint, root field path, fixed literal root
arguments, page-size and cursor argument names, and the complete field path for
every static result column. The closed literal grammar contains GraphQL null,
boolean, integer, string, enum, list, and object values; it contains no
variables, directives, fragments, source text, or raw interpolation.

Connector deterministically generates one named `query` document from those
facts, the fixed page-size and Runtime cursor variables, the complete result
selection, and `pageInfo { hasNextPage endCursor }`. It parses its own generated
bytes as a validation backstop and computes their SHA-256 identity. Mutation,
subscription, introspection, caller-provided document bytes, credential
literals, conditional directives, fragments, and extra operations are
unrepresentable.

The renderer is exact `duckdb_api/graphql_relay_query_v1`. It emits UTF-8 with
LF, two-space indentation, no blank or trailing-whitespace lines, and no final
newline. The header is `query NAME($page: Int!, $cursor: String) {` using the
declared variable names. Intermediate root fields each open on their own line.
The final connection field opens `field(`; arguments appear one per line in
this exact order: page size, cursor, then fixed arguments in source order; `)`
and `{` share the closing argument line as `) {`. The page and cursor values
render as `$name`.

Fixed literals render on one line: `null`, `true`, `false`; canonical decimal;
an exact double-quoted string; GraphQL enum; `[item, item]`; or
`{name: value, name: value}` preserving declared list/object order. String
rendering emits quotation mark and reverse solidus as `\"` and `\\`, uses
`\b`, `\f`, `\n`, `\r`, and `\t` for those five controls, emits every other C0
byte as lower-case `\u00hh`, and preserves solidus plus valid non-ASCII UTF-8
without escaping. Invalid UTF-8 and isolated surrogate values are
unrepresentable. Names use the GraphQL `Name` grammar, object names are unique,
and rendered literal size is bounded by the document ceiling.

The connection body emits `nodes {`, then exactly one selection line per
column in column order. V1 field paths contain one segment (`field`) or two
segments (`parent { child }`); duplicate paths, aliases, a shared two-segment
parent, or a prefix conflict fail. It then emits one inline
`pageInfo { hasNextPage endCursor }` line using the declared field names and
closes the connection and root braces in nesting order. The response node and
page-info paths are `data` plus the root path plus `nodes`/`pageInfo`; errors
are exact root `errors`.

The node collection field is exact `nodes`. `page_info_field` must differ from
`nodes`; `has_next_page_field` must differ from `end_cursor_field`; and neither
pagination leaf may equal a selected field at the same response-object level.
The root path may not repeat a segment or use `data`, `errors`, `nodes`, or the
declared pagination names in a way that aliases two structural response roles.
These checks run during source compilation, and alias mutations fail with
`DUCKDB_API_INVALID_GRAPHQL_PROFILE` before a plan or remote validation exists.

Page-size and cursor variable names are distinct. Page-size, cursor, and fixed
argument names are pairwise disjoint at the connection field; fixed argument
names and object-field names are unique. Root, selection, pagination,
operation, argument, and variable names beginning `__` fail source compilation
with `DUCKDB_API_INVALID_GRAPHQL_PROFILE`. The generated-query parser backstop
runs before a `ScanPlan`, credential resolution, or network authority exists.

[`evidence/0013/graphql-query-golden.yaml`](evidence/0013/graphql-query-golden.yaml)
binds the proposed GitHub YAML to the native 581-byte document and
`sha256.9d3d78e2214669f11b9caabc2a7f062e2985f9da9628485f124e1f24e3a50c85`.
Every compiler and migration oracle must reproduce both bytes and digest.

The profile admits:

- one HTTPS endpoint origin/path and fixed non-sensitive headers governed by
  the REST header grammar and reserved-name rules;
- one fixed non-null integer page-size variable and one nullable Runtime-owned
  cursor variable;
- structural node, error, page-info, has-next-page, and end-cursor paths
  derived from the declared root and fixed Relay shape;
- result-column paths relative to a node with `VARCHAR`, `BIGINT`, or `BOOLEAN`
  scalar conversion and declared nullability;
- fail-on-any-nonempty-error behavior;
- forward, sequential, mutable Relay pagination with concurrency one and a
  positive maximum page count; and
- explicit per-request and per-scan serialized-body ceilings.

Caller-input or secret variables, projection-sensitive selection, schema introspection,
partial-data acceptance, independent pages, stable-snapshot claims, resume,
totals, retry, cache, and providers are invalid. A fixed ordering argument in
the generated query describes cursor enumeration only and grants no DuckDB ordering
or limit capability.

#### Authentication, network, and resources

A manifest credential is a logical bearer capability with exact secret field
`token`, exact `Authorization` placement, and a non-empty set of exact HTTPS
destination origins. It contains no credential value or DuckDB secret name.
Relations explicitly select `anonymous` or one credential, allowing the same
package to contain both.

The network policy admits exact lower-case DNS hosts, HTTPS, and explicit
ports. Redirects, private addresses, link-local addresses, and loopback
addresses are all `deny`; v1 has no opt-in state, wildcard, host source, proxy,
or TLS override. Credential destinations must be a subset of the network
origins. Runtime intersects all declarations with host policy and rechecks the
fully resolved request before materializing the bearer value.

A host is 1 to 253 ASCII bytes, has no trailing dot, and consists of one or
more lower-case DNS labels separated by `.`, each 1 to 63 bytes, beginning and
ending in `[a-z0-9]` with interior `[a-z0-9-]`; IP literals and Unicode host
spellings are invalid. A port is canonical decimal in `1..65535`. A fixed path
is 1 to 2,048 ASCII bytes, starts with `/`, and contains only non-empty
`[A-Za-z0-9._~-]+` segments separated by single `/`; root `/` is valid, while
percent escapes, query/fragment delimiters, backslashes, empty interior
segments, and dot-only traversal segments fail. Origins compare the exact
typed scheme, host, and port rather than a reconstructed URL string.

The closed `network_policy` object has exactly `origins`, `redirects`,
`private_addresses`, `link_local_addresses`, `loopback_addresses`, and
`max_response_bytes`. `origins` is a non-empty unique sequence of exact
`{scheme: https, host, port}` objects; the four policy values are required and
must be `deny`. `max_response_bytes` is a required positive per-response wire
ceiling. Every relation `max_response_bytes_per_page` must be less than or
equal to it. The effective page ceiling is the minimum of manifest, relation,
and host values; the relation scan ceiling is separately intersected with the
host scan ceiling. Unknown policy fields and contradictory or widening values
fail with `DUCKDB_API_POLICY_WIDENING`.

Each relation declares positive maxima for response bytes and records per page
and scan plus one `max_extracted_string_bytes` ceiling per decoded scalar.
Paginated relations additionally
declare positive page and scan envelopes whose checked products cannot overflow
and whose scan bounds cannot exceed the page bound times maximum pages.
GraphQL adds request-body bounds. Header, decompression, memory, batch,
wall-time, and concurrency ceilings remain host-owned and can only narrow the
compiled plan.

#### Predicates and local relational ownership

V1 admits at most one emitted conditional remote input per operation. A
mapping is one typed literal equality on one returned column, bound to one
operation-local conditional input and one REST query field on named operations.
Conditional inputs are compiled predicate/request facts, not declared relation
inputs and therefore not Query arguments. It declares
`exact` or `superset` and references mandatory positive, false/NULL, and
duplicate-occurrence fixture cases. The compiler derives the proof identity
and base domain from package digest, relation, operation, request encoding,
and complete bounded pagination profile; authors do not name internal proof or
domain enums. Every proof case contains paired `base` and `restricted`
executions over one finite occurrence-labelled domain. `occurrence_ids` are
fixture-only unique labels aligned one-for-one with decoded rows; value-equal
duplicates have different labels. The runner evaluates the declared DuckDB
equality on every base occurrence using three-valued logic and verifies that
rows carrying the same label are value-identical across executions.

For `superset`, every base occurrence for which DuckDB evaluates `TRUE` must
appear in the restricted bag with the same multiplicity; restricted `FALSE` or
`NULL` occurrences may remain. For `exact`, the restricted occurrence bag must
equal exactly the base `TRUE` bag. No restricted label may be absent from the
base domain. The mandatory positive case contains a `TRUE` occurrence, the
false/NULL case contains every non-TRUE state admitted by the column's
nullability (only `FALSE` for a non-null column), and the duplicate case
contains at least two value-equal `TRUE` occurrences with distinct labels. A
mapping cannot claim conjunction, disjunction, negation, complement, fan-out,
or more than one request input.

The author owns the truth of the upstream-domain assertion; Connector owns
schema/reference consistency and mandatory occurrence-domain fixtures; Semantics
owns implication, three-valued logic, operation-local applicability, residual
ownership, and conservative fallback. A local package is explicitly loaded
source, not a product certification of its upstream claim. Fixtures are
necessary evidence but cannot prove every behavior of a mutable upstream API;
accepting this explicit local-package trust boundary is a reserved product
decision. Explain output identifies the package digest, mapping, declared accuracy, domain, emitted
input, and residual owner.

The v1 adapter retains every offered DuckDB predicate locally, including an
`exact` mapped atom. Accuracy controls whether emitting the remote restriction
is safe and how it is explained; it does not transfer residual ownership or
authorize removal. If the capability profile or DuckDB query structure is
absent, ambiguous, compound, or unsupported, no remote input is emitted and
DuckDB still retains the predicate. Residual removal requires a later accepted
adapter-capability and ownership change.

Projection, ordering, remote limit, and remote offset declarations are not in
v1. Projection closure is local: Runtime fetches every source column needed by
the requested output and DuckDB residuals. DuckDB performs final projection,
ordering, limit, and offset. This deliberately narrows RFC 0009's intended
candidate; accepting that narrowing is a reserved product decision.

#### Diagnostics and offline fixtures

Compiler diagnostics use the following closed code vocabulary:

| Code | Meaning |
| --- | --- |
| `DUCKDB_API_UNSUPPORTED_SPEC` | Absent, draft, or unknown `api_version` |
| `DUCKDB_API_UNSUPPORTED_DIALECT` | Unknown extractor dialect |
| `DUCKDB_API_MALFORMED_YAML` | Encoding, document, duplicate-key, alias/tag, or syntax failure |
| `DUCKDB_API_UNKNOWN_FIELD` | Field outside the closed schema |
| `DUCKDB_API_MISSING_FIELD` | Required field absent |
| `DUCKDB_API_DUPLICATE_ID` | Identifier duplicate or case/path collision |
| `DUCKDB_API_INVALID_REFERENCE` | Missing or wrong-kind cross-reference |
| `DUCKDB_API_INVALID_IDENTIFIER` | Value outside the exact identifier grammar |
| `DUCKDB_API_INVALID_TYPE` | Unsupported type or typed scalar/default mismatch |
| `DUCKDB_API_INVALID_EXTRACTOR` | Path outside `json_path_v1` |
| `DUCKDB_API_RESERVED_INPUT` | Declared relation input is `secret` |
| `DUCKDB_API_UNSUPPORTED_DECLARATION` | Well-formed design-only capability or value |
| `DUCKDB_API_INVALID_SELECTOR` | Fallback, required-input, eligibility, or tie declaration invalid |
| `DUCKDB_API_INVALID_PREDICATE` | Mapping, proof fixture, domain, or encoding invalid |
| `DUCKDB_API_INVALID_GRAPHQL_PROFILE` | Structured query cannot produce the closed query-only profile |
| `DUCKDB_API_POLICY_WIDENING` | Network, credential, or resource declaration exceeds authority |
| `DUCKDB_API_RESOURCE_EXHAUSTED` | Compiler or fixture-tool spec/host budget is exhausted |
| `DUCKDB_API_PACKAGE_IDENTITY` | Root/source snapshot, digest, or immutable version identity fails |
| `DUCKDB_API_FIXTURE_MISMATCH` | Fixture index, request, payload, rows, diagnostic, or explanation differs |
| `DUCKDB_API_INCOMPATIBLE_RELOAD` | Candidate transition is not reload-compatible |
| `DUCKDB_API_PUBLICATION_CONFLICT` | RFC 0012 name or generation publication refuses atomically |

Code-to-phase assignment is closed:

| Phase | Codes and triggering stage | Source coordinates |
| --- | --- | --- |
| `source` | `PACKAGE_IDENTITY` for root, path, entry-set, file-identity, snapshot, or digest failure; `DUPLICATE_ID` only for case-colliding paths; `RESOURCE_EXHAUSTED` for directory, path, file-byte, or aggregate-snapshot bounds | A failing admitted leaf carries `file` only; a YAML byte offset additionally carries line/column; root or entry-set failures omit coordinates |
| `syntax` | `MALFORMED_YAML`; `RESOURCE_EXHAUSTED` for YAML depth, node, scalar, or container-entry bounds | Always `file`, line, and column at the first invalid or over-budget token |
| `schema` | `UNSUPPORTED_SPEC`, `UNSUPPORTED_DIALECT`, `UNKNOWN_FIELD`, `MISSING_FIELD`, semantic `DUPLICATE_ID`, `INVALID_IDENTIFIER`, `INVALID_TYPE`, `INVALID_EXTRACTOR`, `RESERVED_INPUT`, `UNSUPPORTED_DECLARATION`; `RESOURCE_EXHAUSTED` for entity counts | Always `file`, line, and column at the rule below |
| `reference` | `INVALID_REFERENCE` | Always the reference coordinate; `related` carries the wrong-kind or conflicting declaration when present |
| `compile` | `INVALID_SELECTOR`, `INVALID_PREDICATE`, `INVALID_GRAPHQL_PROFILE`, `POLICY_WIDENING`; `RESOURCE_EXHAUSTED` for generated-document, diagnostic, or normalized-generation bounds | Always the controlling declaration coordinate; cross-declaration conflicts use `related` |
| `fixture` | `FIXTURE_MISMATCH`; `RESOURCE_EXHAUSTED` for index, payload, case, page, or fixture aggregate bounds | Index/schema errors carry `fixtures/index.yaml`, line, and column; payload byte/digest errors carry only the payload `file`; execution mismatches carry the case coordinate |
| `compatibility` | `PACKAGE_IDENTITY` only for immutable version/digest reuse; `INCOMPATIBLE_RELOAD` for structural or version classification | No source coordinates; safe connector ID may be present |
| `publication` | `PUBLICATION_CONFLICT` | No source coordinates; safe connector/relation IDs may be present |

The public diagnostic record fields are exact: `code` from the table; `phase`
from `source`, `syntax`, `schema`, `reference`, `compile`, `fixture`,
`compatibility`, or `publication`; optional package-relative POSIX `file`;
optional one-based unsigned `line` and `column`, present together only with
`file`; optional `$`-rooted `yaml_path`; optional already validated
`connector`, `relation`, `operation`, and `fixture_case` IDs; and at most one
`related` location containing only `file`, `line`, `column`, and `yaml_path`.
No other field is stable or safe.

YAML sources require LF line endings and reject a BOM, making coordinates
portable. Line and column are one-based and count Unicode scalar values, not
UTF-8 bytes; a tab inside an admitted double-quoted value counts once. A token
error anchors at that token's first scalar value. A missing field in a
non-empty block mapping, including a sequence-item mapping, anchors at the
first present key token in source order. In an empty flow mapping it anchors at
`{`; an empty block mapping has no admitted spelling. A missing root field in a
non-empty root mapping uses its first key, and an empty root document or flow
mapping uses line 1 column 1. A missing required non-empty sequence anchors at
the sequence field's key. A cross-reference error anchors at the reference and
places the declaration, when one exists, in `related`. A synthesized
file-wide YAML error anchors at line 1 column 1. Coordinate presence otherwise
follows the phase matrix exactly.

Multi-error author tooling uses this total order: `phase` in the vocabulary
order above; UTF-8 `file` bytes with absent last; `line` and `column` with
absent last; `code`; `yaml_path` with absent last; connector, relation,
operation, and fixture-case IDs with absent last; then the same tuple for
`related`. If all stable fields are equal, the records are identical and one
is retained. RFC 0012 management failure returns the first record in that
order. Absolute roots, source snippets, generated query contents,
user-supplied secret names, credential values, authorization headers, response
values, received URLs, cursors, and dependency exceptions are never diagnostic
fields.

`fixtures/index.yaml` is a schema-backed ordered offline author-evidence index
bound to the semantic package digest. Each case names one relation and
operation, explicit typed inputs, an optional supported equality predicate, a
tagged execution, occurrence-proof, or pre-request failure transcript,
normalized request oracles, expected typed rows or safe error category, and a
canonical explanation. Each
payload is named by the index and carries its own SHA-256. Exact
index/file-set agreement rejects unindexed payloads and stale references, but
does not by itself prove coverage.

Coverage is derived independently from the immutable compiled feature facts.
Connector owns the project-shipped exact
`duckdb_api/fixture_coverage_v1` mapping from each operation, auth mode, input
state, conversion, predicate proof role, pagination mode and termination,
GraphQL error/null/cursor state, resource boundary, cancellation point, and
stable failure category to canonical coverage keys. The normative mapping,
scope/variant expansion, key grammar, construction rule, evaluation order, and
diagnostic set are frozen in
[`fixture-coverage-v1.json`](evidence/0013/fixture-coverage-v1.json) at raw-byte
digest
`sha256.8a19c50eaa87e3655ae8c5f2a5511bfc6d7cfa3499e9f14583af40c1c54f36f8`.
The compiler derives the required set from the independently schema-validated
normalized descriptor before opening `fixtures/`; neither the fixture index
nor its payloads can influence that set. The mapping is not editable by a
package author.

Each fixture case declares unique `covers` keys. Acceptance fails if the
complete derived key set and the indexed key set differ, if one key is covered
twice, or if a case does not execute the named scope and variant through the
production path. Deleting a feature declaration may change the independently
derived set and package behavior and is therefore governed by compatibility;
deleting only a case and payload leaves its source-derived key uncovered.

Pagination cases provide each page in request order. Auth cases provide only
`anonymous`, `bearer_present`, or `bearer_missing` capability state, never a
secret name or bytes. `bearer_missing` is valid only with a
`pre_request_failure: {checkpoint: authorization_resolution}` transcript and
an exact `authentication`/`authorization` Runtime error oracle; it contains no
page because transport must remain unobserved. The runner uses the production
compiler, Semantics planner, Runtime admission, policy, pagination, decoder,
and stream paths with a test-only controlled transport; fixtures grant no
network or credential authority.

The closed fixture index schema is:

| Node | Exact v1 fields |
| --- | --- |
| index | `api_version`, `kind: fixture_index`, `package_digest`, ordered non-empty `cases` |
| case | `id`, `relation`, `operation`, ordered `covers`, ordered `inputs`, optional `predicate`, `auth`, exactly one of `execution`, `proof`, or `pre_request_failure`, and `expected` |
| input | `id` and discriminated typed `value` using the input-default scalar grammar |
| predicate | `column`, exact `operator: eq`, and typed `literal: {type, value}` |
| auth | exact `anonymous`, `bearer_present`, or pre-request-only `bearer_missing` capability state |
| execution | ordered non-empty `pages` |
| proof | `base` and `restricted`, each containing ordered non-empty `pages` over one occurrence domain |
| pre-request failure | exact `checkpoint: authorization_resolution`; no request, response, or page is permitted |
| REST request | exact `protocol: rest`, `method: GET`, typed `origin`, `path`, ordered encoded `query`, and `authorization: none|bearer_capability` |
| GraphQL request | exact `protocol: graphql`, `method: POST`, typed `endpoint`, compiled `document_digest`, ordered typed `variables`, canonical `serialized_body_digest`, and `authorization: bearer_capability` |
| GraphQL variable | `name`, exact `INT_NON_NULL|STRING_NULLABLE` type, and discriminated typed `value`; only the declared page-size and cursor variables are admitted in compiled order |
| page response | integer `status`, ordered non-sensitive `headers`, relative `body_file`, `sha256.` `body_digest`, and proof-only ordered `occurrence_ids` aligned with decoded rows |
| expected success | exact `residual_owner: duckdb`; exact `remote_accuracy: exact|superset|unsupported`; ordered typed `rows`; and closed safe `explain` fact map |
| expected failure | exact compiler `diagnostic_code`, or exact Runtime `runtime_error: {stage, field}`; a pre-request missing bearer requires `stage: authentication`, `field: authorization` |

The GraphQL body oracle is the SHA-256 of the exact UTF-8 serialization
`{"query":DOCUMENT,"variables":{PAGE_NAME:PAGE_VALUE,CURSOR_NAME:CURSOR_VALUE}}`
with object members in that order, no whitespace, the RFC renderer's exact
document bytes, canonical decimal page size, and canonical JSON string or
`null` cursor. JSON strings escape quotation mark, reverse solidus, and the
named JSON controls; other C0 bytes use lower-case `\u00hh`, solidus and
non-ASCII UTF-8 remain unescaped, and no other escape form is emitted. Wrong,
missing, stale, or repeated cursor variables and body digests fail. Expected
nullable cells use `{kind: null}`; ordinary YAML `null` is never an expected
value.

The fixture dialect accepts no arbitrary request URL, source snippet, raw
authorization value, live transport, or unspecified expected field. Body files
must be regular no-follow files under `fixtures/`, every body digest must match,
and every non-index file in that directory must be referenced exactly once.
The [`evidence/0013/github/fixtures/index.yaml`](evidence/0013/github/fixtures/index.yaml)
cases resolve the three paired native superset occurrence references and one
two-page GraphQL request/body/cursor transcript. They prove this RFC's decision
oracles, not complete package acceptance. The independently derived complete
REST, GraphQL, auth, input, pagination, conversion, error, resource, and
cancellation coverage set remains mandatory follow-on delivery evidence.

Repository acceptance requires the four-relation GitHub fixture and a second
controlled package covering typed defaults, explicit NULL, multiple operation
selection, exact/superset counterexamples, and ambiguity. Author fixtures are
not part of active generation identity and are never read by ordinary bind or
execution.

#### Delivery clarification: executing closed coverage variants

The accepted coverage mapping names source-identity, diagnostic, cancellation,
planning-failure, resource, reload, publication, and Runtime failure variants
that are intentionally not author-scriptable in `fixture-index-v1`. A `covers`
key therefore has two roles: it identifies the compiled scope and selects one
project-owned closed variant executor. It does not add an implicit YAML field
or grant an author an arbitrary mutation, failure, cancellation, or lifecycle
hook.

The case transcript and `expected` value describe the case's base execution.
For every claimed key, the project fixture provider must additionally execute
the named variant through the applicable production compiler, planner,
admission, policy, pagination, decoder, stream, compatibility, or publication
path and report that exact key only after its mapping-defined outcome occurs.
The runner accepts the case only when the claimed and actually executed key
sets are identical. Variant names ending in `_rejected`, `_missing`,
`_failure`, `_one_over_rejected`, or a stable diagnostic code require the
corresponding closed failure with no later authority or side effect; boundary,
success, selection, identity, transition, termination, cancellation, and
lifecycle variants require their exact positive or terminal invariant.

Derivation retains each key together with its structured rule, scope, variant,
and already validated bound identifiers. Dispatch uses that typed entry; no
consumer splits or interprets the underscore-delimited display key, whose
identifier boundaries are intentionally not recoverable from text.

Project executors may derive only bounded deterministic values from the
validated generation and already identity-checked transcript. Source and
diagnostic variants operate on private no-follow copies of the retained source;
planning variants use typed requests built from compiled inputs and selectors;
Runtime variants use the immutable plan and verified controlled pages;
cancellation variants use a deterministic checkpoint control; reload and
publication variants use isolated generation coordinators. These executors
cannot read an unindexed payload, contact a live service, expose a credential,
change the author schema, or alter the active generation. A variant without a
project executor remains uncovered even if its key appears in `covers`.

This post-acceptance clarification records the execution meaning already
required by the frozen mapping and the rule that every claimed variant run
through the production path. It does not change the accepted package or fixture
syntax, coverage set, key grammar, compatibility behavior, or product policy.

### Shared interfaces

Connector Experience provides one immutable generation service. The exact C++
layout remains private, but the semantic handoff contains:

```text
CompiledPackageGeneration
├── identity
│   ├── spec_identifier
│   ├── connector_id
│   ├── package_version
│   └── package_digest
├── safe source map
├── ordered relations
│   ├── ordered structural output schema
│   ├── ordered structural input schema and typed defaults
│   ├── anonymous | required logical-secret Query shape
│   ├── immutable operation, predicate, auth, policy, and resource facts
│   └── stable safe explanation
└── compatibility classification
```

Query consumes only identity, relation IDs, ordered structural types and
defaults, safe source references, auth shape, and an opaque immutable
generation handle. It derives `<connector_id>_<relation_id>`, synthesizes the
`secret` argument, binds typed values, and owns DuckDB catalog behavior. It
does not parse source, type grammar, protocol alternatives, selectors, or
snapshots.

Relational Semantics consumes the immutable compiled facts plus a typed
`ScanRequest`. It exclusively resolves inputs and defaults, selects an
operation, classifies predicates, computes projection closure and residual
ownership, intersects budgets, and produces an immutable `ScanPlan`.

Remote Runtime consumes only the `ScanPlan`, execution control, and an opaque
authorization capability. A generalized closed execution-profile identity is
derived from validated compiled facts and digest, not connector ID, relation
name, package version, explanation, or source text. Runtime never parses
packages or reinterprets relational claims.

The active-generation coordinator owns canonical roots, compilation outside
the publication lock, serialized atomic publication, generation retention,
reload, close, and shutdown. RFC 0012's provider/consumer split remains: Query
owns DuckDB registration; neither Connector nor Semantics performs catalog
mutation.

### Operational behavior

Load and reload perform local file I/O and compilation only at management-call
execution. They perform no network I/O and resolve no credential. All semantic
sources are read into an immutable candidate before publication; later source
changes cannot mutate it.

Source snapshot, YAML parsing, schema/reference validation, compilation,
generation validation, compatibility classification, and publication waiting
all receive the management call's cancellation control. They check it before
and after every bounded file, document, relation, fixture index, and generated
GraphQL profile, and once immediately before acquiring the publication lock.
After the lock is acquired, the coordinator rechecks cancellation and either
returns a cancellation failure with no mutation or performs the bounded atomic
commit as the call's success point; cancellation cannot publish a candidate
after reporting failure.

Runtime validates the complete planned capability before any request or secret
materialization. One page is one uncommitted replay unit, but v1 performs one
attempt. Pagination is sequential and backpressured through the existing
bounded stream. Cancellation interrupts transport, decode, fixture execution,
and publication wait without exposing a partial generation.

Byte-identical active source is a successful reload no-op. A changed candidate
is either published atomically or rejected with the prior generation and every
in-flight owner unchanged. Prepared, transactional, introspection, and shutdown
behavior remains exactly as accepted in RFC 0012.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Connector Experience | Sponsor and accountable author stream | Own schema, parser, validator, compiler, source map, digest, explanation, fixtures, and package compatibility | Collaboration, then X-as-a-Service provider | Consumers use one immutable generation service without source or Connector-private construction knowledge |
| Query Experience | DuckDB-facing consumer | Consume structural SQL descriptors and opaque generations; preserve RFC 0012 registration and lifecycle behavior | Collaboration, then X-as-a-Service consumer | Query parses no package/type/protocol syntax and the four generated functions match dispatcher structure and behavior |
| Relational Semantics | Semantic authority | Generalize closed facts without transferring input, operation, predicate, residual, or budget meaning | Collaboration, then X-as-a-Service provider | Complete law and counterexample suites operate on provider fixtures without package syntax knowledge |
| Remote Runtime | Execution platform | Generalize closed plan admission while retaining plan-only authority, bearer isolation, bounds, and lifecycle | Collaboration, then X-as-a-Service provider | Package plans execute through documented services with no name/version/source admission shortcuts |
| Engineering Enablement | Evidence facilitator | Supply reusable source/digest/schema/migration gate patterns and transfer domain checks | Facilitation | Each domain team maintains its entries and normal gates without Enablement approval |

No team accountability or directory ownership moves. Cognitive load is reduced
by making the stable syntax a Connector-owned service, the SQL projection a
Query-owned service, relational interpretation a Semantics-owned service, and
execution authority a Runtime-owned service. The coordinator composes those
interfaces without becoming a new catch-all package subsystem.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** Only one positive
  equality input is representable. Superset mappings retain residuals; absent
  or unsupported proof structure emits nothing. Projection/order/limit remain
  local, eliminating unproven remote capability paths.
- **Authentication, credentials, network policy, and privacy:** Only anonymous
  and exact-destination bearer behavior are authorable. HTTPS and deny-only
  address policy are mandatory. Secret resolution remains execution-only and
  destination checked.
- **Resource budgets, backpressure, and cancellation:** Every relation and
  pagination mode has positive page/scan bounds intersected with host ceilings.
  Existing bounded streams and cancellation remain the only delivery path.
- **Replay units, retries, caching, and duplicate prevention:** GET and
  generated queries are declared safe but receive one attempt. Sequential
  pagination
  preserves received order and duplicate occurrences. No retry or cache syntax
  exists.
- **Concurrency, immutability, and state ownership:** Compilation produces a
  complete immutable candidate. Serialized publication swaps one generation;
  in-flight owners pin their prior generation.
- **FFI, initialization, reload, shutdown, and failure containment:** No public
  ABI is created. Package/compiler/coordinator exceptions are contained at
  DuckDB callbacks. Failed compilation/publication changes no catalog or
  registry state; close drains and releases all generations.
- **Diagnostics, redaction, metrics, and progress:** Stable compiler categories
  and safe source coordinates are public. Runtime retains its stage/field/safe
  message surface. Metrics and progress remain excluded.

## Compatibility and migration

`duckdb_api/v1` is exact and closed. Unknown fields and values fail so a newer
package cannot be silently misread by an older implementation. Additive stable
syntax requires a new spec identifier unless it was explicitly reserved with
complete semantics in this RFC; no such extension fields are reserved.

Package versions are immutable within one DatabaseInstance's observed package
registry: the same connector ID and package version with a different digest is
rejected. A changed digest must carry a greater package version, and reload
does not downgrade. The v1 registry is intentionally non-persistent, so a
fresh DatabaseInstance cannot enforce history it has never observed; release
immutability across instances is author/distribution policy outside this local
loader. Authors classify releases as follows:

- **PATCH:** normalized compiled behavior is identical; only source provenance,
  formatting, package metadata version, documentation, or fixture evidence
  changes.
- **MINOR:** appends a relation while preserving every existing call and
  relation contract.
- **MAJOR:** every normalized change not explicitly admitted as MINOR below,
  including any change to an existing relation's SQL, row-domain, operation,
  predicate, authentication, network, resource, or failure contract.

The compiler emits an ordered normalized compatibility descriptor containing
every accepted manifest and relation fact except package version, digest,
source coordinates, README, and fixtures. The reload classifier is exhaustive:

| Candidate difference from active descriptor | Required package SemVer | Reload result |
| --- | --- | --- |
| Exact same spec, connector ID, package version, and digest | Same version | Successful no-op with `changed = false`; no compilation publication or catalog mutation |
| No normalized difference, but a different digest/version due only to comments, key formatting, source coordinates, or package version | Greater PATCH or MINOR within the active major | Compatible; publish with `changed = true` |
| Append a relation while every prior relation matches its normalized descriptor and the new relation uses only existing network origins; declarations reachable only from the new relation may be added | Greater MINOR | Compatible, subject to RFC 0012 collision/publication checks |
| Change spec identifier or connector ID | Any | Not a reload of this connector; reject |
| For a different generation, reuse a package version with another digest, use any numerically non-greater version, or downgrade | Any | Reject immutable identity |
| Remove, rename, reorder, or insert before an existing relation | MAJOR | Incompatible; fresh DatabaseInstance required |
| Add, remove, rename, reorder, or change any existing column, including type, nullability, extractor, or position | MAJOR | Incompatible |
| Add, remove, rename, reorder, or change any existing input, including type, nullability, default presence/value, or request binding | MAJOR | Incompatible |
| Add, remove, reorder, rename, or change an existing operation, selector, fallback, cardinality, protocol, origin, path, header, query field/literal/encoding, response source, replay fact, pagination fact, GraphQL fact, or generated query | MAJOR | Incompatible |
| Add, remove, reorder, rename, or change a predicate mapping, conditional input, accuracy, occurrence fixture reference, operation applicability, or request encoding | MAJOR | Incompatible |
| Add, remove, or change an existing relation's auth requirement, credential, secret field, placement, destination, or any network-policy origin/address/redirect fact | MAJOR | Incompatible |
| Add, remove, widen, or narrow any existing manifest/relation/GraphQL resource ceiling | MAJOR | Incompatible |
| Change README, fixture index, fixture payload, or their evidence digest without changing semantic source | None required | Outside active generation and reload classification |
| Use a greater MAJOR for a reload-compatible or descriptor-identical change | MAJOR | Reject reload; a next-major package requires a fresh DatabaseInstance |
| Any normalized difference not matched above | MAJOR | Fail closed as incompatible |

Appending means after all existing ordered entries; this keeps introspection
and compiled ordering deterministic. A credential added solely for an appended
relation is MINOR only when its destinations are a subset of existing network
origins and no existing relation references it. A new network origin is always
MAJOR. Column addition is MAJOR because it changes `SELECT *`, result schemas,
and prepared metadata.

The compiler/coordinator independently classify structural compatibility; an
author's version cannot waive an incompatible diff. Reload accepts a greater
same-major version only when the classifier finds no incompatible transition.
An incompatible or next-major package requires a fresh DatabaseInstance in v1
because unload, historical selection, and destructive replacement are absent.
The prior generation remains active after rejection.

Project SemVer governs the extension's public SQL and implementation behavior,
not package lineage. Pre-1.0 project releases may fix implementation defects,
but after this RFC is accepted they do not intentionally reinterpret a valid
`duckdb_api/v1` package. The `1.x` line treats the accepted spec and SQL
inventory as stable subject to the release/support policy.

`duckdb_api/draft` has no compatibility guarantee, auto-upgrade, or coexistence
mode. Its only migration fixture proves explicit rejection. Migration fixtures
start with bounded `duckdb_api/v1` packages loaded by `0.8.0`, then prove
compatible reload and the `0.9.0` public-author workflow without changing the
grammar. The native dispatcher follows RFC 0012's separate `0.8.0` deprecation
and `0.9.0` removal path.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Can one declarative subset represent the complete native GitHub product? | Field-by-field trace for identity, four schemas, requests, response sources, auth, predicate, pagination, GraphQL, and resources | [`evidence/0013/github/`](evidence/0013/github/), `src/connector/native_github_composition.cpp`, Connector catalog snapshots, the RFC trace below, and `ruby scripts/verify-rfc-0013-evidence.rb` | The content-addressed decision verifier accepts the closed evidence set, schema-valid source and index, semantic digest `sha256.b286e6f7481b437b243dfe2ce017a59d601d909272b9d2b35788fb78753ff23b`, payloads, GraphQL vectors, coverage golden, and numeric envelopes; the package compiler does not yet exist |
| Can Query publish typed package relations without parsing source? | Structural relation/input descriptor and generation-bound registration | RFC 0012 native coordinator trial plus Query charter/source audit | Catalog transaction and snapshots established; typed package input handoff remains delivery evidence |
| Can Semantics preserve results while generalizing author facts? | Operation-selection, exact/superset, three-valued, duplicate, residual, pagination, and local-operator laws | Existing Connector/Semantics fixture suites and RFC 0010 oracles | Closed and controlled profiles established; arbitrary valid v1 instances remain delivery evidence |
| Can Runtime execute both protocols through one bounded service? | REST and GraphQL end-to-end product tests with auth, cancellation, resources, pagination, and failures | `make test` native product suite and accepted RFCs 0005-0011 | Established for four closed profiles; name-independent admission remains delivery evidence |
| Is package identity path-independent and fail-closed? | Framed SHA-256 golden vectors, copied-root equality, path/symlink/duplicate/case counterexamples | Connector-owned digest and source-admission fixtures | Pending implementation evidence; algorithm and oracle are fully specified here |
| Is the GraphQL surface genuinely authorable rather than a renamed fixed profile? | At least two distinct valid structured queries plus mutation/subscription unrepresentability and literal, variable, path, digest, credential, and resource counterexamples | Repository GitHub package and independent controlled package through one generator/parser backstop | Pending implementation evidence; required before product delivery, not RFC acceptance |

### Four-relation contract trace

| Native relation | Stable v1 declarations required | Existing deterministic evidence | New delivery evidence |
| --- | --- | --- | --- |
| `duckdb_login_search_page` | Anonymous REST GET; fixed query; terminal collection path; three non-null scalar columns; no pagination; 64 KiB/3-row bounds | `connector_contract_tests.cpp`, REST Query/Runtime product tests | Package-source, digest, diagnostic, compiled-generation, and generated-function differential |
| `authenticated_user` | Bearer REST GET; exact destination; root object; exactly one on success; three non-null columns; no pagination | Connector auth catalog, secret lifecycle, request-admission, and decoder tests | Package credential/source mapping and generated-function differential |
| `authenticated_repositories` | Bearer REST GET; six columns; one superset `visibility = 'private'` input; sequential Link pagination; page/scan bounds | Predicate proof/composition suites, Link parser/traversal, paginated product tests | Author predicate/source fixture, generic plan admission, and package/native differential |
| `viewer_repository_metrics` | Bearer structured query; compiler-generated document digest; eight columns with nullable language; fail-on-error; sequential cursor; body/page/scan bounds | GraphQL Connector, Semantics, Runtime, and actual-DuckDB product suites | General structured generator/parser backstop plus second-query counterexamples and package/native differential |

No decision-critical feasibility result is pending. The existing product proves
execution and team boundaries; this RFC deliberately makes generalized
compiler/admission evidence part of the complete follow-on product, not a
reason to accept placeholders.

## Alternatives considered

### Relabel the full `duckdb_api/draft` document as v1

This would preserve current examples and maximize apparent author capability.
It would also stabilize unimplemented providers, partitions, retry/cache,
dynamic schema, broad auth, transforms, importers, generated GraphQL,
distribution, and `sql_name`. Every team would inherit an unbounded surface,
and `0.8.0` could only ship a partial compiler. Rejected.

### Publish only the four exact GitHub execution profiles

This is the shortest path to a local YAML file and uses today's closed Runtime
admission. It does not let an independent author describe another API, makes
package version/name an accidental execution identity, and would force
`0.9.0` to introduce the real author grammar for the first time. Rejected.

### Exclude GraphQL authoring while retaining the native GraphQL relation

This reduces parser and validation work and keeps the REST package useful. It
splits the v1 protocol claim from the authoring claim and leaves the fourth
repository relation as a privileged native exception. The roadmap requires
the complete retained protocol subset before freeze; the structured bounded
query profile is selected instead.

### Accept arbitrary explicit GraphQL documents

This is flexible and resembles the broad draft's explicit mode. It imports a
large external grammar and makes source validation, variable authority,
selection completeness, and relational-domain review depend on opaque author
bytes. The structured full-selection profile can represent the proven product
and independent Relay APIs while making mutation, subscription, credentials,
and projection-sensitive documents unrepresentable.

### Retain projection, ordering, and remote-limit declarations

The broad draft already describes them. The permanent catalog and planner
advertise no such capability, and the four-relation product proves correct
DuckDB-local ownership. Retaining fields with ignored or always-disabled
behavior would violate the complete-subset rule. The proposal omits them; a
later spec family can add them with semantic laws and product evidence.

### Use an alpha or beta spec identifier until project 1.0

This preserves freedom to change grammar but creates planned package migration
work and lets `0.8.0` avoid the stable successor required by RFC 0009. The
selected small closed contract uses `duckdb_api/v1` immediately and makes any
incompatible change explicit.

### Digest normalized YAML or the complete directory

Normalized YAML hides source changes behind parser behavior and makes aliases,
duplicate keys, comments, and ordering difficult to reason about. Whole-root
digests make README, fixture, and editor changes replace executable
generations. Exact framed semantic-source bytes provide simpler custody and
path-independent identity.

## Drawbacks and failure modes

- Three scalar types, fixed REST paths, one query-input encoding, and a narrow
  GraphQL profile exclude many APIs. Connector Experience owns clear
  unsupported diagnostics and must not imply the broad draft is accepted.
- The GraphQL generator/parser backstop is new permanent product work.
  Connector owns source validation; Runtime still revalidates only compiled
  plan authority.
- Exact source-byte digest means harmless comment or ordering changes publish
  a new generation. This is intentional provenance, but authors must bump the
  immutable package version for such a source change.
- Author predicate claims cannot be universally verified against a mutable
  upstream API. Explicit local loading, mandatory counterexamples, explanation,
  and conservative runtime fallback make responsibility visible but do not
  certify a third-party author.
- V1 reload refuses major/incompatible transitions and has no unload. Operators
  must open a fresh DatabaseInstance for such a package change.
- External relation files make tiny packages more verbose. They preserve
  deterministic order, source identity, and independently reviewable
  responsibilities.

## Acceptance and verification

- **End-to-end demonstration:** Validate and fixture-test the repository GitHub
  package, compile it to the same safe explanation as the native catalog, load
  all four generated RFC 0012 functions, and run anonymous REST, bearer REST,
  superset predicate, Link pagination, and GraphQL cursor queries. Copying the
  package preserves its digest. Draft syntax, a widened policy, reserved input,
  invalid generated-query structure, unsupported field, and incompatible reload each fail
  before publication with the prior generation usable.
- **Automated oracle:** Closed YAML schema and mutation corpus; digest golden
  vectors; source-path/symlink/duplicate-key cases; all retained type/input/
  selector/protocol/auth/predicate/pagination/resource forms; native/package
  catalog and dispatcher/generated request-plan-result-error differentials;
  paired occurrence proof laws; encoded Link targets; GraphQL response-role
  aliases and wrong, missing, repeated, or stale cursor/body transitions;
  independently derived fixture coverage; offline fixture parity; reload
  compatibility; and unsupported draft-field rejection.
- **Quality gates:** Focused Connector, Query, Semantics, and Runtime targets;
  `make build`, `make test`, `make demo`; source and dependency identity gates;
  a fresh native product cell; `ruby scripts/verify-rfc-0013-evidence.rb` for
  the bounded RFC decision artifacts; agent-asset validation; and staged/
  unstaged whitespace checks.
- **Independent review:** Fresh Connector author-contract, Query type/catalog,
  Semantics relational-law, Runtime security/lifecycle, Enablement digest/gate,
  and adversarial compatibility/security/lifecycle reviewers.
- **Interaction exit:** Final source/target audit proves each consumer uses its
  chartered immutable interface, all collaboration exits above have executable
  evidence, and no package syntax or provider internals leak across boundaries.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Replace broad v1 implications with the accepted package, compiler, handoff, and local-operator boundary | Pending RFC acceptance and propagation |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Affected | Make `duckdb_api/v1` normative, move every excluded draft field to post-v1 design, and include exact schema/diagnostics/fixtures | Pending RFC acceptance and propagation |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Define package identity, compiled generation, structural types/inputs, generalized protocol facts, source map, fixture path, and name-independent plan admission | Pending RFC acceptance and propagation |
| RFC 0012 and public inventory | Affected, no decision change | Resolve the spec-version/package-shape placeholders and add exact connector-owned metadata shapes | Pending RFC acceptance and propagation |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | Existing accountability and interfaces govern implementation; record actual interaction exits | Pending delivery audit |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing goal, RFC, contract-change, topology, review, and delivery practices apply | Current agent-asset validation |
| Repository packages, examples, diagnostics, fixtures, tests, and release evidence | Affected | Add complete GitHub and controlled v1 packages, migration/rejection corpus, and product differentials | Pending follow-on product delivery |

The RFC records rationale. Contract propagation begins only after acceptance;
product implementation remains a separately activated follow-on goal.

## Unresolved questions

- The public invocation spelling for standalone validate, compile-explain, and
  fixture-test author workflows is outside the connector source contract. It
  must reuse the permanent services implemented for `0.8.0` and be decided
  before the `0.9.0` public-author surface freezes; package loading itself uses
  RFC 0012's accepted SQL.
- The project-wide release and support policy still decides how long a stable
  project line supports `duckdb_api/v1`; this RFC decides package semantics,
  not support windows.

Neither question changes package meaning or the complete `0.8.0` compiler and
load obligation.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| `connector_package_v1_connector_review` | Connector Experience | Approved | Final re-review at base `f579a1128d589a58f6ed10d898bda8c837f8e660` confirms the content-addressed semantic and fixture schemas close the author grammar, GraphQL rendering and diagnostics are deterministic, coverage keys derive independently, and `bearer_missing` is representable only as an exact no-transport authorization-resolution failure | Initial grammar, renderer, diagnostic, coverage, and missing-bearer objections resolved; permanent compiler, fixture-runner, and immutable-generation evidence remains the delivery exit |
| `connector_package_v1_query_review` | Query Experience | Approved | Fresh review of base `f579a1128d589a58f6ed10d898bda8c837f8e660` confirmed exact identifier/type/default/NULL/auth shapes, operation-local predicate input, stable diagnostics, DuckDB residual ownership, and a structural immutable Query handoff; public-inventory verification and all 18 mutation tests passed | Initial public-input, default, identifier, and diagnostic objections resolved; delivery must expose a bounded Query descriptor/view and opaque generation handle while RFC 0012 retains SQL/lifecycle authority |
| `connector_package_v1_runtime_review` | Remote Runtime | Approved | Final re-review confirmed canonical encoded Link preservation, response-role disjointness, two-page GraphQL body/cursor oracles, exact compiler/fixture ceilings, bounded cancelable directory enumeration, ordinary-load fixture isolation, and a 256-record diagnostic cap | Initial transport, oracle, and bounded-work objections resolved; boundary, one-over, cancellation, plan-admission, credential, and lifecycle oracles remain delivery evidence |
| `connector_package_v1_semantics_review` | Relational Semantics | Approved | Final re-review confirmed explicit input states and candidate-local selection, paired occurrence-labelled implication/multiplicity proofs, retained DuckDB ownership for every predicate, and GraphQL `duckdb`/`unsupported` fixture agreement with the accepted native plan | Initial residual, NULL, selector, proof-domain, and GraphQL-vocabulary objections resolved; native/package plan differential remains the interaction exit |
| `connector_package_v1_enablement_review` | Engineering Enablement | Approved | Independent re-review recomputed package, payload, generated-document, serialized-body, schema, and coverage identities and confirmed exact reload precedence, canonical package SemVer, independent coverage derivation, failsafe YAML rules, deterministic diagnostics, and one coherent source snapshot | Initial reload, compatibility, YAML, diagnostic, fixture-coverage, and snapshot objections resolved; facilitation exits when domain teams maintain their permanent gates |

### Closed-variant delivery clarification review

The post-acceptance clarification above was independently reviewed by every
affected team on 2026-07-20. It closes how the already accepted coverage map is
executed; it does not amend author syntax, public behavior, compatibility, or
the frozen coverage set.

| Team perspective | Result | Evidence | Delivery exit retained |
| --- | --- | --- | --- |
| Connector Experience | Approved | Typed entries preserve scope, variant, and bound identifiers without parsing display keys; required coverage is derived before fixture I/O; no-follow custody, exact payload identity, and exact required/claimed/executed equality prevent author claims from granting evidence | Complete the bounded runner, source and diagnostic variants, product-owned schema and coverage assets, full GitHub coverage, and the controlled second package |
| Query Experience | Approved | Query still receives only structural registration metadata, typed requests, and an opaque generation handle; RFC 0012 remains authoritative for catalog transactions, publication leases, MVCC, prepared plans, and collision behavior | Exercise publication conflict and publication-wait cancellation through the actual isolated catalog coordinator; Query must not link a fixture parser or parse coverage keys |
| Remote Runtime | Approved | Runtime receives only an immutable plan, closed authorization state, identity-verified controlled pages, and call-scoped control; it never receives package source, fixture schema, expected values, coverage keys, or credential bytes | Exercise every transport, decode, pagination, resource, failure, cancellation, and close variant through the controlled production executor |
| Relational Semantics | Approved | Typed inputs and selectors reach the production generation-bound planner; DuckDB retains residual ownership, and occurrence-labelled predicate domains remain the exactness and multiplicity oracle | Exercise every selection failure through the planner, prove Runtime was not entered on planning failure, and complete the exact predicate-law matrix |
| Engineering Enablement | Approved | Content-addressed schema and coverage assets, typed dispatch, isolated integration composition, and permanent ordinary/fresh targets make missing, echoed, or drifted evidence fail closed | Byte-lock the product assets, include all fixture sources and integration targets in the product graph, pin their identities, and pass both developer and fresh native gates |

No reviewer requested an RFC amendment. Approval of the clarification does not
close the implementation interactions recorded in the follow-on goal.

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Technical decision:** Accepted by the lead agent on 2026-07-20 after every
  required reviewer approved the final contract and all material objections
  were resolved.
- **Product approval:** Approved by the product manager on 2026-07-20 for the
  exact `duckdb_api/v1` identity, closed declaration subset, structured GraphQL
  authoring profile, package compatibility rules, deliberate omission of
  remote projection, ordering, and limit syntax, and explicit local-package
  trust boundary for author predicate claims.
- **Rationale:** All required reviewers approve the current bounded contract.
  The four-relation package, strict semantic and fixture schemas, coverage
  derivation, source identity, GraphQL document/body identities, occurrence
  proofs, diagnostics, and compatibility classifier make the proposed subset
  complete without stabilizing the broader draft.
- **Material objections:** Connector, Query, Runtime, Semantics, and Enablement
  raised evidence-backed objections during review. Every objection was fixed
  and re-reviewed; none was rejected or waived.
- **Superseded by:** Not applicable.

Acceptance is not implementation completion.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Author and query a complete local connector package | Connector Experience | Query Experience and Remote Runtime — Collaboration then X-as-a-Service; Relational Semantics — X-as-a-Service; Engineering Enablement — Facilitation | RFC 0013 Accepted; RFC 0012 Accepted; canonical public-inventory gate complete; product goal activated |
| Freeze and prove the public connector-author workflow | Connector Experience | Query Experience — Collaboration; Engineering Enablement — Facilitation; Runtime and Semantics — X-as-a-Service | Complete package services implemented in `0.8.0`; author-tool invocation decision accepted; `0.9.0` goal activated |
