# Engineering Enablement plan: reproducible package delivery evidence

## Outcome and authority

Status: **Facilitation exit reached as of 2026-07-21.** All four domain
teams' focused CMake test suites are independently runnable with no
Enablement-owned allow-list gating a team's additions, and source/dependency
drift gates fail closed. 13 focused targets (fixture cancellation/execution/
failure/variant cases, REST plan admission, Semantics input-resolution) had
compiled but were never invoked by `make test` or the release gate; both now
run all 72 focused targets (see
[the goal's Completion record](../stable-local-connector-packages.md)).

Engineering Enablement will make the accepted package, generated SQL, fixture,
and migration evidence reproducible in the supported build cell, then transfer
each gate to the domain team whose behavior it checks. It does not own product
acceptance, package semantics, SQL behavior, planner correctness, or Runtime
policy.

No new parser or schema dependency is introduced by this plan. A durable
third-party dependency would require the governance and license/supply-chain
review not currently authorized by RFC 0013.

## Gate design

The reusable delivery surface covers:

- byte-identical copies and source identities for the two accepted schemas,
  GitHub package, fixture coverage descriptor, generated GraphQL vectors, and
  public SQL inventory;
- deterministic mutation drivers for closed schema shapes, YAML lexical and
  boundary cases, package identity, diagnostics, compatibility, and public
  inventory;
- focused Connector, Semantics, Runtime, Query coordinator, controlled
  product, SQLLogicTest, and migration targets discoverable through normal
  build interfaces rather than excluded trial commands;
- version and artifact identities for the implemented `0.8.0` surface,
  preserving the pinned dependency and clean-host direct-load contracts; and
- a clean full gate that proves the load-inspect-query-reload-reject narrative
  from a fresh build root without network-dependent correctness evidence.

Generated mutation cases may reduce repetition but cannot replace independent
semantic oracles. Checked-in evidence is deterministic and contains no
credentials, personal data, package roots, or live response material.

## Acceptance evidence and transfer

- Connector owns schema, YAML, digest, source, compiler, diagnostic, fixture,
  and compatibility entries.
- Relational Semantics owns law and native/package plan differential entries.
- Remote Runtime owns request, policy, resource, protocol, cancellation, and
  lifecycle entries.
- Query owns SQL inventory, catalog publication, generated/dispatcher
  differential, and migration entries.
- Enablement owns only the common harness, pinned environment, build graph,
  artifact identity, and transfer documentation.

Facilitation exits when each focused target is independently runnable, normal
`make` and fresh native gates include the new behavior, source/dependency
drift fails closed, and domain teams can add cases without editing an
Enablement-owned allow-list or waiting for approval. Root version, pin, release
record, and CI integration changes serialize under the lead after domain
interfaces and evidence are stable.
