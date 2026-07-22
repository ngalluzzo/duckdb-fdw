# Product roadmap

This roadmap describes how duckdb-fdw intends to unlock useful product
outcomes from the first executable preview through a stable `1.0.0` release.
It is the living home for release sequence and product intent; architecture,
connector, and runtime documents define durable behavior without carrying
release sequencing.

The roadmap does not activate delivery work or establish a compatibility
promise by itself. Each release outcome is shaped and activated through
`docs/PRODUCT_DELIVERY.md`, assigned through `docs/TEAM_TOPOLOGY.md`, and routed
through `docs/RFC_PROCESS.md` when it introduces or changes a durable public or
shared contract.

## Planning rules

- Releases unlock coherent user or connector-author value, not percentages of
  component completion.
- Versions are evidence gates, not dates. A release ships only when its
  observable outcome and relevant correctness, security, lifecycle, and
  compatibility evidence pass.
- The ordered releases are the intended progression. Product learning may
  revise an unreleased outcome through the normal product process; published
  versions and artifacts never change.
- Patch releases may occur between the releases shown here. The roadmap names
  planned product outcomes, not every maintenance release.
- Scope not explicitly promised by a release remains uncommitted, even when a
  design document explores it.

## Versioning model

The project follows Semantic Versioning 2.0.0 and always writes complete
versions such as `0.1.0` and `1.0.0`.

During initial development:

- `0.Y.0` introduces a coherent preview outcome or an incompatible change to a
  previously published public surface.
- `0.Y.Z` contains backward-compatible correctness or security fixes only.
- Every incompatible change includes release notes and migration guidance,
  even though the project has not reached `1.0.0`.
- Only the latest `0.Y` release line is supported unless release notes
  explicitly state otherwise.

Beginning with `1.0.0`:

- a patch release contains backward-compatible fixes;
- a minor release contains backward-compatible additions or deprecations; and
- a major release contains an incompatible public change or removal.

The project uses `1.0.0-rc.N` for immutable release-candidate artifacts and
`vX.Y.Z` for Git tags. A release has one authoritative project version shared
by source metadata, extension metadata, its tag, artifact manifest, and release
notes. Conventional Commits inform release notes but do not determine version
changes without compatibility review.

Four version domains remain distinct:

| Domain | Contract |
| --- | --- |
| Project and extension version | SemVer for the documented duckdb-fdw product surface and observable behavior |
| Connector specification version | Compatibility identifier for the declarative language, currently `duckdb_api/draft` |
| Connector package version | Independent SemVer for one connector's relations, inputs, policies, schemas, and upstream adaptations |
| DuckDB compatibility | Tested matrix of DuckDB release, integration profile, platform, architecture, and installation mode |

DuckDB compatibility metadata is recorded in the artifact manifest and support
matrix. It is not encoded as a substitute for the project version. Any future
public native, WASM, Rust, or columnar ABI requires its own explicitly versioned
contract; internal Rust interfaces are not public merely because the project
reaches `1.0.0`.

## Release progression

### `0.1.0` — first trustworthy query

A DuckDB user can build and locally load the extension, query one static REST
relation backed by a deterministic fixture, and receive correct bounded
results. Binding performs no network I/O. Execution can be canceled and closed
safely, and at least one representative failure is meaningful and redacted.

Release evidence includes a clean build and load on one declared DuckDB and
platform target, an end-to-end query oracle, bounded execution, cancellation
and teardown checks, version introspection, an inventory of the preview public
surface, and immutable release artifacts.

### `0.2.0` — reproducible source build

A contributor can build a byte-reproducible extension artifact in a clean
environment, install it into an isolated DuckDB home, load it by name, identify
its version, and execute the `0.1.0` query on the first declared compatibility
target. An incompatible DuckDB host, platform, or artifact fails with an
actionable diagnostic.

This release proves artifact reproducibility and compatibility enforcement; it
does not require publication through an ordinary-user channel. Local unsigned
loading remains explicitly a contributor or controlled-preview path. The
selected ordinary-user distribution, signature, and trust path is exercised
only after the remote-query product has earned a public surface.

Release evidence includes an immutable source ref, artifact checksums and
provenance, exact DuckDB and platform identities, clean installation, restart,
load-by-name, version, query, byte-reproduction, and incompatible-cell tests on
every claimed row. Before `1.0`, fixes move forward without a backport
commitment. Project releases are immutable and support is best-effort through
GitHub Issues.

