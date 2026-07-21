# Trust & Provenance team charter

Trust & Provenance is the complicated-subsystem team that supplies Package
Ecosystem with conservative-by-default trust evaluation: signing, provenance
attestation, trust-root management, and the registry's adversarial threat
corpus. This charter was established by [RFC 0015](../rfcs/0015-establish-package-ecosystem-and-trust-provenance-teams.md).
It inherits `docs/TEAM_TOPOLOGY.md` and cannot override `AGENTS.md`, an
accepted RFC, or the product and engineering contracts.

## Mission and customers

Serve Package Ecosystem, its sole customer, by answering one question
correctly and conservatively: given a candidate artifact, its claimed
provenance, and its namespace or publisher coordinate, what is its verified
trust state, and does verification succeed under an adversary who may
substitute artifacts, squat namespaces, confuse dependencies, typosquat,
compromise a publisher or mirror, roll back or freeze metadata, compromise a
key, transfer ownership, or equivocate.

Trust and provenance reasoning is isolated from Package Ecosystem's general
distribution and catalog mechanics for the same reason Relational Semantics is
isolated from Connector and Query Experience: it requires specialist,
adversarial, conservative-by-default reasoning that general service-operations
work does not, and mixing the two risks exactly the kind of trust shortcut
this team exists to prevent.

## Responsibilities

- Choose and implement the signing scheme and its verification.
- Bind publisher identity to signed releases.
- Manage trust-root distribution and rotation.
- Define and verify the provenance attestation format.
- Own the revocation, advisory, and quarantine state machine and its
  safe-default semantics: an unknown or ambiguous trust state is never
  reported as trusted.
- Own the adversarial threat corpus and its executable oracles: artifact
  substitution, mutable-version overwrite, namespace squatting, dependency
  confusion, typosquatting, compromised publishers, stale or malicious
  mirrors, metadata rollback/freeze/mix-and-match, key compromise, ownership
  transfer, equivocation, and registry recovery.
- Sponsor non-product RFCs for supply-chain trust and security-modeling
  objectives.

## Explicit non-responsibilities

- Artifact storage, content-addressed transport, and OCI distribution
  mechanics (Package Ecosystem).
- Namespace CRUD, discovery, and search (Package Ecosystem).
- The dependency-resolution algorithm and lockfile construction (Package
  Ecosystem).
- Moderation process or human workflow — this team supplies the
  revocation and advisory primitives moderation acts through, not the
  moderation workflow itself (Package Ecosystem).
- Mirrors, disaster recovery, and registry operating cost (Package
  Ecosystem).
- DuckDB catalog activation and SQL lifecycle (Query Experience).

The team may contribute across these boundaries but does not acquire their
accountability.

## Team API and service expectations

Trust & Provenance accepts a candidate artifact digest, claimed provenance,
and namespace or publisher coordinate from Package Ecosystem, and returns a
conservative-by-default trust-state verdict and provenance-verification
result. It does not interact directly with Connector Experience, Query
Experience, or Remote Runtime; Package Ecosystem is the sole integration
point, so activation-time code never needs registry trust-evaluation logic.

The team expects Package Ecosystem to:

- supply the exact artifact identity and every claimed provenance fact
  needed for verification, not a partial or pre-filtered summary;
- treat a verdict of unknown, ambiguous, or unverified exactly like a
  negative verdict; and
- never bypass a returned verdict through an ambiguous internal "verified"
  state that combines signatures, namespace authorization, package
  validation, fixture results, and project certification.

An interface change follows `docs/RFC_PROCESS.md` and requires Package
Ecosystem's review.

## Decision rights and review lens

Agents operating in this charter are delegated reversible, charter-local
cryptographic and threat-modeling implementation decisions that preserve
accepted contracts and team APIs. The lead agent retains all other technical
decision authority. Trust-policy choices, key-management policy, and other
reserved choices require the product manager under `AGENTS.md`.

When consulted, evaluate whether:

- a trust verdict is conservative by default under every named threat-corpus
  scenario;
- signature, namespace authorization, package validation, fixture evidence,
  advisory state, and project certification remain separately visible rather
  than collapsed into one ambiguous state;
- key compromise, rotation, and equivocation are handled without silent
  trust-state rollback; and
- the proof obligation has a deterministic, adversarial fixture oracle, not
  only a documentation description.

## Code documentation expectations

Document the trust-state model, signature-verification entry points, and
threat-corpus fixtures beside the code: which facts are cryptographically
proven, which are unverified claims, and which failure paths are safe
defaults versus hard rejections. A reader should be able to inspect and
explain a trust-state verdict without learning Package Ecosystem's storage,
namespace, or resolution internals.

## Success evidence

- Every named threat-corpus scenario has a deterministic, adversarial fixture
  proving the conservative-by-default outcome.
- A compromised key, publisher, or mirror cannot produce a verdict of
  trusted.
- Package Ecosystem consumes trust verdicts as an opaque, typed result
  without re-deriving verification logic.

## Cognitive-load limits

Keep artifact storage, namespace CRUD, discovery, moderation workflow, and
registry operations behind Package Ecosystem's explicit team API. Repeated
need for Package Ecosystem to reason about signature verification or the
threat corpus directly is evidence that this interface has leaked.

## Supported interactions

| Partner | Mode | Purpose | Exit condition |
| --- | --- | --- | --- |
| Package Ecosystem | Collaboration, then X-as-a-Service | Resolve trust state for a candidate artifact before composition | Package Ecosystem consumes trust verdicts without re-deriving threat-model logic |
| Engineering Enablement | Facilitation | Transfer supply-chain security testing, key-management operations, and adversarial-fixture practice | Trust & Provenance maintains the threat-corpus oracle independently |

## Consultation disposition

Return `Approved`, `Objected`, or `Needs evidence` with the affected trust-state
guarantee or threat-corpus scenario, concrete evidence, required action, and
the interaction exit condition. Preference for a different cryptographic
scheme is not an objection without a security, compatibility, or contract
consequence.
