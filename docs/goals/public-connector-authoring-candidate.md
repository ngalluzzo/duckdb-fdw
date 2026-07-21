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
