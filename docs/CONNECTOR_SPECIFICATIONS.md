# Connector package specification

This document is the author-facing contract for local declarative connector
packages. The only accepted specification identifier is
`duckdb_api/v1`. [RFC 0013](rfcs/0013-define-connector-package-v1-contract.md)
accepted this compatibility family; the schemas below close every source
shape.

The specification describes immutable metadata. It does not grant network or
credential authority by itself, and it does not contain DuckDB catalog state,
scan plans, residual filters, execution state, or secret values.

## Package layout and identity

A package root is an explicit absolute local directory:

```text
connector.yaml
relations/<relation_id>.yaml
fixtures/index.yaml
fixtures/...
README.md
```

`connector.yaml` lists relation identifiers in order. Each identifier resolves
only to `relations/<relation_id>.yaml`; inline relations and arbitrary relation
paths are not valid. Ordinary load reads only the manifest and listed relation
files. Fixture tooling separately opens `fixtures/`. `README.md` is human
documentation and is not semantic source.

Four identities remain distinct:

| Identity | Value |
| --- | --- |
| Specification | Exact `duckdb_api/v1` in every machine-readable source file |
| Project | The extension release version |
| Package | Canonical author SemVer `MAJOR.MINOR.PATCH` in `connector.yaml` |
| Generation | Specification, connector ID, package version, and package digest |

Package version components are unsigned 32-bit canonical decimals. Signs,
leading zeroes, omitted components, prerelease labels, and build metadata are
invalid.

The package digest is
`sha256-length-prefixed-path-and-bytes-v1` over `connector.yaml` and exactly the
listed relation files. Paths are normalized UTF-8 POSIX relative paths and
sorted bytewise. For each file, the digest receives:

```text
u64be(path length) || path bytes || u64be(content length) || raw content
```

The public spelling is `sha256.<64 lowercase hexadecimal digits>`. Absolute
root, file metadata, fixtures, README, and unreferenced files do not enter the
digest. Any semantic-source byte change does.

## Source custody and YAML

The loader does not expand tildes, environment variables, URLs, or search
paths. It opens every absolute root component and admitted child without
following links. Symbolic links, hard-linked leaves, special files, `.` or
`..`, backslashes, absolute child paths, duplicate normalized paths,
case-colliding paths, unlisted relation YAML, and files that change during the
read are rejected.

Compilation operates on one bounded immutable byte snapshot. The loader
captures and compares root and `relations` entry sets plus file device, inode,
type, link count, size, modification time, and change time before and after
the read. It never compiles a mixture of filesystem states.

Each YAML source is one UTF-8 YAML 1.2 document under the failsafe schema. All
scalars initially decode as text; field-role validation interprets Booleans,
integers, identifiers, enums, and typed values afterward.

Accepted syntax:

- block mappings and sequences;
- flow mappings and sequences;
- plain scalars; and
- double-quoted scalars with JSON string escapes.

Rejected syntax includes UTF-8 BOM, CR line endings, indentation tabs, explicit
tags, single-quoted or block scalars, anchors, aliases, merge keys, duplicate
mapping keys, and multiple documents. The plain token `null` is text and is
valid only as the documented null-value discriminator. `~` has no valid role.
Boolean fields accept only plain `true` or `false`; integer fields accept only
their documented canonical decimal grammar.

The exact decoded source schemas are:

- [`connector-package-v1.schema.json`](rfcs/evidence/0013/connector-package-v1.schema.json)
- [`fixture-index-v1.schema.json`](rfcs/evidence/0013/fixture-index-v1.schema.json)

Unknown fields fail. Schema validation is followed by identifier, reference,
type, uniqueness, selector, policy, resource, predicate, and protocol laws.

## Manifest

The manifest has this closed top-level shape:

```yaml
api_version: duckdb_api/v1
kind: connector
id: github
version: 1.0.0
extractor_dialect: duckdb_api/json_path_v1

credentials: []
network_policy: {}
relations: []
```

Connector, relation, column, input, operation, predicate, and credential IDs
use lower-case snake case, begin with a lower-case ASCII letter, and contain at
most 63 bytes. IDs are case-sensitive source identities. Generated DuckDB
names follow DuckDB's case-insensitive catalog comparison.

`extractor_dialect` is exactly `duckdb_api/json_path_v1`. The accepted JSON
path subset is structural and deterministic: root `$`, dot-name object fields,
and the terminal collection marker `[*]` where a response declaration permits
it. Recursive descent, filters, scripts, unions, slices, wildcards in column
extractors, and implementation-specific coercions are invalid.

