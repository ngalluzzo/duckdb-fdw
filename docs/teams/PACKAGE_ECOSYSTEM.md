# Package Ecosystem team charter

Package Ecosystem is the stream-aligned team for the path from a validated
connector package to governed ecosystem availability. This charter was
established by [RFC 0015](../rfcs/0015-establish-package-ecosystem-and-trust-provenance-teams.md).
It inherits `docs/TEAM_TOPOLOGY.md` and cannot override `AGENTS.md`, an
accepted RFC, or the product and engineering contracts.

## Mission and customers

Serve connector publishers, package consumers, and registry operators by
making a validated connector package safely publishable, discoverable,
acquirable, verifiable, composable, reproducible, governable, and recoverable
without weakening its semantics or granting remote registry state execution
authority.

The team is accountable when the primary acceptance narrative ends with a
publisher releasing a package, a consumer acquiring and locking a
composition, or a registry operator running the service those workflows
depend on.

## Responsibilities

- Own immutable package artifacts and content-addressed storage.
- Own composition manifests, dependency resolution, and lockfiles.
- Own offline restore and compiled composition candidates handed to Query
  Experience.
- Own registry coordinates, namespaces, and publisher identities, kept
  independent of embedded connector IDs and generated SQL names.
- Own the OCI distribution profile and exact verified acquisition semantics.
- Own release records, release-lifecycle governance (retention, deletion,
  yanking, deprecation, and advisories), discovery metadata, and search.
- Own mirrors, bundles, disaster recovery, and registry operations, including
  moderation workflow, availability, privacy, support, and cost control.
- Sponsor product RFCs whose outcome primarily changes publisher, consumer,
  or registry-operator behavior.

## Explicit non-responsibilities

- Connector language and package compilation semantics (Connector
  Experience).
- DuckDB catalog activation and SQL lifecycle (Query Experience).
- Relational pushdown proof (Relational Semantics).
- Generic transport implementation (Remote Runtime).
- Signing, provenance verification, trust-root management, and the
  adversarial threat corpus (Trust & Provenance).
- Permanent enabling or security-review ownership by Engineering Enablement.

The team may contribute across these boundaries but does not acquire their
accountability.

## Team API and service expectations

Package Ecosystem consumes validated packages and immutable `CompiledConnector`
snapshots from Connector Experience without reinterpreting package meaning.
It provides Query Experience an exact compiled composition candidate: graph
and lock identity, ordered package generations, dependency edges, complete
generated SQL inventory, lifecycle eligibility already enforced, and safe
explanation. Query Experience must not perform registry lookups or trust
evaluation during activation; both are resolved upstream by Package Ecosystem
and Trust & Provenance.

The team expects:

- Connector Experience to supply a validated, immutable package with stable
  identifiers and no embedded credentials;
- Remote Runtime to supply bounded authenticated registry transfer with
  credential scoping, destination and redirect enforcement, cancellation, and
  resource limits, owning safe transport while Package Ecosystem owns what is
  fetched and why; and
- Trust & Provenance to supply a conservative-by-default trust-state verdict
  for a candidate artifact, never treating an unknown or ambiguous state as
  trusted.

An interface change follows `docs/RFC_PROCESS.md` and requires affected
consumer review.

## Decision rights and review lens

Agents operating in this charter are delegated reversible, charter-local
implementation decisions that preserve accepted contracts and team APIs. The
lead agent retains all other technical decision authority. Public registry
behavior, compatibility, exclusions, namespace policy, moderation policy,
security policy, privacy, retention, and other reserved choices require the
product manager under `AGENTS.md`.

When consulted, evaluate whether:

- a publisher or consumer action has one authoritative identity (content
  digest, registry coordinate, or lock) rather than an ambiguous one;
- namespace ownership changes never silently change a package's embedded
  connector ID or generated SQL identity;
- dependency resolution uses qualified ecosystem coordinates rather than an
  unqualified connector name;
- an outage, moderation action, or security incident cannot expose partial,
  unsigned, stale, or revoked package state as trusted; and
- the compiled composition candidate remains the only path Query Experience
  needs to reach an executable relation.

## Code documentation expectations

Document artifact identity, graph and lock construction, namespace and
publisher-coordinate semantics, and release-lifecycle state transitions
beside the code: which facts are immutable, which are only claims pending
Trust & Provenance verification, and which information Query Experience may
rely upon without re-deriving it. A reader should be able to inspect and
explain a compiled composition candidate without learning DuckDB catalog,
relational-planner, or transport-lifecycle internals.

## Success evidence

- A package can be published, discovered, acquired, verified, composed,
  reproduced, and recovered end to end without bypassing local compilation,
  lockfile, or activation contracts.
- Registry metadata cannot satisfy a dependency or trust decision through an
  unqualified connector name.
- Outage, moderation, and security-incident drills preserve exact identities
  and previously observed trust state.
- Query Experience implements composition activation against this team's
  service without registry-specific coordination per goal.

## Cognitive-load limits

Keep signing, provenance verification, and adversarial threat-model reasoning
behind Trust & Provenance's explicit team API. Keep DuckDB catalog and SQL
lifecycle behind Query Experience's explicit team API. Repeated need to
reason about cryptographic trust proofs or catalog activation internals is
evidence that an interface has leaked.

## Supported interactions

| Partner | Mode | Purpose | Exit condition |
| --- | --- | --- | --- |
| Connector Experience | X-as-a-Service | Consume validated packages and compiled structural metadata | Package Ecosystem consumes packages without reinterpreting package meaning |
| Trust & Provenance | Collaboration, then X-as-a-Service | Resolve trust state for a candidate artifact before composition | Package Ecosystem consumes trust verdicts without re-deriving threat-model logic |
| Remote Runtime | Collaboration, then X-as-a-Service | Prove bounded authenticated registry transfer | Package Ecosystem uses a documented low-friction transport interface |
| Query Experience | Collaboration for the composition-candidate boundary, then X-as-a-Service | Keep composition and activation behavior coherent | End-to-end publish-to-activation evidence passes |
| Engineering Enablement | Facilitation | Transfer supply-chain testing, identity operations, disaster-recovery, and launch-gate practice | Package Ecosystem maintains the practice independently |

## Consultation disposition

Return `Approved`, `Objected`, or `Needs evidence` with the affected
publisher, consumer, or registry-operator workflow, concrete evidence,
required action, and the interaction exit condition. Preference for
different syntax is not an objection without a compatibility, safety, or
contract consequence.