### `0.3.0` — first live REST relation

A DuckDB user can query one native, compiled-in, unauthenticated HTTPS REST
relation and receive strictly typed rows through the complete offline-bind,
plan, transport, JSON-decode, bounded-batch, and DuckDB-output path. This
release proves the central remote-query mechanism before declarative authoring
or package distribution becomes product work.

Release evidence includes a deterministic controlled HTTP oracle, one current
public-service compatibility demonstration, exact request and typed-row
checks, immutable prepared plans, post-DNS destination policy, hard response
and time budgets, cancellation, teardown, redacted status/network/decode
failures, and proof that bind and planning perform no network I/O.

### `0.4.0` — safe authenticated APIs

A DuckDB user can query the fixed `github.authenticated_user` relation through
one explicitly named temporary `duckdb_api/config` secret without granting
ambient credential or network authority. Bind and planning retain only the
logical name; every execution resolves current temporary-memory state and
moves an opaque capability into Runtime for the fixed bearer placement at
`api.github.com:443`. The anonymous relation remains credential-free.

Release evidence covers real DuckDB secret registration and resolution,
offline bind/describe/explain/prepare, prepared replacement and drop, exact
bearer placement, concurrent credential isolation, `401`/`403`, redirect
denial, DNS and resolved-address policy, bounded resources, redaction,
cancellation, close, recovery, and public-artifact canaries. Persistent and
environment providers, implicit selection, caller-selected authority, OAuth,
pagination, retry, and caching remain outside this contract.

### `0.5.0` — bounded real-world traversal

A DuckDB user can query the fixed `github.authenticated_repositories` relation
through an explicitly named temporary secret and receive the
duplicate-preserving bag of repository rows from every accepted page. Runtime
follows only sequential `rel="next"` transitions for the exact fixed GitHub
operation, reconstructs each request from typed plan state, and keeps one page
and one bounded output batch live at a time.

Release evidence covers controlled multi-page and empty-middle-page traversal,
strict continuation authority, per-page and aggregate resource accounting,
nonempty stream pulls, cancellation, deadlines, early close, late failure,
redaction, recovery, and a privacy-safe live compatibility check. Retries,
rate-limit waiting, parallel page requests, resume state, caller-selected page
inputs, and cache remain outside this release.

### `0.6.0` — semantic trust and explainable optimization

DuckDB users can apply projections, supported predicates, ordering, and limits
without changing query meaning. Pushdown distinguishes exact, superset, and
unsupported behavior; every residual has exactly one owner; unavailable
adapter capabilities fall back conservatively; and explain output truthfully
describes remote and local work. Correct local execution satisfies this release
when remote optimization is unavailable; remote projection, ordering, and
limit pushdown are not required outcomes.

Release evidence includes a protocol-neutral planner matrix for exact,
superset, unsupported, ambiguous, and failure classifications; composition and
`NULL` behavior; projection closure; filtering and required ordering before
limits; DuckDB-equivalence and property tests; strict schema and value
conversion; residual ownership; deterministic explanation; and
capability-profile fallbacks.

Current unreleased evidence completes the semantic-trust outcome without
expanding remote authority. Query translates a bounded typed predicate tree,
Relational Semantics classifies exact, superset, unsupported, ambiguous, and
invalid cases, and Runtime consumes only the resulting typed operation/input.
Actual-DuckDB oracles cover three-valued logic, duplicate-sensitive bags,
projection closure, total and non-total ordering, local limits and offsets,
prepared copies, conservative fallback, early close, and scan pruning. The sole
installed optimization remains
`github.authenticated_repositories.visibility = 'private'`; DuckDB owns every
filter, projection, ordering, limit, and offset.

The installed-path differential compares identical optimized and forced-local
SQL. Connector-backed production-planner laws cover Exact, Ambiguous, and
invalid states that intentionally cannot authorize the installed Runtime.
Operation selection uses declared eligibility, specificity, priority, and a
sole fallback, with tied winners and contradictory selectors failing before
execution.

### `0.7.0` — reusable protocol product path

A DuckDB user can query a second representative API shape through the same
permanent connector, relational-planning, runtime, and Query interfaces as the
existing REST product. Internal protocol machinery is not a release outcome by
itself.