### Credentials

V1 admits bearer credentials only:

```yaml
credentials:
  - id: github_token
    kind: bearer
    secret_field: token
    placement: authorization_header
    destinations:
      - scheme: https
        host: api.github.com
        port: 443
```

`secret_field` is a logical field name, not a DuckDB secret name or a secret
value. The runtime materializes an approved bearer capability only during
execution and only for an exact declared destination.

### Network policy

Every manifest declares a deny-only network policy:

```yaml
network_policy:
  origins:
    - scheme: https
      host: api.github.com
      port: 443
  redirects: deny
  private_addresses: deny
  link_local_addresses: deny
  loopback_addresses: deny
  max_response_bytes: 8388608
```

Only HTTPS origins are accepted. Connector policy may narrow host policy but
never widen it. Redirects and private, link-local, and loopback addresses are
always denied in v1. Hosts are exact lower-case DNS names or admitted IP
literals; URL userinfo, wildcard hosts, path prefixes, and ambient proxy,
cookie, netrc, or credential policy are not authorable.

## Relations and structural types

Each `relations/<id>.yaml` begins:

```yaml
api_version: duckdb_api/v1
kind: relation
id: authenticated_repositories
schema: static
```

`schema` is exactly `static`. V1 types are `BOOLEAN`, `BIGINT`, and `VARCHAR`.
Columns retain declaration order:

```yaml
columns:
  - id: private
    type: BOOLEAN
    nullable: false
    extract: $.private
```

Conversion is strict. Missing or JSON-null data fails for a non-nullable
column. Numeric conversion must be integral and lossless for `BIGINT`; strings
and Booleans are never coerced across types.

### Relation inputs and defaults

Relation inputs are ordered named DuckDB arguments:

```yaml
inputs:
  - id: owner
    type: VARCHAR
    nullable: false
  - id: include_archived
    type: BOOLEAN
    nullable: true
    default:
      kind: boolean
      value: false
```

Defaults use a discriminated object. The admitted `kind` values correspond to
the declared type plus `null` for a nullable typed NULL default. Boolean
defaults use plain `true` or `false`; `BIGINT` defaults use canonical signed
64-bit decimal; `VARCHAR` defaults use a double-quoted scalar. A default's type
must exactly match its input. A non-nullable input cannot default to NULL.

`secret` is reserved and cannot be a relation input. Query synthesizes a
separate required `secret VARCHAR` argument only for an authenticated
relation.

All relation inputs are omittable at DuckDB bind. Semantics distinguishes:

- `UNBOUND`: no explicit argument;
- `BOUND_NULL`: explicit SQL NULL; and
- `BOUND_VALUE`: explicit typed value.

A default applies only to `UNBOUND`. Explicit NULL suppresses a default and is
invalid for a non-nullable input. Only `BOUND_VALUE` satisfies a required-input
operation selector.

### Authentication

A relation is anonymous:

```yaml
auth:
  mode: anonymous
```

or references one manifest bearer declaration:

```yaml
auth:
  mode: credential
  credential: github_token
```

The operation origin must be within both the credential destination set and
the connector network policy. Packages never name a DuckDB secret or carry a
credential value.

### Resource envelope

Every relation declares positive ceilings:

```yaml
resources:
  max_response_bytes_per_page: 8388608
  max_response_bytes_per_scan: 67108864
  max_records_per_page: 100
  max_records_per_scan: 3200
  max_extracted_string_bytes: 512
```

All resource values are canonical unsigned 64-bit decimals in
`1..18446744073709551615`. The compiler uses checked arithmetic and rejects
contradictory or overflowing page/scan envelopes. Runtime intersects these
values with host ceilings and debits actual use.

## Operation selection

A relation declares one or more base operations. V1 selectors contain only:

- `when.required_inputs`, containing tagged `input.<id>` or
  `conditional.<id>` references; and
- at most one relation-wide `fallback: true` operation.

For each non-fallback candidate, Semantics resolves relation inputs and typed
defaults, derives only that operation's predicate-owned conditional inputs,
and checks required references. Eligible operations rank by the number of
satisfied required references. Equal top ranks fail; declaration order never
breaks a tie. The fallback is considered only when no non-fallback is eligible.

Alternative input sets, forbidden-input selectors, author priority, and
declaration-order precedence are not part of `duckdb_api/v1`.

## REST operations

REST v1 admits replay-safe HTTPS `GET` with a fixed origin and path:

```yaml
operations:
  - id: list_rows
    fallback: true
    cardinality: many
    replay_safety: safe
    request:
      protocol: rest
      method: GET
      origin:
        scheme: https
        host: api.github.com
        port: 443
      path: /rows
      query: []
      headers: []
    response:
      source: root_array
    pagination:
      strategy: disabled
```

`cardinality` is `one` or `many`. `one` requires a root object response;
`many` accepts `root_array` or `terminal_collection` with a records path.

Ordered query fields contain exactly one value source:

- a fixed non-sensitive UTF-8 literal;
- a declared relation `input`; or
- one selected operation-local `conditional` predicate input.

Every query value uses `form_urlencoded`. Relation-input fields require
`omit_when_unbound: true` and `omit_when_null: true`; v1 has no emit-null
encoding. Fixed headers are non-sensitive structural values. `Authorization`,
`Proxy-Authorization`, `Cookie`, and other credential-bearing placements are
not fixed author metadata.

Pagination is either disabled or sequential mutable `Link: rel=next`. An
accepted continuation must remain on the exact operation origin and path.
Pagination is pull-driven, one page at a time, bounded by positive page and
scan ceilings, and does not grant ordering, snapshot, parallelism, resume,
deduplication, retry, or cache guarantees.

## Structured GraphQL operations

GraphQL authoring is structured; raw documents are not accepted. A query
declares:

- one query operation name;
- a root field path;
- typed fixed arguments made from Boolean, signed 64-bit integer, string,
  enum, null, list, and ordered object literals;
- one output selection per declared relation column; and
- the closed forward Relay pagination profile.

The compiler validates GraphQL names, aliases, response-path ownership,
duplicate arguments/object fields, scalar compatibility, and disjoint row,
error, and page-info paths. It generates one canonical query-only document and
parses that generated document as a backstop. Mutation, subscription,
introspection, caller-supplied documents, fragments, directives, arbitrary
variables, partial-data recovery, and reverse pagination are invalid.

Relay pagination is sequential and mutable with exact names for page size,
cursor, `pageInfo`, `hasNextPage`, and `endCursor`; `max_concurrent_pages` is
exactly 1. `partial_data` is exactly `fail_on_any_error`. Positive document,
request-body, page, record, and scan limits are mandatory.

The repository package contains complete REST and GraphQL examples under
[`docs/rfcs/evidence/0013/github`](rfcs/evidence/0013/github).

## Predicate mappings and DuckDB ownership

V1 predicate declarations map one positive typed equality to at most one
operation-local conditional request input. The declaration names the relation
column, exact typed literal, operation, target input, encoding capability,
accuracy (`exact` or `superset`), base occurrence domain, and occurrence proof.

The safety law is:

```text
DuckDB predicate D implies remote predicate R
```

Exactness additionally requires `R` to imply `D` over the declared occurrence
bag with matching multiplicity. A label is not proof. Compiler fixtures must
exercise TRUE, FALSE, NULL, duplicates, unknown restricted occurrences,
changed rows, extra rows, and lost true rows.

DuckDB retains every predicate offered to the extension, including an exact
mapped predicate. Projection, ordering, limit, and offset remain DuckDB-owned.
V1 has no author declarations for remote projection, ordering, or limit
pushdown.

## Diagnostics

Compilation produces bounded deterministic diagnostics with an exact code,
phase, safe package-relative source coordinate, and structural field. Source
content, absolute roots, credentials, secret names, generated documents,
request bodies, rows, and remote messages are never diagnostic fields.

The stable codes are:

| Code | Meaning |
| --- | --- |
| `DUCKDB_API_UNSUPPORTED_SPEC` | Missing, draft, or unknown `api_version` |
| `DUCKDB_API_UNSUPPORTED_DIALECT` | Unknown extractor dialect |
| `DUCKDB_API_MALFORMED_YAML` | Encoding, document, duplicate-key, alias/tag, or syntax failure |
| `DUCKDB_API_UNKNOWN_FIELD` | Field outside the closed schema |
| `DUCKDB_API_MISSING_FIELD` | Required field absent |
| `DUCKDB_API_DUPLICATE_ID` | Identifier duplicate or case/path collision |
| `DUCKDB_API_INVALID_REFERENCE` | Missing or wrong-kind reference |
| `DUCKDB_API_INVALID_IDENTIFIER` | Value outside the identifier grammar |
| `DUCKDB_API_INVALID_TYPE` | Unsupported type or typed-value mismatch |
| `DUCKDB_API_INVALID_EXTRACTOR` | Path outside `json_path_v1` |
| `DUCKDB_API_RESERVED_INPUT` | Declared relation input is `secret` |
| `DUCKDB_API_UNSUPPORTED_DECLARATION` | Well-formed value outside v1 |
| `DUCKDB_API_INVALID_SELECTOR` | Selector, fallback, eligibility, or tie invalid |
| `DUCKDB_API_INVALID_PREDICATE` | Mapping, proof, domain, or encoding invalid |
| `DUCKDB_API_INVALID_GRAPHQL_PROFILE` | Structured query cannot produce the closed profile |
| `DUCKDB_API_POLICY_WIDENING` | Network, credential, or resource authority widens |
| `DUCKDB_API_RESOURCE_EXHAUSTED` | Compiler or fixture budget exhausted |
| `DUCKDB_API_PACKAGE_IDENTITY` | Source snapshot, digest, or version identity fails |
| `DUCKDB_API_FIXTURE_MISMATCH` | Offline evidence differs from compiled behavior |
| `DUCKDB_API_INCOMPATIBLE_RELOAD` | Candidate transition is not compatible |
| `DUCKDB_API_PUBLICATION_CONFLICT` | Catalog or generation publication refuses atomically |

Diagnostics sort by phase, source path, line, column, field, code, and safe
detail. At most 256 records are retained: the first 255 detail candidates and
one terminal resource record.

## Offline fixtures

`fixtures/index.yaml` is explicit author evidence, not package identity.
Fixture tooling first compiles the same immutable package generation used by
load, validates the index against its closed schema, derives required coverage
independently, and only then opens indexed payload leaves.

Fixtures predict operation selection, exact request structure, response pages,
typed rows, predicate occurrence laws, error stage, and safe explanation.
They cannot grant network or credential authority and do not replace the host
policy or Runtime admission checks.

For the repository GitHub package, the independently derived contract contains
258 unique coverage keys. The checked-in RFC decision cases are examples; the
delivery gate executes the complete set.

## Reload compatibility

Reload compares normalized compiled descriptors, not source path, YAML order,
comments, or explanation prose.

- Byte-identical digest is an exact no-op.
- A greater PATCH may change provenance only when the normalized descriptor is
  identical.
- A greater MINOR may retain the descriptor or append relations while leaving
  every existing relation structurally identical and ordered.
- Reused versions with another digest, downgrade, non-greater changed source,
  next-major transition, relation removal/reordering/change, and any auth,
  origin, policy, resource, schema, input, operation, predicate, or execution
  change to an existing relation are incompatible.

Failure publishes nothing and leaves the active generation and every bound or
in-flight owner usable.

## Resource ceilings

V1 source and fixture processing is finite:

| Resource | Ceiling |
| --- | ---: |
| Semantic files | 65 |
| Root entries | 4,096 |
| `relations` entries | 1,024 |
| Aggregate root and relation entries | 5,120 |
| One semantic YAML file | 1 MiB |
| Aggregate semantic source | 16 MiB |
| Package-relative path | 255 UTF-8 bytes |
| YAML nesting | 32 levels |
| YAML nodes | 100,000 |
| One decoded scalar | 1 MiB |
| One mapping or sequence | 4,096 entries |
| Relations | 64 |
| Columns, inputs, operations, predicates per relation | 256, 128, 64, 64 |
| Credentials and origins | 64 each |
| Query fields, fixed arguments, fixed headers per operation | 64, 64, 32 |
| Generated GraphQL document | 64 KiB, further narrowed by relation policy |
| Diagnostics | 256 |
| Fixture cases and response pages per case | 1,024 and 32 |
| Fixture index and one payload | 4 MiB and 8 MiB |
| Aggregate fixture payloads and leaves | 256 MiB and 4,096 |

A host may choose a lower value. The effective limit is the minimum of the
host and specification ceilings; zero never means unlimited.

## Compatibility boundary

`duckdb_api/v1` contains exactly the behavior above and the byte-copied
schemas. It does not contain author-chosen SQL names, automatic discovery,
URLs as package roots, OpenAPI or GraphQL introspection import, raw GraphQL
documents, dynamic schemas, write operations, connection profiles,
partitions, providers, enrichment, retries, caching, rate-limit waiting,
remote projection/order/limit declarations, custom native code, WASM, or a
public C++ ABI.

Adding any such capability requires a later accepted contract. An
implementation must reject the declaration rather than accept and ignore it.
