# Connector Experience plan: stable local connector packages

## Outcome and authority

Status: **In progress as of 2026-07-21; accountable stream.** Query, Remote
Runtime, and Engineering Enablement interactions are confirmed exited to
X-as-a-Service by source/test-dependency audit. The Relational Semantics
interaction remains Collaboration: native/package plan differentials are
proven only for the GraphQL relation, not the three REST GitHub relations
(see [the goal's Completion record](../stable-local-connector-packages.md)).

Connector Experience will turn the complete accepted `duckdb_api/v1` source
contract into one immutable package generation. An author can load the
repository GitHub package and receive deterministic, source-located evidence;
invalid or unsafe source never reaches publication. Accepted
[RFC 0013](../../../rfcs/0013-define-connector-package-v1-contract.md) governs
the author contract and [RFC 0012](../../../rfcs/0012-define-local-package-sql-registration.md)
governs its DuckDB projection.

The team owns local source custody, failsafe YAML, closed schema and semantic
validation, GraphQL generation, diagnostics, digest and source identity,
compiled facts, compatibility, fixture coverage, and safe explanation. It
does not own DuckDB catalog mutation, relational interpretation, transport,
credential resolution, or active-generation lifecycle.

## Permanent service boundary

Connector will provide these bounded operations:

```text
OpenLocalPackageRoot(absolute_root, limits, cancellation)
    -> opaque immutable source snapshot

CompilePackage(source_snapshot, limits, cancellation)
    -> generation | bounded ordered diagnostics

ClassifyReload(active_generation, candidate_generation)
    -> exact_no_op | compatible_patch | compatible_minor | incompatible

RunPackageFixtures(generation, fixture_root, provider_services, cancellation)
    -> bounded deterministic report
```

`CompiledPackageGeneration` owns spec, connector, package and digest identity;
safe source coordinates; ordered relations; structural outputs and inputs;
typed defaults; authentication shape; immutable operation, predicate, policy,
and resource facts; and a safe explanation. Query receives a deliberately
narrow registration view and an opaque generation owner. Semantics receives
compiled semantic facts. Runtime receives no Connector value: only the final
`ScanPlan`.

Production responsibilities remain separately understandable:

- package-source custody and framed digest;
- bounded YAML event/tree reading;
- exact schema validation and cross-reference validation;
- compiled package model and compatibility descriptor;
- REST and structured GraphQL compilation;
- stable diagnostics and safe explanation; and
- fixture index, coverage derivation, and fixture orchestration.

No catch-all compiler module may retain all of those reasons to change. The
accepted JSON schemas are copied byte-for-byte into Connector-owned product
assets and checked by source identity.

## Acceptance evidence

The Connector oracle proves the complete accepted v1 inventory, not only the
four repository examples:

- absolute no-follow root acquisition, exact semantic-file enumeration,
  before/after identity checks, hard-link and special-file rejection, bounded
  immutable reads, cancellation, and the length-framed digest golden;
- every admitted and excluded YAML lexical form at boundary and one-over
  limits, duplicate-key coordinates, deterministic diagnostic ordering,
  redaction, and the 255-detail plus terminal resource record rule;
- every structural schema alternative and cross-reference, typed scalar,
  uniqueness, operation-selection, auth/network, resource, predicate, REST,
  GraphQL, pagination, and compatibility law;
- a second controlled package that exercises typed defaults, explicit NULL,
  operation fallback and conflict, exact and superset predicates, and a
  distinct structured GraphQL query;
- all 258 independently derived GitHub coverage keys and package fixtures;
  fixture index/source/body identity failures occur before payload trust; and
- native/package catalog, plan, request, result, error, and explanation parity
  for all four GitHub relations.

The first integrated slice is a private one-relation anonymous REST package.
It uses the permanent source, compiler, generation, Semantics, Runtime, and
Query services and remains private until every v1 declaration form is
implemented. A partial compiler is never exposed as `duckdb_api/v1`.

## Dependencies and interaction exits

- **Query Experience — Collaboration to X-as-a-Service.** Exit when Query
  derives generated names and arguments only from the structural registration
  view, pins an opaque generation, and parses no package, type, selector,
  protocol, or explanation syntax.
- **Relational Semantics — Collaboration to X-as-a-Service.** Exit when the
  planner consumes compiled facts through the public service, imports no
  compiler internals, and complete native/package semantic differentials pass.
- **Remote Runtime — X-as-a-Service consumer.** Exit when Runtime accepts only
  immutable plans and contains no connector-name, package-version, source, or
  compiler dependency.
- **Engineering Enablement — Facilitation.** Exit when Connector owns and can
  update its schema, mutation, digest, fixture, and source-identity gates
  without an approval queue.

Connector production is listed once behind its provider targets. Consumer
tests may use bounded Connector fixtures but must not compile Connector source
directly or import private construction access. Adjacent APIs document source
custody, identity, immutability, thread safety, cancellation checkpoints,
error ownership, redaction, resource authority, compatibility, and lifetime.