REST and GraphQL remain in the v1 claim only if one representative GraphQL
relation passes the same user-visible SQL, semantic, network-policy, resource,
cancellation, diagnostic, and lifecycle oracles. Otherwise the product claim
is narrowed to REST before this release. Evidence includes a deterministic
corpus of protocol and error variations and proof that retained protocols
require no bespoke authority or lifecycle path.

Current unreleased evidence retains GraphQL through the fixed authenticated
`github.viewer_repository_metrics` relation. Connector publishes one
repository-owned canonical query profile; Relational Semantics carries it as
an immutable protocol-operation and cursor plan; Runtime admits and executes
it through the standard authorization, transport, resource, cancellation, and
`BatchStream` services; and Query exposes it through the unchanged
`duckdb_api_scan` function. DuckDB owns every relational operator, nullable
language values become SQL `NULL`, every nonempty GraphQL error array fails,
and sequential traversal stays within the accepted page, row, body, response,
memory, concurrency, and time budgets.

This outcome does not activate declarative GraphQL authoring. It proves the
permanent product path needed before `0.8.0` chooses and implements the complete
local-package subset.

### `0.8.0` — local package lifecycle and analytical workflows

Before this release begins, accepted product RFCs choose the intended SQL and
naming surface and the stable successor to `duckdb_api/draft`, including its
relationship to project and connector-package versions. The complete proposed
v1 package subset—not a throwaway or partial compiler—then uses the permanent
validation, compilation, explanation, fixture, loading, registration, and
reload path through repository-owned packages.

Users employ those relations in joins, materialization, export, prepared and
repeated scans. Operators reload packages without changing in-flight snapshots.
Release evidence covers every retained author declaration, package identity and
migration from bounded package previews produced under the accepted successor
contract, analytical workflow narratives, registration collisions,
all-or-nothing initial multi-relation publication and replacement reload,
rollback after late validation, registration, or collision failure, zero
partial visibility, unchanged prior registry state, immutable in-flight plans,
queue draining, worker joining where introduced, shutdown and exception
containment, and leak and resource-exhaustion checks across the declared
compatibility matrix.

### `0.9.0` — public connector authoring and API candidate

A connector author can validate, compile, explain, fixture-test, and load a
versioned declarative connector package using the complete subset already
implemented and exercised in `0.8.0`. Multiple independently authored packages
and migration fixtures prove that equivalent inputs produce equivalent compiled
output and diagnostics. Unsupported connector-spec versions or extractor
dialects fail explicitly rather than being silently reinterpreted.

The intended `1.0.0` public contract is enumerated and frozen for compatibility
testing. It includes normative SQL and extension naming, configuration,
diagnostics, explain and version surfaces, a stable connector-spec candidate,
migration and deprecation rules, supported compatibility cells, the chosen
distribution path, and explicit exclusions. No intentional public break remains
planned.

No public declaration, SQL behavior, lifecycle mechanism, or compatibility
surface is first implemented in this release. If another coherent preview is
needed, the roadmap adds `0.10.0` or later rather than turning the API candidate
or release candidate into feasibility work.

Release evidence includes a schema-backed validator, source-located
diagnostics, deterministic compile and explain oracles, offline fixtures,
package digest identity, connector package SemVer guidance, the public API
inventory, connector-spec migration fixtures, prior-package and prior-SQL
compatibility tests, a green declared DuckDB support matrix, installation and
upgrade narratives, release and support policies, and reproducible artifacts.

### `0.10.0` — body-signaled REST pagination

A connector author can declare a third REST pagination strategy, `response_next`,
that reads its continuation signal from a declared JSON path in the decoded
response body rather than an HTTP `Link` header, while preserving the
reconstruct-and-verify safety model `link_next` already enforces. This unlocks
the common shape the Rick and Morty trial could not represent under `0.9.0`
(body-embedded `info.next` absolute URLs).

Accepted [RFC 0016](rfcs/0016-decide-body-signaled-rest-pagination.md) decides
the design and commits its implementation to this release. The strategy is
architecturally identical to `link_next` (`dependency: sequential`,
`consistency: mutable`, `target_scope: exact_operation_origin_and_path`,
identical page-number/page-size/ceiling fields) except that the continuation
signal is read from a new required `next_url_path` field under the existing
`json_path_v1` extractor grammar, and a present value is validated against the
same generalized target-comparison rule `link_next` already applies to a
header value. The scoping spike RFC 0016's review required (decoder
single-pass-vs-second-parse and the encoding-normalization rule) gates
implementation before it commits.

