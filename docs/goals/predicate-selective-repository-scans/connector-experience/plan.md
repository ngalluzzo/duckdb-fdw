# Connector Experience plan: native repository predicate metadata

## Outcome interpretation and authority

Status: **Planned; Connector RFC re-review approved; implementation evidence
pending**.

Provide the Connector Experience part of the active Query Experience outcome:
add one trailing required `VARCHAR` column named `visibility`, extracted from
`$.visibility` on each repository response object, and publish an immutable,
deterministic declaration telling Relational Semantics that
`github.authenticated_repositories` has exactly one mapping from the structured
predicate `visibility = 'private'` to the typed REST query input
`visibility=private`. The schema and declaration must be inspectable without
DuckDB, credentials, network access, request execution, or knowledge of GitHub
request-construction internals.

This plan approves the revised RFC from the Connector charter and proposes its
charter-owned provider boundary. It does not accept the RFC or independently
authorize the public/shared predicate-pushdown contract. The RFC technical
decision owner retains that decision, the product manager retains reserved
product choices, and Relational Semantics owns the implication proof,
classification, and residual owner. Here `D` is DuckDB
`visibility = 'private'` over the required upstream field and `R` is GitHub's
restriction to that specified visibility. The plan accepts the RFC's
conservative `SUPERSET` value and retained DuckDB residual; if delivery cannot
preserve `D => R`, the native catalog publishes no usable mapping and the
existing full traversal remains the behavior.

### Topology routing

- Accountable team: Query Experience for the DuckDB user's selective-scan
  outcome.
- Supporting team: Connector Experience for immutable native predicate facts,
  validation, safe explanation, and deterministic provider oracles.
- Affected teams: Relational Semantics consumes the mapping and owns its
  interpretation; Remote Runtime consumes only the resulting `ScanPlan`; Query
  Experience supplies structured predicate metadata and user-visible evidence.
- Interaction: Connector Experience and Relational Semantics collaborate until
  the metadata API and executable counterexample oracle are proven, then use
  X-as-a-Service.
- Decision authority: the RFC decision owner for the shared technical contract
  and the product manager for any reserved public behavior; this plan records
  Connector Experience input only.

## Scope

### In scope

- One trailing required `VARCHAR visibility` column with the strict
  `$.visibility` row extractor, plus one closed native predicate-to-input
  declaration for the existing `authenticated_repositories` relation and its
  existing operation.
- An immutable public Connector team API through
  `duckdb_api/connector_catalog.hpp`, with construction restricted to the
  canonical native builder and Connector-owned non-installable test access.
- Cross-field validation tying the mapping to an existing column, the selected
  operation, an allowed typed literal, a non-conflicting REST query input, and
  an evidence-backed accuracy value.
- Safe, locale-independent relation and catalog explanation that distinguishes
  fixed source query fields from a conditionally selected predicate input.
- Direct catalog, validation, snapshot, provenance, immutability, and
  credential-absence tests.
- A Connector-owned fixture service that lets Semantics and Query prove
  mapping presence, absence, and accuracy without constructing Connector
  internals or inferring capability from names or request shape.
- Preservation of both unaffected relation schemas, all existing repository
  column meanings and order before the one trailing addition, authentication
  policy, exact origin, pagination declaration, resource ceilings, and
  unfiltered behavior.

### Out of scope

- DuckDB filter extraction, capability reporting, SQL text inspection, bind or
  callback lifecycle, prepared-statement behavior, and public explain output.
- Relational implication, composition, operation selection, residual
  ownership, or `ScanPlan` construction.
- Applying a typed input to an HTTP request, validating received Link values,
  reconstructing later-page requests, transport, authorization, pagination
  state, budgets, cancellation, or close.
- Other predicates, any mapping from the broader `private` Boolean,
  `visibility = 'public'`, `visibility = 'internal'`, `visibility <> 'private'`,
  `IS NULL`, projection, ordering, limit, offset, retry, cache, GraphQL,
  provider, or partition work.
