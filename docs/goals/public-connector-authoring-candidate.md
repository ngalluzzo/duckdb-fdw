# Goal: Public connector authoring and API candidate

Follow `docs/PRODUCT_DELIVERY.md`.

Status: **Active**. [RFC 0014](../rfcs/0014-adopt-release-support-and-backport-policy.md)
is Accepted and satisfies `0.9.0`'s release-and-support-policy prerequisite.
No other RFC gate is open for this goal as scoped: it validates and freezes
already-accepted behavior (RFC 0009, RFC 0012, RFC 0013) rather than
introducing new public behavior. If freezing the `1.0.0` contract surfaces a
genuine gap not already answered by an accepted RFC, delivery stops at that
boundary per `docs/RFC_PROCESS.md` rather than deciding it silently.

## PM brief

### Outcome

For connector authors and the eventual `1.0.0` stable release, prove that the
`duckdb_api/v1` package contract accepted and delivered in `0.8.0` is
portable — not an artifact of the one real package (GitHub) it has been
proven against — and freeze the complete `1.0.0` public contract as one
enumerated, testable artifact, so `1.0.0` can be built and supported on
evidence instead of assumption.

### Why now

`0.8.0` proved the accepted v1 subset end to end, but against exactly one
real package. Per `ROADMAP.md`, `0.9.0` is explicitly a validation and freeze
release — "No public declaration, SQL behavior, lifecycle mechanism, or
compatibility surface is first implemented in this release" — and is the
required gate before any `1.0.0` compatibility rehearsal (`1.0.0-rc.N`) can
begin.

### Product guardrails

- Must: freeze the complete `1.0.0` public contract (SQL/extension naming,
  configuration, diagnostics, explain/version surfaces, the stable
  connector-spec candidate, migration/deprecation rules, supported
  compatibility cells, distribution path, explicit exclusions) as one
  enumerated, testable artifact.
- Must not: introduce new public SQL, lifecycle mechanisms, or compatibility
  surfaces. `0.9.0` proves and freezes; it does not expand.
- Preserve: `0.8.0`'s accepted v1 subset, its migration/reload guarantees
  (RFC 0012, RFC 0013), and RFC 0014's release/support/backport policy.

### Success signals

- A second, independently authored connector package (targeting a real API
  meaningfully different in shape from GitHub — a different auth style,
  pagination style, or response shape) validates, compiles, explains,
  fixture-tests, and loads through the same `0.8.0` path.
- Migration fixtures prove that equivalent package inputs across both
  packages produce equivalent compiled output and diagnostics.
- An unsupported connector-spec version or extractor dialect fails
  explicitly and safely rather than being silently reinterpreted.
- The `1.0.0` public contract is a discoverable, enumerated artifact a
  connector author or reviewer can read without inspecting source.

## Agent commitment

### Observable interpretation

A connector author who has never seen the GitHub package points
`duckdb_api_load_connector` at a second, independently authored package
targeting a different real REST or GraphQL API, and it validates, compiles,
explains, fixture-tests, and loads exactly like GitHub's — proving the v1
contract, not one example package, is the actual product. A reviewer reads
one frozen document enumerating everything `1.0.0` will promise, cross-checked
against the schema-backed public inventory already gated in `0.8.0`.

### Acceptance evidence

- Demonstration: load, inspect, query, and reload the second package through
  DuckDB exactly as the GitHub demo already does; compare its compiled
  catalog, plan, and diagnostic shapes against GitHub's for the parts of the
  v1 contract both packages exercise.
- Automated oracle: package-independent compiler/fixture/migration tests
  (the second package's own fixture corpus, parallel to GitHub's), a
  frozen-`1.0.0`-contract inventory test analogous to
  `release/public-surface/inventory.json`'s existing schema-backed gate, and
  explicit-failure tests for an unsupported connector-spec version or
  extractor dialect.
- Quality gates: `make build`, `make test`, `make demo`, source and
  dependency identities, agent-asset and public-inventory validation,
  staged/unstaged whitespace checks, and the `0.9.0` compatibility-matrix
  evidence run named in `ROADMAP.md`.