This release is a backward-compatible capability addition under the pre-1.0
versioning model, so it is a `0.Y.0` minor rather than a `0.9.x` patch. The
`1.0.0` candidate freeze records `response_next` as an
`accepted_candidate_revision` until this release ships; graduation into the
schema-closed `pagination_strategies.rest` set happens with this release's
freeze snapshot.

Release evidence includes the new schema/decoder/compiled-IR branch, the
generalized Runtime validator with header/body target-source parity (including
at least one encoding-divergence case), the Relational Semantics
`BaseDomain`-equivalence property test, the Query-owned `EXPLAIN`
differential test, the corrected fixture-coverage variant set including the
`next_field_wrong_type_rejected` category GraphQL cursor pagination already
establishes, and adoption of the strategy in
[`connectors/rickandmorty`](connectors/rickandmorty)'s `character_search`
relation.

### `0.11.0` — static API-key credential

A connector author can declare a second static credential kind, `api_key`,
placed as an author-named fixed HTTP header or an author-named fixed URL
query parameter, alongside the existing `bearer` kind. This closes a gap an
architecture maturity assessment found ahead of the `1.0.0` release gate
below (>=10 connector providers): a large share of free/public REST APIs gate
access with a static key rather than a bearer token, and `duckdb_api/v1`
could not represent that shape at all.