- General native input registries, arbitrary predicate operators or values,
  package/YAML syntax, connector loading, author tooling, distribution, or a
  public native ABI.
- Activating the `SUPERSET` mapping or choosing a continuation rule before RFC
  acceptance and the required controlled-oracle evidence exists.

## Source and oracle ownership

| Artifact | Connector Experience ownership |
| --- | --- |
| `src/include/duckdb_api/connector_catalog.hpp` | Public pre-`1.0` Connector team API for immutable predicate declaration access, closed value types, lifetime and compatibility documentation |
| `src/include/duckdb_api/internal/connector/predicate_declaration.hpp` and `src/connector/predicate_declaration.cpp` | Connector-private value validation and deterministic explanation; no planner, request builder, or execution logic |
| `src/connector/catalog_model.cpp` | Relation/catalog cross-field validation and composition of predicate explanation with existing schema, operation, pagination, authentication, and resource facts |
| `src/connector/native_github_composition.cpp` | Sole production declaration of the required `visibility` extraction and accepted native mapping, with coherent native metadata version activation after the RFC gate |
| `src/include/duckdb_api/connector.hpp` and `src/connector/README.md` | Adjacent no-I/O construction, ownership, consumer, and compatibility documentation |
| `src/connector/sources.cmake` | Connector-owned production source inventory for any new predicate value module |
| `test/cpp/connector/connector_predicate_contract_tests.cpp` | Closed value, validation, immutability, explanation, and negative-construction oracle |
| `test/cpp/connector/connector_contract_tests.cpp` | Exact installed six-column repository schema and mapping, preserved relations, canonical snapshot, provenance, locale, and prohibited-state regressions |
| `test/cpp/connector/support/catalog_test_access.hpp` | Connector-only invalid-value construction; never a consumer API |
| `test/cpp/connector/support/connector_catalog_test_fixtures.*` | Bounded mapping/decoy fixture service consumed through the public const metadata API |
| `test/cpp/connector/connector_catalog_test_fixtures_tests.cpp` | Direct proof that consumer fixtures are stable, explicit, credential-free, and do not require name/request inference |
| `test/cpp/connector/sources.cmake` and `test/cpp/connector/targets.cmake` | Connector-owned focused test inventory and provider target registration |

Connector owns the declaration oracle, not the relational truth oracle. The
following evidence remains outside this workstream:

| Oracle | Owner | Connector dependency |
| --- | --- | --- |
| `D => R`, `R => D`, composition, ambiguous/unsupported input, and residual laws | Relational Semantics | Public const mapping API and Connector fixture service |
| Structured DuckDB filter conversion, retained local predicate, offline lifecycle, and SQL equivalence | Query Experience | `ScanPlan` service, not Connector internals |
| Exact request trace, input preservation across pages, security, budgets, cancellation, and close | Remote Runtime | Typed selected input in `ScanPlan`, never `CompiledConnector` |
| End-to-end mixed-visibility result and smaller request sequence | Query Experience | Integrated provider services after the RFC is accepted |

Connector owns the required `visibility` column declaration and extractor
contract as well as the mapping declaration. Runtime owns strict conversion of
the declared value from accepted response rows; missing, `NULL`, or non-string
values are errors and never evidence that a row fails the predicate.

## Proposed public provider interface

The provider is public only as a bounded team API. It remains a private
pre-`1.0` C++ contract, not an installed native ABI or connector-package
compatibility promise.

Add one immutable `CompiledPredicateMapping` value exposed by
`CompiledRelation::PredicateMappings() const`. The value carries only declared
source facts:

| Fact | Closed native representation | Candidate repository value |
| --- | --- | --- |
| DuckDB column | validated stable column identifier | `visibility` |
| operator | closed equality operator | `EQUALS` |
| operand | typed closed scalar | required `VARCHAR` value `private` |
| operation | validated stable operation identifier | `github_authenticated_repositories` |
| remote input placement | closed protocol placement | REST query parameter |
| remote input name | validated query-field identifier | `visibility` |
| encoded remote value | validated fixed scalar encoding | `private` |
| accuracy | `EXACT` or `SUPERSET`, never implicit | accepted `SUPERSET`, activated only after RFC acceptance |

The boundary must preserve these distinctions. A fixed
`CompiledQueryParameter` continues to mean source identity present on every
scan; the conditional visibility binding uses a separate typed predicate-input
representation. A consumer must not append a field by parsing a snapshot,
matching the column or extractor name, inspecting `/user/repos`, or noticing
that a fixed query already contains `page` and `per_page`.

The accessor returns an immutable ordered collection. The first native version
has an empty collection on both existing non-repository relations and exactly
one entry on `authenticated_repositories` if the RFC accepts a safe mapping.
Production callers cannot default-construct, partially construct, assign, or
mutate a mapping. Connector-owned test access may construct invalid and decoy
values in a non-installable target.

Validation must reject at least:

- an unknown, nullable, or non-`VARCHAR` mapped column; an unsupported operator
  or operand; an unknown operation; an unsupported protocol placement; or an
  empty or unsafe encoded field;
- a native repository schema whose trailing `visibility` column is absent,
  renamed, nullable, not `VARCHAR`, not extracted exactly from
  `$.visibility`, duplicated, or placed before an existing column;
- a mapping whose typed literal and encoded value are not both exactly the
  lower-case string `private`, or whose remote field is not exactly
  `visibility`;
- duplicate mappings for the same predicate shape or multiple mappings that
  bind the same remote input ambiguously;
- a conditional `visibility` input that conflicts with a fixed `visibility` or
  `type` field, another conditional mapping that can emit either field, or
  pagination's page-size/page-number bindings; GitHub documents the legacy
  `type` field as invalid when `visibility` is also present;
- any mapping that changes the declared operation, base authenticated domain,
  headers, authority, credential placement, pagination declaration, or hard
  resource policy rather than only supplying the one selected typed input;
- an accuracy outside the accepted closed set or a mapping whose required
  evidence identity is absent from the accepted native profile; and
- any value that can carry SQL text, a DuckDB object, secret name, credential,
  mutable request, received Link value, page state, runtime counter, transport,
  or lifecycle object.

The safe relation snapshot renders predicate shape, operation, remote input,
and accepted accuracy as a distinct `predicate_mappings` section. It is
explanation, not serialization or execution authority. It must not render SQL
text, credential values, secret names, user data, received pagination state,
or an executable URL.

## Decision-critical GitHub semantic evidence

The accepted candidate is same-field mapping on `GET /user/repos`: DuckDB reads
the repository response's required `visibility` string and the remote request
uses GitHub's `visibility=private` restriction. The official endpoint and
pinned OpenAPI description for API version `2022-11-28` establish that the
endpoint returns the authenticated user's explicitly accessible repository
collection, exposes repository visibility, and limits that collection to the
specified visibility when the `visibility` query field is present. GitHub
documents public, private, and internal as distinct visibility categories.

For this closed shape, every base row satisfying
`visibility = 'private'` belongs to the specified private-visibility subset, so
`D => R`. An internal row has the distinct value `internal` and does not
satisfy `D`; it is not a required remote row. If GitHub returns an unexpected
extra row, the conservative `SUPERSET` classification and retained DuckDB
residual remove it. Missing, `NULL`, or malformed response visibility is a
strict extraction error, not predicate falsehood or evidence for accuracy.

The earlier broader-Boolean evidence remains material. Mapping
`private = TRUE` to `visibility=private` is unsafe because GitHub's private
Boolean is also true for internal repositories, so the remote visibility
restriction could omit DuckDB-true rows. Mapping that Boolean to legacy
`type=private` is also rejected because the pinned REST contract does not
explicitly guarantee inclusion of every internal row. Neither rejected mapping
may reappear as a fixture shortcut or inferred fallback.

