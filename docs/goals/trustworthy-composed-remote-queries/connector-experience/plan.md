# Connector Experience plan: composable predicate proof declarations

## Outcome interpretation and charter authority

Status: **Delivered; provider interaction exited to X-as-a-Service**.

Connector Experience supplies the author- and maintainer-trust portion of the
active Query Experience goal: extend the private pre-`1.0` immutable native
catalog so Relational Semantics can distinguish validated predicate truth
evidence, base-domain identity, occurrence preservation, and executable
request encoding. The installed catalog keeps exactly the existing
`visibility = 'private'` to `visibility=private` `SUPERSET` mapping. A separate
Connector-owned controlled catalog supplies the one validated `EXACT` case
required by RFC 0010 without turning that fixture into public SQL behavior.

This authority follows `docs/teams/CONNECTOR_EXPERIENCE.md`: Connector owns
validated `CompiledConnector` facts, deterministic explanation, and provider
fixtures. It does not own DuckDB expression translation, implication or
three-valued equivalence, predicate composition, operation selection, residual
ownership, `ScanPlan`, request execution, or the user-visible result. RFC 0010
is Accepted and is the decision authority for this shared private interface;
the lead agent owns the implementation decomposition and coherent contract
integration, and the product manager retains the public-behavior decisions in
`AGENTS.md`.

### Topology routing

- Accountable team: Query Experience for the trustworthy composed DuckDB
  result.
- Supporting team: Connector Experience as the provider of validated immutable
  predicate capability facts and controlled catalog fixtures.
- Affected consumer: Relational Semantics, which alone interprets the facts and
  produces the semantic decision. Query consumes the resulting planning
  service; Remote Runtime consumes only the completed `ScanPlan`.
- Interaction: bounded Collaboration with Relational Semantics to freeze the
  declaration vocabulary and negative oracle, then X-as-a-Service through the
  public read-only catalog and Connector fixture service.
- Decision authority: RFC 0010 and the lead agent for this technical boundary;
  this plan neither changes the approved public mapping nor activates package
  syntax.

## Scope

### In scope

- Generalize the existing native `CompiledPredicateMapping` declaration enough
  to expose four independent closed facts: predicate proof identity, declared
  `EXACT` or `SUPERSET` accuracy, base-domain identity plus occurrence
  preservation, and operation-scoped encoding/composability capability.
- Preserve predicate shape, selected operation, typed conditional input,
  placement, encoded value, and accuracy as separate immutable values.
- Validate every combination against an accepted closed proof profile before a
  relation or catalog can be constructed; invalid facts are planning-contract
  defects, not ordinary mapping absence.
- Keep the installed GitHub catalog at one `SUPERSET` mapping and add no
  operation, column, relation, input value, network authority, or public SQL
  behavior.
- Add one Connector-owned non-installable controlled catalog with a distinct
  deterministic `EXACT` proof identity, a distinct controlled operation, the
  same declared base-domain identity on its base and restricted occurrences,
  and duplicate-sensitive exact occurrence preservation.
- Declare the current native encoding envelope explicitly: one positive typed
  REST query input can be emitted for the selected operation; no compound
  conjunction encoding, union/`OR` encoding, or complement/`NOT` encoding is
  declared.
- Extend safe deterministic snapshots, validation diagnostics, immutable-value
  probes, installed-catalog oracles, and bounded consumer fixtures for the new
  facts.
- Supply decoys that prove consumers cannot infer capability from relation,
  column, extractor, operation, URL/query shape, accuracy name, or fixture
  identity.

### Out of scope

- The Query-owned candidate algebra, DuckDB expression conversion, capability
  profile, expression retention, adapter lifecycle, SQL examples, or public
  explanation.
- Implication (`D => R`), three-valued exactness, Boolean composition,
  classification, operation selection/ranking, ambiguity disposition, fallback
  reasons, residual ownership, projection closure, ordering, limit, offset, or
  `ScanPlan` construction.