- Independent review: Connector Experience (author-contract), Query
  Experience (frozen SQL/diagnostic surface), and Engineering Enablement
  (compatibility-matrix and public-inventory-freeze mechanics), plus the
  final topology interaction-exit audit.

### Contract and invariant impact

- Propagates the `1.0.0` freeze into `docs/ARCHITECTURE.md`,
  `docs/CONNECTOR_SPECIFICATIONS.md`, `docs/RUNTIME_CONTRACTS.md`,
  `release/public-surface/inventory.json`, and a new frozen-contract artifact
  under `release/` once its shape is decided.
- Preserves every invariant in `AGENTS.md` and RFC 0009/0012/0013/0014's
  accepted decisions. Introduces no new relational, security, resource, or
  lifecycle behavior.

### Team and RFC routing

- Accountable stream: Connector Experience (the primary acceptance narrative
  ends with a connector author validating, compiling, explaining, and
  loading a package).
- Query Experience — **X-as-a-Service.** Query's own SQL/diagnostic/version
  surface must be enumerated into the same frozen `1.0.0` contract. Exit when
  Query's public inventory entries are self-maintained against the existing
  schema-backed gate without new Connector-Experience coordination.
- Engineering Enablement — **Facilitation.** Derives the exact supported
  DuckDB/profile/platform/architecture/installation matrix from passing
  evidence and freezes final inclusions/exclusions. Exit when Connector and
  Query teams can run the compatibility-matrix and inventory-freeze gates
  independently.
- Remote Runtime, Relational Semantics — no new interaction. `0.9.0`
  introduces no new capability for either team to provide; their already-
  accepted behavior is included in the freeze by reference, not re-decided.
- RFC: RFC 0014 is Accepted and closes the one identified gate. No other RFC
  gate is open for the scope above. If enumerating the `1.0.0` contract
  surfaces a public-behavior question not already answered by RFC 0009,
  0012, or 0013 (for example, a genuinely new compatibility exclusion), stop
  at that boundary and open a new RFC rather than deciding it inside this
  goal.

### Unknowns and first trial

- Unknown: does the `duckdb_api/v1` contract, proven only against GitHub's
  REST+GraphQL shape, actually generalize to a real API with a meaningfully
  different shape (different authentication style, pagination style, or
  response envelope), or does it have hidden GitHub-specific assumptions?
- Trial: author one complete second connector package against a real,
  publicly reachable API chosen to differ from GitHub's shape, and prove it
  through the full validate/compile/explain/fixture-test/load path before
  scaling the rest of `0.9.0`'s evidence (migration fixtures, contract
  freeze, compatibility matrix) around that proof.