Delivery must prove all of the following:

1. **Schema and extraction:** the installed repository relation has exactly the
   five existing required columns followed by required `VARCHAR visibility`
   with extractor `$.visibility`; public, private, and internal strings decode
   unchanged, while missing, `NULL`, non-string, and over-budget values fail.
2. **Safe narrowing:** for the same endpoint, API version, authenticated base
   domain, response field, and query-field domain, every row satisfying
   `visibility = 'private'` remains reachable under `visibility=private`
   (`D => R`).
3. **Conservative accuracy:** the declaration remains `SUPERSET` and DuckDB
   retains the complete predicate. Snapshots, examples, and fixtures make no
   residual-removal or `R => D` claim.
4. **Base-domain preservation:** the selected input changes only visibility
   membership within the existing explicit-access collection; operation,
   affiliation domain, credentials, origin, headers, duplicate behavior,
   pagination, and resource authority remain unchanged.
5. **Cross-field validation and decoys:** validation rejects absent or
   incompatible schema facts, wrong literal/value, wrong operation, duplicate
   `visibility`, conflicting legacy `type`, collision with page fields, and any
   name-, extractor-, snapshot-, or request-string inference. Fixtures include
   absent, exact, superset, broader-Boolean, wrong-extractor, wrong-type, and
   conflicting-input declarations.
6. **Pagination continuity:** Runtime proves `visibility=private` is
   reconstructed unchanged on every page and that Link targets cannot omit,
   alter, duplicate, or add fixed fields. Connector records only the immutable
   binding fact and does not choose the Link rule.
7. **Request reduction:** a deterministic public/private/internal multi-page
   service shows fewer accepted pages or requests than the unfiltered baseline
   while preserving the duplicate-aware row bag. Live execution is
   compatibility evidence only.

Primary evidence pins the official REST endpoint and OpenAPI description by
API version and source revision. The same-field contract closes the prior
decision-critical implication gap; deterministic fixtures prove the product's
schema, mapping, failure, and fallback behavior without treating live account
contents as the correctness oracle.

## Dependencies, sequencing, and parallel work

1. **RFC gate:** no production mapping, metadata-version activation, or
   consumer behavior lands until the required RFC is Accepted with product
   approval and all affected-team reviews.
2. **Evidence gate:** GitHub semantic and continuation evidence supports at
   least `D => R`; otherwise the goal uses the existing full-traversal fallback
   and the production mapping remains absent.
3. **Provider-shape gate:** Connector and Relational Semantics agree on the
   const mapping fields, accuracy vocabulary, typed input handoff, and fixture
   oracle. Runtime confirms that `ScanPlan` can carry the selected input without
   importing Connector types.
4. **Generic provider implementation:** Connector implements the closed value,
   schema/mapping validation, explanation, direct tests, and fixture service
   without activating the native relation mapping.
5. **Consumer implementation:** Semantics classifies only structured predicates
   against the provider API; Query and Runtime implement their respective ends
   of `ScanRequest -> ScanPlan -> BatchStream`.
6. **Native activation:** after provider and consumer oracles pass, the
   canonical builder publishes the required trailing `visibility` column, the
   accepted mapping, and coherent metadata identity in the same buildable
   integration checkpoint.

After the provider shape is frozen, these can proceed in parallel:

- Connector's generic value/validation/explanation module and decoy fixtures;
- Semantics' normalized predicate, implication, fallback, and plan snapshots;
- Query's pinned DuckDB structured-filter trial and adapter conversion; and
- Runtime's typed query-input application, pagination-continuity oracle, and
  request trace.

Writers use disjoint files or separate worktrees. Semantics and Query do not
include `catalog_test_access.hpp` or compile Connector production sources;
they link the Connector metadata and fixture services. Runtime does not include
`connector_catalog.hpp` at all. The lead agent owns shared RFC integration,
authoritative cross-contract propagation, root build/version/release records,
final dependency audit, Git history, and goal closure.