- HTTP request generation, conditional-input application, Link reconstruction,
  authorization, strict response conversion, budgets, cancellation, close, or
  Runtime admission.
- A second public predicate mapping; a new exact GitHub claim; relabeling the
  GitHub visibility mapping from `SUPERSET` to `EXACT`; remote projection,
  ordering, limit, or offset authority; or a protocol addition.
- Connector-package/YAML predicate syntax, general author tooling, loading,
  distribution, a public native ABI, arbitrary proof strings, arbitrary
  expression trees, or custom executable predicates.
- Encoding a compound Boolean expression merely because its logical
  approximation is safe. Connector declares encoding capability; Semantics
  decides whether the candidate can use it.

## Concrete production and test ownership

Connector work is confined to these responsibilities. Names for new files are
part of the ownership freeze; the implementer may merge a proposed new file
into its named existing responsibility only if the final dependency map remains
equally explicit.

| Artifact | Connector Experience ownership |
| --- | --- |
| `src/include/duckdb_api/connector_catalog.hpp` | Public pre-`1.0` Connector team API: closed proof/domain/occurrence/encoding values, immutable mapping accessors, lifetime and compatibility documentation |
| `src/include/duckdb_api/internal/connector/predicate_declaration.hpp` and `src/connector/predicate_declaration.cpp` | Generic closed-value validation, mapping-to-schema/input conflicts, operation-scoped encoding validation, and safe rendering; no relational classification |
| Proposed `src/include/duckdb_api/internal/connector/predicate_proof_profile.hpp` and `src/connector/predicate_proof_profile.cpp` | Closed accepted proof-profile registry and cross-field binding of proof identity, accuracy, operation, base domain, occurrence guarantee, and encoding mode; keeps upstream/controlled proof identities out of generic mechanics |
| `src/connector/catalog_model.cpp` | Invoke complete declaration validation before publication and compose safe catalog snapshots; no consumer inference or planner behavior |
| `src/connector/native_github_composition.cpp` | Sole installed declaration, unchanged public mapping count/value and authority, with the generalized GitHub `SUPERSET` proof fields |
| `src/include/duckdb_api/connector.hpp` and `src/connector/README.md` | Adjacent provider ownership, no-I/O, lifetime, consumer, fixture, and compatibility guidance |
| `src/connector/sources.cmake` and `src/connector/targets.cmake` | Connector production inventory and independently linkable metadata service |
| `test/cpp/connector/connector_predicate_contract_tests.cpp` | Generic immutability, closed values, encoding conflicts, safe explanation, and prohibited-state oracle |
| Proposed `test/cpp/connector/connector_predicate_proof_contract_tests.cpp` | Proof-profile cross-product: distinct exact identity, domain/occurrence binding, accuracy consistency, relabeling rejection, and encoding-composability rejection |
| `test/cpp/connector/connector_contract_tests.cpp` | Installed three-relation catalog, sole public superset mapping, canonical snapshot/provenance, locale, and absence of authority drift |
| `test/cpp/connector/support/catalog_test_access.hpp` | Connector-private invalid construction only; never linked or included by a consumer |
| `test/cpp/connector/support/connector_catalog_test_fixtures.*` | Bounded public test service returning validated exact, superset, absent, cross-domain, occurrence-changing, and encoding-decoy catalogs through public const catalog access |
| `test/cpp/connector/connector_catalog_test_fixtures_tests.cpp` | Provider-fixture identities, production-validation passage, duplicate-sensitive profile, determinism, credential absence, and no-inference oracle |
| `test/cpp/connector/sources.cmake` and `test/cpp/connector/targets.cmake` | Focused proof-oracle and fixture-service inventories; consumer targets link services rather than list Connector sources |

The following production and oracle ownership is expressly disjoint:

| Responsibility | Owner | Connector provides |
| --- | --- | --- |
| Candidate algebra and actual DuckDB structure | Query Experience | No DuckDB type or SQL text |
| Implication, three-valued equivalence, composition, ambiguity/failure decision, and residual laws | Relational Semantics | Validated read-only facts and controlled catalogs |
| Typed `ScanPlan`, semantic explanation facts, and law matrix | Relational Semantics | No plan construction or classification reason |
| Request encoding, page continuity, plan admission, resources, cancellation, and close | Remote Runtime | No `CompiledConnector` dependency; only Semantics' typed plan input |
| Composed SQL differential and user-visible explanation | Query Experience | Installed catalog through product composition, never Connector-private fixtures |

## Proposed bounded public provider API

The API is public only between repository teams. It remains a private pre-`1.0`
C++ service, not an installed ABI or connector-package promise.

`CompiledRelation::PredicateMappings() const` remains the sole mapping
collection. Each immutable `CompiledPredicateMapping` continues to expose its
column, closed operator and typed literal, operation, input placement/name,
encoded value, and declared accuracy, and additionally exposes closed values
equivalent to:

| Fact | Required representation and rule |
| --- | --- |
| Predicate proof identity | Closed enum with separate installed GitHub-superset and controlled-exact identities; never a caller string or accuracy alias |
| Base-domain identity | Closed stable identity naming the duplicate-preserving base occurrence bag to which both the base and restricted operation facts are bound |
| Occurrence preservation | Closed guarantee distinguishing preservation of every required matching base occurrence from exact preservation of selected occurrences and multiplicities |
| Encoding capability | Closed operation-scoped mode for one positive typed conditional input, including an explicit maximum of one selected mapping and explicit absence of compound `AND`, union/`OR`, and complement/`NOT` encodings |

The exact spelling may use small immutable value classes rather than raw enums,
but consumers must be able to compare every fact structurally without parsing a
snapshot. The mapping must not carry a predicate evaluator, SQL/DuckDB object,
secret, credential, mutable request, received URL/Link, page state, transport,
counter, callback, or lifecycle object.

Two accepted profiles are sufficient and no third profile is introduced:

1. The installed GitHub profile retains its reviewed visibility proof identity,
   authenticated-repository base-domain identity, all-required-occurrences
   guarantee, one-positive-input encoding, and `SUPERSET` accuracy.
2. The controlled profile uses a different proof identity and controlled
   operation/base-domain identity, declares exact selected-occurrence and
   multiplicity preservation, uses the same bounded one-positive-input
   encoding class, and alone may declare `EXACT`.

The controlled factory is exposed from
`connector_catalog_test_fixtures.hpp` with a stable fixture relation identifier
and returns a fully validated immutable `CompiledConnector`. It may use
Connector-private construction behind the fixture service, but it must traverse
the same production constructors and proof validation as the installed catalog.
No Semantics or Query test receives a mapping constructor or
`catalog_test_access.hpp`.

Snapshot output renders the proof, domain, occurrence, and encoding facts as
separate deterministic fields. It is explanation only and is never parsed by a
consumer. Existing credential, secret, raw SQL, URL-authority, Link-state, and
runtime-state exclusions continue to apply.

## Validation and acceptance evidence

### Distinct exact proof identity

- The GitHub proof identity plus `EXACT` is rejected; changing only the accuracy
  enum can never manufacture exactness.
- The controlled exact identity plus `SUPERSET`, the GitHub operation/domain,
  or the installed mapping's remote input is rejected as an incoherent profile.
- Unknown proof identities fail catalog construction. Empty mappings remain a
  valid explicit absence and are not converted into invalid-contract failures.
- The exact factory passes the production validator and public const accessors;
  no Semantics-private factory, mutation, snapshot parse, or validation bypass
  participates.

### Base-domain and occurrence preservation

- Validation binds mapping, selected operation, base-domain identity, response
  source, cardinality, and occurrence guarantee as one accepted profile.
- The controlled exact fixture contains duplicate rows/occurrences whose
  identity is stable enough for Semantics' bag oracle. Its exact declaration
  promises equality of selected occurrences and multiplicities, not merely
  equal distinct predicate values.
