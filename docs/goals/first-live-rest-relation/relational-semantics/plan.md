# Relational Semantics plan: bounded live REST scan

## Outcome and status

Produce the deterministic, offline `ScanRequest -> ScanPlan` handoff for the
accepted `github.duckdb_login_search_page` relation. The plan describes the
complete zero-to-three-row base domain of one fixed response page, not the
unbounded GitHub user or search-result universe. Its fixed `q` and `per_page`
request values define that base domain; they are not DuckDB predicate or limit
pushdown.

Status: **Satisfied on `main`**. Connector Experience's immutable native
metadata, this planning service, the Query adapter, and Remote Runtime are
integrated in the permanent product graph at `f834eb0`. Query and Runtime
consume the resulting plan through its typed public interface without
constructing plan fields or reclassifying its relational decisions.

## Owned files

| File | Responsibility |
| --- | --- |
| `src/include/duckdb_api/scan_plan.hpp` | Typed immutable plan meaning, ownership classifications, applied capability and resource envelope, and safe explanation |
| `src/scan_plan.cpp` | Immutable plan access and stable safe explanation rendering |
| `src/scan_planner.cpp` | Side-effect-free validation and construction from the compiled connector plus conservative request |
| `test/cpp/scan_plan_contract_tests.cpp` | Typed plan shape, ownership, golden explanation, and determinism oracles |
| `test/cpp/scan_planner_tests.cpp` | Connector/request counterexamples, domain and host budget intersections, source-constant classification, and controlled-origin planning |
| `test/cpp/support/live_scan_request.hpp` | Shared construction of the conservative Query-to-Semantics test request |
| `docs/ARCHITECTURE.md` | Accepted native-preview architecture and evidence boundary under RFC 0005 |

Relational Semantics does not edit Connector metadata, Query request/adapter
code, Remote Runtime execution, build files, or shared design documents beyond
the targeted architecture propagation in this workstream.

The accepted contract-propagation task adds the targeted architecture edit
above. Engineering Enablement remains responsible for adding the new source
and focused test target to the integrated build graph.

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
- the fixed `per_page=3` source definition is a semantic ceiling in addition
  to the general host decoder ceiling: Connector may narrow `max_records` to
  one or two, but a mutated value of four is rejected during planning rather
  than deferred to Runtime;
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

## Responsibility audit

The original `src/scan_planner.cpp` combined two independently changing
responsibilities: validating and constructing a plan from provider inputs, and
implementing the immutable plan's accessors and safe explanation format. The
same pressure appeared in one test executable that mixed type-shape and golden
snapshot checks with planner counterexamples and resource-intersection laws.
The split above makes plan representation/explanation and planning policy
independently readable and testable without changing the public API or adding a
catch-all module. Test support contains only the shared conservative request at
the Query-to-Semantics boundary.

The public header remains one cohesive team API because every declaration is
part of the same immutable `ScanPlan` handoff. Its adjacent documentation now
distinguishes relational-domain bounds, host bounds, explanation provenance,
and Runtime enforcement authority. A further header split would fragment one
consumer contract without an independent reason to change.

## Contract propagation audit

| Layer | Disposition and evidence |
| --- | --- |
| Architecture | Updated the accepted native preview, bounded domain, typed team boundaries, exclusions, and controlled-versus-public evidence. |
| Connector syntax | Unaffected: RFC 0005 keeps package/YAML authoring inactive; this consumes only the compiled native snapshot. |
| Schema and validation | Connector's existing snapshot fixes `max_records=3`; planner counterexamples now reject a widened compiled value before returning a plan. |
| Compiled IR | Unchanged shape; the existing connector ceiling retains its distinct narrowing meaning. |
| Planning | The effective decoded-record budget accepts nonzero narrowing through three and cannot widen beyond the fixed response-page domain. |
| Execution | Unchanged service authority; Runtime enforces the accepted typed plan and may still fail closed on unsupported executable facts. |
| Diagnostics | Unchanged safe planning error class; no URL, credential, or response content is added. |
| Tests and fixtures | Separate plan-contract and planner-policy suites cover the golden value, one/two-row narrowing, the four-row counterexample, and immutable relational ownership. |

## Interaction exits

- **Connector Experience — Satisfied; X-as-a-Service.**
  `BuildConservativeScanPlan` consumes the immutable public
  `CompiledConnector` and protocol-neutral `ScanRequest`; it neither imports
  Connector implementation details nor reads YAML. The independently runnable
  `duckdb_api_scan_planner_tests` and
  `duckdb_api_scan_plan_contract_tests` targets prove the complete golden plan,
  deterministic construction, valid one-to-three-row narrowing, the widened
  four-row counterexample, and conservative rejection of inconsistent source
  declarations.
- **Query Experience — Satisfied; X-as-a-Service.** In
  `src/duckdb_api_adapter.cpp`, Query constructs only the conservative
  `ScanRequest`, obtains the plan from `BuildConservativeScanPlan`, and retains
  the immutable result in bind state. It consumes declared output types,
  resource bounds, and safe identity for adapter work but does not construct or
  inspect predicates, residual ownership, ordering or bound delegation, or the
  classification reason. The controlled relational oracle proves that
  `DESCRIBE` and `PREPARE` perform no request, prepared and ordinary scans agree,
  and filter, ordering, limit/offset, and filter-before-limit variants each
  preserve the exact single-request target while DuckDB produces the expected
  results. The focused adapter target consumes the Semantics and Runtime public
  interfaces without linking transport or decoder implementation.
- **Remote Runtime — Satisfied; X-as-a-Service.** Runtime compiles against the
  typed `ScanPlan` contract and validates only executable operation, schema,
  feature, network, and resource facts before performing I/O. Its source
  explicitly excludes source snapshot, predicates, relational ownership,
  ordering and limit delegation, and classification reason from executable
  validation. The independently runnable HTTP executor tests obtain plans
  through the Semantics service and prove structural request identity, typed
  batches, budget narrowing, no replay, cancellation, close, and one persistent
  deadline without importing or mutating planner internals.
- **Engineering Enablement — Satisfied; facilitation ended.** The integrated
  CMake graph names `RELATIONAL_PLANNING_SOURCES` separately and gives plan
  representation and planner policy their own focused targets. Cached
  `make build`, `make test`, and `make demo` passed, and fresh
  `make verify PROFILE=debug` rebuilt 618 targets and ran both Semantics suites,
  Runtime consumers, the Query adapter, the private 20-request controlled
  product oracle, and the installed product evidence. The verified delivery
  head and integration commit `f834eb0` have exact Git tree
  `f9f11018fa4671faa213ff9999adc9c7c72e9689`; Enablement retains the reusable
  graph and gates without becoming a semantic approval queue.

All temporary interactions are closed. The immutable, explainable `ScanPlan`
is now an X-as-a-Service boundary maintained by Relational Semantics. Its
consumers remain responsible for Query adaptation and Runtime execution, while
any change to relational classification or ownership returns to this team and
the accepted contract-change process.