## Interaction exit

Current state: **Open; Collaboration**.

The learning objective is to prove that one explicit predicate mapping and a
bounded fixture service let Semantics classify and bind the supported
predicate without inferring source meaning from GitHub request construction,
and let Runtime execute only the resulting typed plan input.

The interaction becomes **Satisfied; X-as-a-Service** only when:

- the RFC is Accepted and the selected accuracy is backed by the implication
  and GitHub-domain evidence above;
- focused Connector declaration, invalid-value, decoy, snapshot, provenance,
  and credential-absence oracles pass;
- Semantics consumes only `PredicateMappings()` and public const accessors,
  owns all relational classification and residual decisions, and never parses
  Connector snapshots or fixed request fields;
- Query does not construct or reinterpret mapping internals, and Runtime sees
  only the complete immutable `ScanPlan` typed input;
- target/source inventories prove consumers link bounded provider services
  rather than compiling Connector production sources or importing private test
  constructors;
- accepted documentation and implementation agree across architecture,
  connector specification, runtime contracts, diagnostics, examples, and
  tests; and
- the controlled end-to-end row-bag, request-reduction, fallback, lifecycle,
  and complete verification gates pass.

The interaction remains Open if a consumer selects capability by relation,
column, extractor, path, credential, or fixed-query shape; re-declares
`visibility=private` or another remote-input constant; needs routine Connector
edits for predicate composition; imports private construction/explanation
internals; or if Runtime depends on `CompiledConnector` instead of `ScanPlan`.

## Documentation obligations

After RFC acceptance, the coherent delivery must update:

- `docs/ARCHITECTURE.md` for the accepted native filter-capability profile,
  implication relationship, offline planning, and conservative fallback;
- `docs/CONNECTOR_SPECIFICATIONS.md` for the native product metadata boundary,
  conditional predicate input, validation, explanation, and explicit
  non-activation of package/YAML syntax;
- `docs/RUNTIME_CONTRACTS.md` for the native `CompiledConnector` mapping,
  `ScanRequest`, `ScanPlan`, pagination continuity, and owner boundaries;
- adjacent C++ declarations and `src/connector/README.md` for purpose,
  ownership, inputs/outputs, immutability, lifetime, error ownership,
  compatibility, and prohibited state;
- accepted SQL/explain examples, fixture documentation, release notes, roadmap
  evidence, and this goal's delivery record when the product proof is complete.

Connector Experience supplies and reviews the exact Connector contract and
adjacent code documentation. The lead agent integrates shared authoritative
documents. No wording may imply general YAML predicate support, arbitrary REST
visibility values, a mapping from the broader `private` Boolean, public ABI
stability, or that a live GitHub observation is the correctness oracle.

## Verification

For this plan-only change:

```sh
ruby scripts/validate-agent-assets.rb
git diff --check
git diff --cached --check
```

The future implementation handoff requires, at minimum:

```sh
make build
make test
make demo
scripts/verify-source-identities.py
python3 -I -B scripts/test-native-dependencies.py
scripts/run-native-product-tests.sh /absolute/new/build-root debug
```

Run both focused Connector executables and any new predicate-focused target
before the integrated suite. Verification must also include compile-time
immutability/prohibited-member probes; generic and installed catalog
snapshots; exact/superset/absent decoys; locale stability; prohibited SQL,
secret, credential, Link, and runtime-state canaries; source-inventory and
consumer-boundary checks; the controlled mixed-visibility request/row oracle;
required `visibility` extraction success plus missing/`NULL`/wrong-type failure;
unsupported and ambiguous predicate fallback; offline explain/prepare; and
existing unfiltered relation regressions. Run the cached diff check only after
the intended implementation files are staged so new files participate in the
whitespace gate.