- Negative catalogs cover cross-domain restriction, changed operation,
  distinct/deduplicating selection, dropped duplicate occurrence, multiplied
  occurrence, wrong response source/cardinality, and a same-named operation on
  a different base domain. All fail production validation rather than falling
  back.
- The installed GitHub profile remains conservative: it proves only the
  accepted all-required-occurrences obligation and never acquires exact
  multiplicity or `R => D` wording.

### Encoding composability

- Validation proves that the mapping's typed input is encodable by the selected
  operation and does not collide with fixed query fields, pagination bindings,
  another conditional mapping, or a different operation.
- The positive controlled and installed profiles each admit exactly one
  selected conditional input. Decoys with two otherwise safe positive inputs,
  conflicting encodings, a changed placement/value, or a cross-operation input
  are rejected or explicitly declare no compatible compound encoding.
- The provider API explicitly reports no compound conjunction, union, or
  complement encoding. Semantics' `AND` may choose at most one unambiguous safe
  leaf; `OR`, `NOT`, and incompatible multi-candidate `AND` cannot gain request
  authority from logical safety alone.
- Snapshot/name/request-shape decoys prove a consumer cannot infer encoding
  support from `visibility`, `/user/repos`, query fields, proof names, or
  accuracy.

### Provider quality

- Compile-time probes preserve copy construction while forbidding default,
  partial, assignment, and public construction of the mapping and proof values.
- Installed and controlled snapshots are deterministic under locale changes,
  contain the new facts once, and contain no SQL, DuckDB object, secret name,
  credential value, executable URL, received Link, or runtime state.
- Native relation count, schema, operation, authentication, pagination,
  resources, network policy, metadata version, and sole public mapping remain
  unchanged unless a coherent lead-owned version record requires only snapshot
  identity propagation.

Connector's tests prove declaration integrity, not `D => R`, `R => D`, Boolean
laws, or DuckDB result equivalence. Those assertions must appear in the
Semantics and Query oracles against these fixtures.

## Dependencies, sequencing, and parallel-safe boundaries

1. **Accepted decision:** RFC 0010 and the active root goal are the authority;
   no further RFC gate blocks this provider work.
2. **Interface freeze:** Connector and Relational Semantics agree on the four
   independent facts and controlled fixture identifiers. Runtime confirms that
   none of them crosses the `ScanPlan` boundary.
3. **Generic provider:** Connector implements closed values, proof-profile and
   encoding validation, safe rendering, and direct negative tests without
   changing the native mapping.
4. **Controlled exact service:** Connector adds the validated exact catalog and
   domain/occurrence/encoding decoys; its provider tests pass independently of
   Query, Semantics, and Runtime.
5. **Native propagation:** the canonical GitHub builder publishes the same
   public `SUPERSET` behavior with explicit generalized facts and coherent
   snapshots.
6. **Consumer adoption:** Semantics consumes only the public const mapping API
   and fixture service. Query and Runtime receive no Connector proof type.

After step 2, the following work is parallel-safe with disjoint writers:

- Connector owns only the production and test artifacts listed above;
- Relational Semantics owns the candidate decision API, law matrix, plan facts,
  and Semantics tests;
- Query Experience owns DuckDB translation, adapter tests, explanation, and
  product differential;
- Remote Runtime owns plan admission and execution/lifecycle tests; and
- the lead agent owns shared authoritative contracts, root build/product
  composition, version/release records, final dependency audit, Git history,
  and goal completion.

No consumer compiles Connector production sources, includes
`duckdb_api/internal/connector/*`, or imports `catalog_test_access.hpp`.
Connector does not edit `relational_predicate.hpp`, `scan_request.hpp`,
`scan_plan.hpp`, Semantics/Query/Runtime source or tests, another workstream
plan, or the root goal. Any integration target that genuinely composes the full
product is lead-owned and explicitly named as integration rather than a focused
consumer target.

## Documentation obligations

