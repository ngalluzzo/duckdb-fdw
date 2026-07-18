# Relational Semantics plan: bounded live REST scan

## Outcome and status

Produce the deterministic, offline `ScanRequest -> ScanPlan` handoff for the
accepted `github.duckdb_login_search_page` relation. The plan describes the
complete zero-to-three-row base domain of one fixed response page, not the
unbounded GitHub user or search-result universe. Its fixed `q` and `per_page`
request values define that base domain; they are not DuckDB predicate or limit
pushdown.

This workstream starts after Connector Experience publishes the immutable
native product metadata required by RFC 0005. Query Experience and Remote
Runtime consume the resulting plan without duplicating or reclassifying its
relational decisions.

## Owned files

| File | Responsibility |
| --- | --- |
| `src/include/duckdb_api/scan_plan.hpp` | Typed immutable plan meaning, ownership classifications, applied capability and resource envelope, and safe explanation |
| `src/scan_planner.cpp` | Side-effect-free validation and construction from the compiled connector plus conservative request |
| `test/cpp/scan_planner_tests.cpp` | Golden, negative, determinism, ownership, and conservative-fallback oracles |

Relational Semantics does not edit Connector metadata, Query request/adapter
code, Remote Runtime execution, build files, or shared design documents in
this workstream.

## Provider dependency

Connector Experience provides the immutable `CompiledConnector` team API. The
planner requires that snapshot to identify:

- native product metadata for connector `github`, version `0.3.0`, relation
  `duckdb_login_search_page`, and operation
  `github_search_duckdb_login_page`;
- one fallback, zero-to-many, retry-disabled REST `GET` operation;
- structural base, path, ordered query fields, fixed safe headers, response
  extractor, strict output columns, and stable snapshot identity;
- the connector-side network narrowing and response/record/string ceilings.

The planner neither constructs Connector metadata nor interprets YAML. Missing,
ambiguous, widened, or inconsistent metadata fails deterministically before a
plan is returned.

## Typed plan contract

The plan freezes one executable operation and its effective host-intersected
policy. It records:

- connector, relation, version, source snapshot, and selected operation
  identity;
- REST protocol, zero-to-many cardinality, structural request and response
  metadata, and the complete typed projection closure;
- `TRUE` as both the remote and residual predicate relative to the bounded
  single-response base domain;
- DuckDB as the sole owner of every SQL filter, ordering, limit, and offset;
- no remote or runtime ordering, limit, or offset;
- disabled authentication, redirects, pagination, providers, retry, and cache;
- the authorized HTTPS destination and one-attempt, response, decode, batch,
  wall-time, memory, and concurrency ceilings; and
- stable classification reasons suitable for a safe golden explanation.

Typed enums and value objects make cardinality, ownership, protocol, and
capability distinctions explicit. The snapshot is an explanation oracle, not
runtime authorization. Remote Runtime may reject executable facts it cannot
support, but it must not rebuild the canonical plan, compare the complete
snapshot, or recalculate relational ownership.

Planning consumes only the immutable connector, conservative request, and host
ceilings. It performs no network, filesystem, environment, or mutable-global
access. The returned value is copied unchanged into immutable bind state for
prepared and ordinary scans.

## Properties and counterexamples

Focused planner evidence proves:

- identical connector, request, and host ceilings produce an identical plan
  and safe snapshot without side effects;
- the fixed `q` and `per_page=3` request fields never appear as predicate or
  limit pushdown, `R = TRUE` is scoped to the single-response domain, and no
  remote or runtime limit exists;
- the full three-column projection closure is preserved when projection
  metadata is unavailable;
- DuckDB remains the exclusive owner of filter, ordering, limit, and offset,
  so filter-before-limit cannot be delegated or applied early;
- connector ceilings and host ceilings are intersected; a connector cannot
  widen destination or resource authority;
- unknown identity, schema or extractor drift, operation drift, reordered or
  altered request metadata, policy widening, incomplete projection, non-TRUE
  unavailable predicate, ordering, bounds, or unsupported adapter capability
  is rejected deterministically; and
- unrelated environment changes cannot alter the plan because authority comes
  only from the compiled snapshot and host policy input.

Query integration supplies the differential oracle: byte-identical request
targets for `WHERE`, `ORDER BY`, `LIMIT/OFFSET`, and filter-before-limit
variants, with results equal to DuckDB evaluation over the same returned base
rows.

## Interaction exit

The Connector interaction exits when the accepted immutable native metadata
drives the complete golden plan without Connector internals or YAML knowledge.
The Query interaction exits when supported capability-profile and prepared-plan
oracles pass without Query constructing or interpreting `ScanPlan`. The Remote
Runtime interaction exits when direct runtime tests consume the typed
executable facts while ignoring planner-only ownership and explanation fields.

Until all three conditions are supported by final source and test dependencies,
the Relational Semantics collaboration remains **Open**. Once satisfied, the
typed `ScanPlan` becomes an X-as-a-Service boundary maintained by Relational
Semantics.
