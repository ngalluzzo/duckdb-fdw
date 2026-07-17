# Connector Experience team charter

Connector Experience is the stream-aligned team for the path from a
well-structured API definition to a trusted connector package. This charter
inherits `docs/TEAM_TOPOLOGY.md` and cannot override `AGENTS.md`, an accepted
RFC, or the product and engineering contracts.

## Mission and customers

Serve connector authors and maintainers by making connector packages
declarative, understandable, safely configurable, deterministically testable,
and diagnosable without requiring Rust code for the common path.

The team is accountable when the primary acceptance narrative ends with an
author creating, validating, testing, explaining, distributing, or maintaining
a connector package.

## Responsibilities

- Own the connector-author workflow, package organization, schemas, validation,
  source-aware diagnostics, examples, fixture authoring experience, and
  explanation of compiled declarations.
- Maintain the author-facing contract in
  `docs/CONNECTOR_SPECIFICATIONS.md` and its agreement with architecture,
  compiled IR, validation, diagnostics, and tests.
- Compile validated packages into immutable `CompiledConnector` data with
  stable identifiers, normalized defaults, preserved source locations, and no
  embedded credentials.
- Make author-visible failure behavior actionable and deterministic.
- Sponsor product RFCs whose outcome primarily changes connector author or
  maintainer behavior.

## Explicit non-responsibilities

- DuckDB SQL integration, adapter lifecycle, or query-visible execution.
- Relational proof for pushdown, residual ownership, ordering, limits, or
  cardinality.
- Transport, authentication execution, pagination engines, retry controllers,
  caching, backpressure, or runtime lifecycle internals.
- Product-policy decisions reserved to the product manager.

The team may contribute across these boundaries but does not acquire their
accountability.

## Team API and service expectations

Connector Experience provides validated packages and immutable
`CompiledConnector` snapshots to Query Experience, Relational Semantics, and
Remote Runtime. Consumers can expect stable identifiers, explicit policies,
source-aware diagnostics, and semantic information sufficient for conservative
planning and bounded execution.

The team expects:

- Query Experience to expose only declared and supported package behavior;
- Relational Semantics to define the proof obligations for author-declared
  relational mappings; and
- Remote Runtime to surface documented capability, policy, and fixture
  interfaces without leaking transport implementation into package syntax.

An interface change follows `docs/RFC_PROCESS.md` and requires affected
consumer review.

## Decision rights and review lens

Agents operating in this charter are delegated reversible, charter-local
implementation and author-workflow decisions that preserve accepted contracts
and team APIs. The lead agent retains all other technical decision authority.
Public package behavior, compatibility, exclusions, security policy, and other
reserved choices require the product manager under `AGENTS.md`.

When consulted, evaluate whether:

- an author can express the capability without hidden code or runtime
  knowledge;
- defaults, validation, errors, examples, and explanation agree;
- the compiled representation is explicit and immutable;
- secrets and host authority remain outside distributed packages; and
- the author path has a deterministic fixture-based oracle.

## Code documentation expectations

Document connector data models and compilation entry points beside the code:
identifier stability, schema and nullability meaning, extractor semantics,
defaults, source provenance, immutability, and which information consumers may
rely upon. Mark internal acceptance metadata distinctly from public package
compatibility. A reader should be able to inspect and explain a
`CompiledConnector` without learning DuckDB callback, relational-planner, or
runtime-lifecycle internals.

## Success evidence

- Representative packages validate or fail with precise source diagnostics.
- Package compilation is deterministic and produces stable explainable IR.
- Fixture tests prove generated requests, extraction, pagination declarations,
  and error behavior without live-service dependence.
- Connector authors can complete the accepted workflow without bespoke runtime
  coordination.

## Cognitive-load limits

Keep protocol execution, DuckDB version coupling, relational theorem proving,
and host resource enforcement behind explicit team APIs. Repeated need for
authors to understand those internals is evidence that an interface has leaked.

## Supported interactions

| Partner | Mode | Purpose | Exit condition |
| --- | --- | --- | --- |
| Relational Semantics | Collaboration, then X-as-a-Service | Prove the semantic contract for new author declarations | Validation and planning share an executable oracle with conservative fallback |
| Remote Runtime | Collaboration, then X-as-a-Service | Prove protocol or operational declarations and fixtures | Compiled declarations use a documented low-friction runtime interface |
| Query Experience | Collaboration for user-visible boundary changes | Keep package behavior and query behavior coherent | End-to-end author-to-query evidence passes |
| Engineering Enablement | Facilitation | Transfer package testing or author-tooling practice | Connector Experience maintains the practice independently |

## Consultation disposition

Return `Approved`, `Objected`, or `Needs evidence` with the affected author
workflow or `CompiledConnector` contract, concrete evidence, required action,
and the interaction exit condition. Preference for different syntax is not an
objection without an author, compatibility, safety, or contract consequence.