Connector Experience supplies and reviews:

- the native-metadata section of `docs/CONNECTOR_SPECIFICATIONS.md`, preserving
  inactive package/YAML syntax while documenting proof, base-domain,
  occurrence, and encoding facts;
- the `CompiledConnector` portions of `docs/RUNTIME_CONTRACTS.md` and the native
  provider description in `docs/ARCHITECTURE.md` for lead-agent integration;
- adjacent declarations and `src/connector/README.md` for purpose, ownership,
  inputs/outputs, immutability, lifetime, validation/error ownership,
  compatibility, prohibited state, fixture use, and consumer boundaries;
- safe snapshot examples, focused test descriptions, changelog/release-note
  input, and the final goal completion evidence.

Durable wording must not claim new author syntax, a stable public ABI, generic
Boolean request encoding, another public mapping, exact GitHub semantics,
residual transfer, remote bounds, or that the controlled exact fixture is an
installed relation. The lead agent integrates the shared contract documents so
all three layers and examples agree in one coherent change.

## Verification

For this plan-only artifact:

```sh
ruby scripts/validate-agent-assets.rb
git diff --check
git diff --cached --check
```

Future Connector implementation runs the focused declaration/proof executable
and fixture-service executable first, then the repository gates:

```sh
make build
make test
make demo
scripts/verify-source-identities.py
python3 -I -B scripts/test-native-dependencies.py
scripts/run-native-product-tests.sh /absolute/new/build-root debug
```

Verification evidence must include source-inventory and consumer-boundary
checks; installed and controlled snapshots; exact/superset/absent and all
domain/occurrence/encoding decoys; immutable/prohibited-member probes; locale
stability; credential and execution-state canaries; production-validation
passage for the exact fixture; rejection of relabeled exact and
multiplicity-changing profiles; unchanged installed SQL/catalog behavior; and
the downstream Semantics law matrix and Query product differential. The cached
whitespace check runs after staging so every new file participates.

## Observable interaction exit

Current state: **Satisfied; X-as-a-Service**.

The learning objective is to prove that Semantics can decide exact, superset,
unsupported, ambiguous, and invalid composition using explicit validated
Connector facts, including a controlled exact bag, without re-creating an
upstream proof profile or request encoder.

The interaction exits only when all of the following are observable in the
final tree and tests:

- focused Connector proof, negative, fixture, snapshot, provenance,
  immutability, and prohibited-state oracles pass independently;
- the installed catalog still exposes exactly the one public GitHub
  `SUPERSET` mapping, while the distinct `EXACT` identity is reachable only
  through the non-installable Connector fixture service;
- Semantics includes only `duckdb_api/connector_catalog.hpp` and the public
  fixture header, consumes every proof/domain/occurrence/encoding fact through
  const accessors, and owns all implication, composition, classification,
  ambiguity, residual, and reason decisions;
- no consumer parses Connector snapshots, matches native names/paths/query
  fields, relabels accuracy, or constructs/retains/reinterprets proof internals;
- Query does not classify mapping facts, and Runtime has no Connector target or
  header dependency and sees only the completed typed `ScanPlan`;
- target/source inventories show focused consumers linking
  `duckdb_api_connector_metadata_service` or
  `duckdb_api_connector_fixture_service`, never compiling Connector production
  sources or importing private test construction;
- accepted documentation agrees across architecture, native connector notes,
  runtime contracts, adjacent code, diagnostics, examples, and tests; and
- the production Semantics law oracle, actual-DuckDB differential, request
  shape, zero-I/O invalid case, lifecycle matrix, and complete verification
  gates pass.

The interaction reopens if a new proof or encoding needs bespoke Semantics
knowledge of Connector construction; a consumer infers capability from names,
snapshots, URLs, or accuracy alone; exactness can be claimed without a distinct
validated proof/domain/occurrence profile; a focused target compiles provider
sources; Query or Runtime imports proof facts; or Connector begins deciding
relational composition or plan ownership.