Accepted [RFC 0018](rfcs/0018-add-static-api-key-credential.md) decides the
design and commits its implementation to this release. `api_key` is
structurally parallel to `bearer` — one static secret value, one fixed
destination set, no dynamic or computed credential behavior — with two
differences: placement may be a query parameter as well as a header, and the
header/parameter name is author-declared rather than fixed to
`Authorization`. Review corrected the original proposal in four places before
acceptance: Relational Semantics' `ValidateAuthentication` and
`ScanPlanBuilder::Build` must read the compiled connector's own credential
kind rather than hard-coding bearer; Remote Runtime's `SameHeaders`
pre-authorization check and its existing query-field-name uniqueness check
(owned by Runtime's admission path, not the compiler) both need to generalize
to the new kind; Query Experience resolves the secret to an opaque
kind-neutral value via an additive factory rather than a kind-aware
extension to its public registration-view shape; and the schema uses the
same closed-sibling-`$defs`-per-`oneOf`-branch idiom REST pagination already
established, not a new conditionally-required-field shape.

This release is a backward-compatible capability addition under the pre-1.0
versioning model, so it is a `0.Y.0` minor rather than a patch. The `1.0.0`
candidate freeze records `api_key` as an `accepted_candidate_revision` until
this release ships, alongside a correction to the mandatory exclusion
`authenticators_beyond_anonymous_and_capability_scoped_bearer`, which
`api_key` otherwise textually contradicts.

Release evidence includes the new schema/compiler/compiled-IR branch, the
corrected Relational Semantics plan-construction sites, the generalized
Remote Runtime admission and pre-authorization checks, an additive
`ScanAuthorization` resolution factory, header- and query-name-collision
fixture-coverage categories, the corrected `release/1.0.0/freeze.json`
exclusion and its mutation-test coverage, and a compatibility fixture proving
`connectors/github` and `connectors/rickandmorty` are unaffected. Choosing the
actual third connector provider that exercises `api_key` is separate,
PM-reserved work tracked as a follow-on, consistent with how Rick and Morty
was chosen as provider #2.

### `0.12.0` — count-terminated pagination

A connector author can declare a fourth REST pagination strategy,
`short_page`, that infers exhaustion purely from the just-decoded page
containing fewer rows than the declared page size (or zero rows), with no
server-supplied continuation signal at all — no `Link` header and no
body-embedded next-page URL. This closes the pagination shape a fresh
architecture-maturity re-assessment (run after `api_key` shipped) judged most
likely to block connector providers #3–#10 among free/hobby-tier REST APIs:
plain `?page=N&page_size=M` or `?offset=N&limit=M` pagination.

Accepted [RFC 0019](rfcs/0019-add-short-page-pagination.md) decides the
design and commits its implementation to this release. `short_page` reuses
the identical local page-reconstruction model `link_next`/`response_next`
already establish (`page_number_parameter`/`first_page`/`page_increment`),
with one difference: `page_size_parameter`/`page_size` are required, not
optional, since termination depends on a known page size. Unlike
`link_next`/`response_next`, it performs no reconstruct-and-verify step at
all — there is no external signal to validate, which review confirmed is a
strictly narrower trust surface than the other two strategies (at the cost
of losing their pagination-drift cross-check, an accepted, disclosed
tradeoff). Review corrected three points before acceptance: the compiled-IR
factory must be a named static method rather than a fourth overloaded
constructor (its required-field set is otherwise identical to `link_next`'s,
which C++ cannot overload on); the Query-owned acceptance criterion must
name a concrete real-`EXPLAIN` test rather than repeat RFC 0016's
`response_next` promise, which was never delivered; and the freeze-artifact
plan must add `short_page` directly to the schema-closed set rather than
edit the mandatory exclusion `pagination_body_url_offset_or_cursor_in_body_strategies`,
which `scripts/contract_freeze.py` forbids removing or renaming.

This release is a backward-compatible capability addition under the pre-1.0
versioning model, so it is a `0.Y.0` minor rather than a patch. Implementation
landed in the same change that accepted the RFC, so `short_page` graduates
directly into the schema-closed `pagination_strategies.rest` set without a
separate `accepted_candidate_revisions` interval, following `api_key`'s
precedent.

Release evidence includes the new schema/compiler/compiled-IR branch, the
`PlanBaseDomain` classification decision (grouped with
`link_next`/`response_next`, since domain depends only on response source,
never on the termination mechanism), `LinkPaginationState::AdvanceByCount`
reusing the existing pagination-state object with no new decode pass, the
corrected fixture-coverage variant set (`termination_on_short_page` and
`termination_on_empty_page` as distinct categories, plus
`exact_multiple_page_boundary`), a real-`EXPLAIN` test asserting the literal
`short_page` string in actual DuckDB output, and a compatibility fixture
proving `connectors/github` and `connectors/rickandmorty` are unaffected.

### `1.0.0-rc.N` — compatibility rehearsal

Each release candidate is an immutable build of the frozen `1.0.0` contract.
The exact artifacts complete clean-install, upgrade, migration, relational,
security, lifecycle, failure-path, and support or backport rehearsals. A later
candidate is a new version; an existing candidate is never rebuilt or relabeled
as final.

`1.0.0-rc.N` cannot begin until the `1.0.0` release gate below is satisfied.

### `1.0.0` — narrow stable contract

The declared public surface is stable and supportable. Every promised
compatibility-matrix row passes the complete release gate, unsupported
combinations fail clearly, and installable artifacts, release notes, migration
guidance, checksums, and provenance are reproducible and immutable.

**`1.0.0` release gate (product-manager decision, 2026-07-21):** `1.0.0` will
not ship until the project supports at least 10 different API providers in
[`connectors/`](connectors/) (two exist as of `0.9.0`: GitHub and Rick and
Morty). This dissolves the freeze-pressure rationale that drove the original
"defer to post-`1.0.0`" dispositions in RFCs considered during the `0.9.0`
window and reframes the `1.0.0` candidate freeze at
[`release/1.0.0/freeze.json`](release/1.0.0/freeze.json) as a snapshot that
subsequent `0.Y.0` releases (starting with `0.10.0`) deliberately extend via
`accepted_candidate_revisions` until enough provider coverage accumulates to
justify the stable contract. The freeze's authority chain is unchanged; only
the timeline to publication is unblocked from "next" to "when coverage is
sufficient."

RFC 0009 sets the intended stable boundary:

- the permanent native C++ DuckDB table-function extension on an exact tested
  DuckDB, platform, architecture, toolchain, and installation matrix, without a
  public C++ ABI;
- documented SQL functions, arguments, relation schemas, configuration,
  connection and secret behavior, diagnostics, explain, version
  introspection, and cancellation semantics;
- a local, explicit, declarative, read-only, static-schema connector
  specification whose accepted subset was completely implemented in `0.8.0`,
  with deterministic validation, compilation, explanation, fixtures, loading,
  package identity, and compatibility rules;
- REST and GraphQL support if the product claim survives `0.7.0` evidence;
- observable relational, security, resource, replay, and lifecycle guarantees;
  and
- installation and updates through a supported, trust-qualified distribution
  path on a published DuckDB, profile, and platform matrix.

Unless separately accepted and proven, the `1.0.0` guarantee excludes:

- the deep native catalog and optimizer integration profile and any Rust or
  stable-C-API replatform;
- public Rust, native plugin, WASM, custom-protocol, custom-pagination, or
  columnar binary ABIs;
- internal types and traits such as `CompiledConnector`, `ScanRequest`,
  `ScanPlan`, `BatchStream`, protocol service interfaces, runtime queues, and
  cache layout;
- central connector discovery or distribution registries, Git fetching,
  dependency resolution, lockfiles, package signing, and connector-package
  trust infrastructure; the bounded registry of explicitly loaded local
  packages is included;
- Tier 2 JQ-compatible transforms, Tier 3 code, column providers, partitions,
  automatic retry or rate-limit waiting, author-configurable cache or
  single-flight behavior, importers, and authenticators beyond anonymous and
  capability-scoped bearer behavior unless a later accepted RFC and pre-freeze
  evidence add them;
- dynamic schemas, write-back, transactions, and continuous streams; and
- compatibility with arbitrary upstream API drift.

## Release gate

Every published release must have:

1. an activated product goal with observable acceptance evidence;
2. accepted RFCs and propagated contracts for every triggered durable
   decision;
3. a reviewed public-surface change classification and the corresponding
   version increment;
4. deterministic correctness, security, lifecycle, and failure-path evidence
   proportional to the change;
5. green results for every compatibility-matrix cell called supported;
6. curated release notes and migration guidance for every incompatible change;
7. a clean, reproducible build tied to one immutable source commit and tag;
8. artifact identity, checksums, provenance, and a published support status;
   and
9. no unresolved design question inside the release's stated public outcome.

Release automation must reject a moving or reused tag, conflicting version
sources, replacement of an existing artifact, an unsupported matrix claim, or
missing compatibility and migration evidence.

## Release-governance decision and remaining gates

Accepted RFC 0009 chooses the native v1 profile, the categories governed by
project SemVer, the narrow candidate package boundary, the local-versus-central
registry distinction, and the dependency-ordered release progression. It
supersedes RFC 0004's requirement to complete Community publication as a
`0.2.0` gate while carrying forward its durable choices: the project remains
MIT licensed, DuckDB Community Extensions remains the ordinary-user
distribution and trust path, source build remains the contributor path,
releases remain immutable, the initial stable release must include the latest
stable DuckDB release and only passing Community rows, and support remains
best-effort through GitHub Issues. Publication evidence remains mandatory
before ordinary-user guidance and `1.0.0`.

The following decisions and evidence must precede the release that consumes
them:

- Before `0.8.0`, Query Experience accepts the exact SQL, naming, collision,
  preview-relation, and migration contract in a product RFC.
- Before `0.8.0`, Connector Experience accepts the stable successor to
  `duckdb_api/draft`, its compatibility and migration rules, and its
  relationship to project and package SemVer in a product RFC.
- Before `0.8.0`, Engineering Enablement establishes a schema-backed public
  inventory and change-classification gate; every domain team must maintain its
  entries independently before `0.9.0`.
- Before `0.9.0`, an Engineering Enablement release-and-support-policy RFC with
  product-manager approval decides support windows and removals, deprecation,
  migration, security response, maintenance, and backport policy. [RFC
  0014](rfcs/0014-adopt-release-support-and-backport-policy.md) is Accepted
  and satisfies this gate: best-effort, latest-release-only support, no LTS
  or backports, breaking changes still require a project-SemVer MAJOR
  increment with release-note migration guidance but no advance-notice
  window, and security reports use public GitHub Issues with redaction
  guidance plus GitHub private vulnerability reporting as a fallback for the
  transport/credential/host surface. See `SECURITY.md`.
- `0.9.0` derives the exact supported DuckDB/profile/platform/architecture/
  installation rows from passing evidence, including the latest stable DuckDB
  release at release time, and freezes the final inclusions and experimental
  exclusions.

Connector Experience, Query Experience, and Remote Runtime review their public
surfaces. Relational Semantics supplies correctness arguments and release
oracles. Product-manager approval remains required for every reserved public
compatibility, support, distribution, licensing, or exclusion choice.