**Decided (product manager, 2026-07-21):** the second package targets the
[Rick and Morty API](https://rickandmortyapi.com/) (`rickandmortyapi.com`).
It requires no authentication at all — proving the v1 contract does not
silently assume auth is always present. Its native multi-page listing
paginates via absolute `info.next`/`info.prev` URLs embedded in the response
body, a materially different shape than GitHub's `Link: rel=next` header —
but as delivered, both relations use `pagination: strategy: disabled`
(single fixed page) rather than following that body-embedded pagination; see
the Completion record for why. It carries no cost, rate-limit-purchase, or
terms-of-service risk suitable for an automated fixture-recorded connector.

### Delivery path

1. Resolve the second-package API choice, then author and prove it end to
   end (the first trial above) — this is the decision-value increment that
   determines whether the rest of `0.9.0` proceeds as scoped or surfaces a
   v1-contract gap requiring its own RFC.
2. Add migration fixtures proving equivalent inputs across both packages
   produce equivalent compiled output and diagnostics, and explicit-failure
   tests for unsupported connector-spec versions/extractor dialects.
3. Enumerate and freeze the complete `1.0.0` public contract as one
   artifact, cross-checked against the existing schema-backed public
   inventory.
4. Derive and freeze the exact supported DuckDB/profile/platform/
   architecture/installation matrix from passing evidence.
5. Audit the actual source/test dependencies against this responsibility
   map, complete independent review and repository gates, and record the
   completion evidence here.

## Completion record

**First trial delivered (2026-07-21).** A second, independently authored
`duckdb_api/v1` connector package now lives at
[`connectors/rickandmorty`](../../connectors/rickandmorty), targeting the
free, public Rick and Morty API. It validates, compiles, explains,
fixture-tests, and loads through the identical production path as
`connectors/github`:

- Two relations, both fully anonymous (no `credentials:` block at all):
  `pilot_episode` (a fixed single-object REST fetch) and `character_search`
  (a `terminal_collection` REST listing with one declared relation `input`,
  `status`, bound directly into a query field — a mechanism GitHub's package
  never exercises).
- A 139-key fixture-coverage corpus (`fixtures/index.yaml` plus 4 real-shaped
  JSON payloads), independently re-derived and verified byte-for-byte against
  the compiler's mechanical `DerivePackageFixtureCoverage` output during
  adversarial review.
- New parity test coverage: a compiler identity/shape test, a
  coverage-derivation golden test, a fixture-corpus-reaches-provider test
  (`test/cpp/connector/package/rickandmorty_package_compiler_tests.cpp`), and
  a SQL load/query-surface test
  (`test/cpp/query/packages/rickandmorty_package_surface_tests.cpp`).
- `make build`, `make test`, and `make demo` pass with this package and its
  tests wired in.

**Two genuine findings from this trial, not worked around:**

1. **`duckdb_api/v1`'s REST pagination does not generalize to a common real
   pagination shape.** Per `docs/CONNECTOR_SPECIFICATIONS.md`'s REST
   operations section, v1 supports only `disabled` pagination or sequential
   `Link: rel=next` header pagination. The Rick and Morty API's native
   multi-page listing instead embeds absolute `next`/`prev` URLs in the
   response body's `info` object — a materially different, common shape v1
   cannot represent at all. Both relations in this package therefore use
   `pagination: strategy: disabled` (one fixed page) rather than attempting
   to follow that pagination. This is a real candidate gap for the `1.0.0`
   contract-freeze phase (`docs/CONNECTOR_SPECIFICATIONS.md`'s pagination
   section would need a new accepted strategy, which requires its own RFC
   under `docs/RFC_PROCESS.md` — not a decision made here).
2. **Real end-to-end fixture execution is not wired up for either package
   today.** `RunPackageFixtures`' abstract `PackageFixtureExecutionService`
   port (`src/include/duckdb_api/package_fixture_runner.hpp`) has no concrete
   Semantics/Runtime integration anywhere in the repository — confirmed true
   for `connectors/github` as well, via
   `test/cpp/connector/package/package_fixture_coverage_tests.cpp`'s own
   `FirstCaseProbe`, which deliberately throws on the first case it reaches.
   What's actually verified today, for both packages, is schema validation,
   exact coverage-key agreement, and exact payload-digest agreement — proven
   by the fixture runner reaching that first case's provider call. Whether
   the fixture corpus's *expected rows/diagnostics* match real compiled
   behavior is not yet exercised by any automated gate.
3. **Adversarial review caught and fixed one integration gap before this
   record was written:** the new
   `duckdb_api_rickandmorty_package_compiler_tests` executable compiled under
   `make build` but was not wired into either `scripts/lib/native-dev-build.sh`
   (`make test`) or `scripts/run-native-product-tests.sh` (the release gate),
   so its 139-key coverage/digest/fixture-corpus assertions never actually
   ran. Fixed by adding it to both scripts' execution lists, matching its
   GitHub-package analogues.

Team review (`$topology-consult`, Route mode): Connector Experience
accountable, Query Experience and Engineering Enablement as recorded above —
routing unaffected by this trial's findings. No RFC was opened for the
pagination gap; it is recorded here as a decision-critical input to the
`1.0.0` contract-freeze delivery path (step 3), not decided.

**Step 2 delivered: package-independence oracle (2026-07-21).** A new
Connector-owned oracle at
`test/cpp/connector/package/cross_package_migration_tests.cpp` (plus a
`BuildRepositoryCrossPackageMigrationFixture` /
`CompileMigrationEnvelopeWithMutation` provider in the existing compiler
fixture service) proves the `duckdb_api/v1` contract, not either real package,
is the product:

- Equivalent valid inputs compile to equivalent output. A canonical
  `migration_probe` relation (anonymous, static schema, one `BIGINT` and one
  `VARCHAR` column, one nullable `VARCHAR` relation input bound into a REST
  query field with omission semantics, `terminal_collection` response, disabled
  pagination, full resource ceilings) is compiled under a github-profile
  envelope (GitHub's real bearer credential + `api.github.com` policy) and a
  rickandmorty-profile envelope (anonymous + `rickandmortyapi.com` policy). The
  two compiled relations are byte-for-byte equivalent across the whole contract
  surface — columns, inputs, operation identity/method/replay/response/pagination,
  the input-bound query field, resource ceilings — modulo only the operation
  origin host each policy admits and the package identity/digest.
- Authentication independence is proven directly: the github envelope declares a
  bearer credential for other relations while the rickandmorty envelope declares
  none, yet both anonymous `migration_probe` relations compile to obligation
  `NONE`. The v1 contract does not silently assume auth is always present.
- Equivalent malformed inputs produce equivalent diagnostics. The same
  one-over-the-connector-ceiling resource widening produces the same
  `POLICY_WIDENING`/`COMPILE` diagnostic set (same count, codes, phases,
  package-relative coordinates, and relation/operation identifiers) across both
  envelopes. Diagnostics carry package-relative coordinates, so the comparison is
  exact.
- Unsupported spec and dialect fail identically regardless of profile.
  `api_version: duckdb_api/v2` fails with `UNSUPPORTED_SPEC`/`SCHEMA` and
  `extractor_dialect: duckdb_api/unsupported` fails with `UNSUPPORTED_DIALECT`/
  `SCHEMA` in both envelopes, satisfying the explicit-failure acceptance signal.
- The two envelopes' digests are well-formed, differ from each other, and differ
  from both real repository packages, so the oracle cannot pass by accidentally
  reproducing a real package.

This step introduces no new public behavior (it is test and fixture service
only), so it required no RFC and no architecture/specification/runtime contract
edit. The pagination gap recorded above remains a step-3 input, not decided
here. `make build`, `make test`, the source-identity, native-dependency,
agent-asset, and public-inventory gates, and the fresh-root
`scripts/run-native-product-tests.sh` gate all pass with this oracle wired into
both the developer and release test lists.

**Step 3 delivered: 1.0.0 contract freeze (2026-07-21).** The complete `1.0.0`
public contract is now enumerated as one discoverable, machine-cross-checked
artifact pair at [`release/1.0.0/`](../../release/1.0.0):

- `release/1.0.0/freeze.md` is the readable enumeration of the six RFC 0009
  SemVer-governed categories, the four distinct version domains, the explicit
  exclusions, the recorded evidence limitations, and what is not yet frozen.
  It cites authority for each layer and cross-references the machine oracles
  rather than duplicating them.
- `release/1.0.0/freeze.json` is the machine-checkable declaration. A new gate
  (`scripts/verify-contract-freeze.py` plus the `contract_freeze` verifier
  module) and mutation suite (`test/python/contract_freeze_tests.py`) assert
  that the freeze agrees with its authoritative sources: the frozen SQL active
  and removed surfaces equal `release/public-surface/inventory.json`'s `0.9.0`
  release view exactly; the connector-spec identifier equals the schema const;
  the REST and GraphQL pagination strategy sets equal the schema's closed
  `oneOf`; the four version domains are intact; the mandatory exclusion set is
  present; and every cited RFC resolves to Accepted. The gate is wired into
  `AGENTS.md` "Current verification".

Two decision-critical boundaries recorded during the first trial were resolved
under product-manager direction rather than decided silently, per this goal's
governance rule:

1. **Pagination exclusion (product-manager decision: explicit exclusion).** v1's
   REST pagination strategy set is closed at `{disabled, link_next}` and
   GraphQL at `{relay_forward}`. Response-body-embedded next URLs, offset/page-
   number traversal, and cursor-in-body strategies — the common shape the Rick
   and Morty trial could not represent — are now an explicit `1.0.0` exclusion
   in `docs/CONNECTOR_SPECIFICATIONS.md` (closed-set description and
   compatibility-boundary enumeration) and `release/1.0.0/freeze.json`. No RFC
   was required: the closed `oneOf` already rejects these strategies today. A
   new schema-layer oracle
   (`test/cpp/connector/package/package_schema_contract_tests.cpp`,
   `TestUnsupportedPaginationStrategyRejected`) pins the rejection at
   `DUCKDB_API_UNSUPPORTED_DECLARATION` in the schema phase, so the exclusion
   rests on direct validation evidence rather than prose. Adding any such
   strategy remains a post-`1.0.0` RFC candidate.
2. **Fixture-execution evidence scope (product-manager decision: freeze now,
   record fast-follow).** The freeze proceeds on the schema, coverage-key, and
   payload-digest agreement basis already proven for both packages. Real
   end-to-end fixture execution (`PackageFixtureExecutionService` has no
   concrete Semantics/Runtime wiring) is recorded as a `1.0.0` fast-follow in
   `release/1.0.0/freeze.json` and `freeze.md`, not a blocker. The mutation
   suite fails closed if that fast-follow entry is removed.

This step introduces no new public SQL, lifecycle mechanism, or compatibility
surface, and no new relational, security, resource, or lifecycle behavior. The
`1.0.0` contract is therefore frozen as a candidate; the supported
DuckDB/profile/platform/architecture/installation matrix (step 4) and the
per-release `release/1.0.0/pins.json` / `public_contract.json` (at shipment)
remain the unfrozen `1.0.0` inputs and are marked as such in the freeze.

**Step 3.5 delivered: the 0.9.0 release itself was cut (2026-07-21).** Steps
1-3 above did real, substantial work under the `0.9.0` label but never
advanced the project's actual version identity: `extension_config.cmake`
remained pinned at `0.8.0`, no `release/0.9.0/` directory existed, and
`release/public-surface/inventory.json`'s `candidate_release` never moved past
`0.8.0`, even though `release/1.0.0/freeze.json` already declared the `0.9.0`
dispatcher removal as decided. This step closes that gap and completes the
work RFC 0012 actually specified for `0.9.0`:

- Removed the `duckdb_api_scan` dispatcher from the installed product
  (`LoadProduct` in `src/query/duckdb/extension_entrypoint.cpp` no longer
  registers it), while keeping it available as internal test-only composition
  for the adapter, auth, and lifecycle test suites that construct their own
  isolated `ExtensionLoader` and never exercised the real product path.
- Cut real `release/0.9.0/pins.json` and `public_contract.json` from actual
  computed source digests, dropping the `function` key and the four
  dispatcher-only diagnostics (`unknown_connector`, `unknown_relation`,
  `missing_relation`, `anonymous_secret_rejected`) that are no longer
  reachable now that a generated function's identity is fixed at registration.
- Advanced `release/public-surface/inventory.json` and `query-contract.json`'s
  `candidate_release` to `0.9.0` (`baseline_release` stays `0.7.0`: advancing
  it would prune `duckdb_api_scan`'s `0.7.0` revision and leave its `0.8.0`
  deprecated state as the ungoverned first revision, which the verifier
  rejects).
- Bumped every version anchor (`extension_config.cmake`, `CMakeLists.txt`,
  `scripts/verify-source-identities.py`'s `CURRENT_RELEASE` and the release
  pins consumed by both the cached developer loop and the fresh release gate)
  and backfilled a missing `HISTORICAL_RELEASES["0.8.0"]` immutability-ratchet
  entry that had never been added when `0.8.0` shipped.
- Wrote `docs/releases/0.9.0-notes.md` and fixed a `CHANGELOG.md` gap where
  `0.6.0`, `0.7.0`, and `0.8.0` had never received their own dated entries
  (`Unreleased` still described `0.8.0`'s content).

Steps 4 (compatibility matrix) and 5 (final audit) remain undelivered; `1.0.0`
itself has still not shipped.
