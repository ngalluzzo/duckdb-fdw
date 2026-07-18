# Connector Experience plan: authenticated native relation catalog

## Outcome and boundary

Status: **Planned; Collaboration open**.

Provide RFC 0006's permanent native product with one deterministic, immutable
`CompiledConnector` catalog containing the existing anonymous GitHub relation
and the new `authenticated_user` relation. The catalog declares the new
relation's exactly-one-on-success source and one required logical bearer
credential bound to exact HTTPS host and header-placement policy, without
containing a DuckDB secret name or credential value.

This is a supporting Connector Experience workstream for the Query Experience
outcome. It owns compiled metadata, its provider interface and documentation,
and direct deterministic oracles. It does not own SQL arguments, DuckDB secret
registration or lookup, relational interpretation, runtime capabilities,
bearer decoration or enforcement, lifecycle, or the end-to-end demonstration.

The work extends only the repository-owned native metadata boundary. It does
not parse or validate YAML, load packages, expose author tooling, establish
package compatibility, or create a public native ABI.

## Owned permanent artifacts

| Artifact | Connector Experience responsibility |
| --- | --- |
| `src/include/duckdb_api/connector.hpp` | Immutable catalog and relation API; typed cardinality and logical credential/bearer binding; exact lookup; provenance, lifetime, credential-absence, and compatibility documentation |
| `src/connector.cpp` | Sole production construction of the two canonical `native_product_metadata` relations, Connector-owned invariant validation, and deterministic safe explanation |
| `test/cpp/connector_contract_tests.cpp` | Direct catalog, policy, provenance, immutability, explanation, determinism, and credential/name-absence oracles independent of DuckDB, planning, runtime, YAML, filesystem, environment, and network |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Propagate the native two-relation metadata boundary while keeping YAML compilation, package loading, environment sources, author tooling, and distribution inactive |

No new Connector production module is planned. This workstream does not own or
edit `ScanRequest`, `ScanPlan`, product composition, the DuckDB adapter,
executor or transport types, controlled-service machinery, architecture or
runtime contracts, examples, or build configuration.

## Provider contract

### Immutable catalog

The current `0.3.0` singleton fields become relation-owned data beneath an
immutable `CompiledConnector` catalog:

- catalog origin `NATIVE_PRODUCT_METADATA`, connector `github`, metadata
  version `0.4.0`, and two relations in stable order:
  `duckdb_login_search_page`, then `authenticated_user`;
- exact-name lookup returns a const relation or explicit absence without SQL
  diagnostics, fallback selection, or I/O;
- each `CompiledRelation` owns its stable name, columns, operation, response
  source, credential/auth policy, and record/string ceilings; and
- whole-catalog and per-relation explanations are deterministic and safe.

Catalog and relation state is private behind const accessors with no public
mutators or partial aggregate initialization. The no-argument native builder
is the sole production construction point and performs no DuckDB call, secret
lookup, environment or filesystem access, package parsing, runtime
construction, or network work. Retained or copied snapshots cannot change
another consumer's observed relation or policy.

Connector-wide provenance, identity, version, and shared network narrowing
remain catalog facts. Moving relation fields beneath `CompiledRelation`
prevents consumers from combining one relation's schema with another
relation's operation or auth policy.

### Typed credential and auth policy

Use a closed relation-scoped value with only two constructible states:

- anonymous: no logical credential, authenticator, or credential placement;
- required bearer: exactly one logical `token`, bearer authenticator, exact
  destination `https://api.github.com:443`, and exact `Authorization` header
  placement.

Do not model these as independent booleans and arbitrary strings that permit
contradictory states. Authentication-enabled state is derived from the closed
policy. Host authority uses the existing typed scheme, validated host, and
explicit port; placement uses a closed header-placement type.

Logical `token` identifies what the relation requires. It is not a DuckDB
secret name and grants no value access. Connector data contains no secret
type/provider object, SQL name such as `github_default`, token bytes, secret
handle, environment key, filesystem path, auth-header value, or general
credential reader. `Authorization` appears only as permitted placement, never
as a valued fixed request header.

### Relation inventory

`duckdb_login_search_page` preserves its required `id BIGINT`, `login VARCHAR`,
and `site_admin BOOLEAN` columns and extractors; fallback replay-safe REST
`GET`; exact `https://api.github.com:443/search/users`; ordered fixed
`q=duckdb+in%3Alogin` and `per_page=3`; accepted non-sensitive headers;
`$.items[*]`; `ZERO_TO_MANY`; anonymous policy; existing network narrowing;
three-record and extraction ceilings; and disabled auth, pagination, retry,
redirect, proxy, provider, and cache capabilities. Its version-bearing
`User-Agent` advances to `duckdb-api/0.4.0`; schema, complete zero-to-three base
domain, and authority remain unchanged.

`authenticated_user` declares:

- required `id BIGINT <- $.id`, `login VARCHAR <- $.login`, and
  `site_admin BOOLEAN <- $.site_admin`, in that order;
- one fallback replay-safe REST `GET` over exact
  `https://api.github.com:443/user`, no query fields, and only the accepted
  non-sensitive `Accept`, `User-Agent: duckdb-api/0.4.0`, and
  `X-GitHub-Api-Version: 2022-11-28` fixed headers;
- a typed root-object response source, `EXACTLY_ONE_ON_SUCCESS`, and a one-row
  ceiling; auth, policy, HTTP, decode, or schema failure is never zero rows;
- the required-bearer policy above; and
- no pagination, retry, redirect, proxy, provider, cache, caller URL, alternate
  host/header, or fallback to the anonymous relation.

### Provenance, explanation, and documentation

Both relations inherit the catalog's `NATIVE_PRODUCT_METADATA` provenance.
Explanations include stable identity, schema, source cardinality, structural
request, non-sensitive headers, logical credential requirement, authenticator,
permitted destination and placement, network narrowing, and ceilings. They are
not serialization, authority, package syntax, or a source of secret values.

No explanation or metadata carries fixture-response identity, package root,
YAML location, package digest, caller path, DuckDB secret name, token, bearer
value, or runtime capability. Code comments beside the declarations document
Connector ownership, consumer expectations, stable identifiers and ordering,
schema/nullability and extractor meaning, source provenance, immutable
lifetime, logical-policy versus execution-authority separation, and the lack
of public ABI or package-authoring compatibility.

## Consumers and delivery order

| Consumer | Required dependency direction |
| --- | --- |
| Query Experience | Receives the catalog through composition, performs exact lookup, reads schema and credential-required/forbidden state, and supplies the SQL secret name separately; it does not construct, mutate, or resolve Connector policy |
| Relational Semantics | Consumes the selected relation and maps typed cardinality, request, credential requirement, and binding into its offline plan; it owns conservative validation and relational meaning, with no secret lookup |
| Remote Runtime | Has no production dependency on `connector.hpp`; it receives only the Semantics plan and Query execution capability, never Connector policy internals |
| Private controlled tests | Keep any loopback construction in a separate non-installable test path; no loopback host, token, mutable policy factory, or authority selector enters the installed builder or artifact |

Delivery order is: preserve the anonymous oracle while reshaping the provider
as a catalog; add the closed auth policy, exactly-one source, second relation,
and safe explanations; let Query and Semantics migrate to lookup/accessors;
then propagate the native metadata boundary and audit the final dependency
graph. Consumer-owned files are not authorized edits on this branch.

## Acceptance evidence

- Direct tests assert catalog origin, identity, version, relation count/order,
  exact and unknown lookup, every relation field and exclusion, and identical
  construction and explanation across independent builds, copies, and a
  changed process locale.
- Anonymous regression tests preserve every `0.3.0` source field except the
  accepted version-bearing header; authenticated tests prove root-object
  source, exactly-one cardinality, `/user`, empty query, fixed headers, logical
  `token`, bearer, exact HTTPS host/port, exact placement, and one-row ceiling.
- Constructor/type counterexamples reject contradictory auth states, alternate
  hosts or ports, arbitrary placement, credentials on the anonymous relation,
  duplicate relation names, and a static `Authorization` header value.
- Structural and string canaries prove the builder accepts no credential or
  secret-name input; public metadata has no credential-value, DuckDB-secret,
  secret-handle, or runtime-capability member; and snapshots contain neither a
  synthetic credential sentinel nor `github_default`.
- Provenance tests require `native_product_metadata` and reject fixture,
  response-content, package-root, YAML-path, package-digest, and
  caller-selected-source provenance.
- The focused Connector target remains standard-library-only apart from test
  assertions. Final inspection proves Query and Semantics consume only the
  public const API without duplicated policy constants, and Runtime has no
  Connector production dependency.

## Collaboration to X-as-a-Service exit

Current status: **Open; Collaboration**.

Learning objective: prove that exact relation lookup plus the typed relation
and credential-policy API lets Query and Semantics consume the catalog without
singleton assumptions, duplicated constants, implementation includes, or
auth-policy reconstruction.

The interaction becomes **Satisfied; X-as-a-Service** only when:

- both direct metadata/explanation oracles pass, including credential/name
  absence and native-provenance counterexamples;
- Query and Semantics consume const lookup/accessors without constructing,
  mutating, or reinterpreting policy internals, while Runtime remains behind
  `ScanPlan`;
- final declarations, includes, construction/composition points, tests, and
  adjacent documentation prove those dependency directions;
- `docs/CONNECTOR_SPECIFICATIONS.md` records the two-relation native boundary
  without activating YAML or packages; and
- focused and integrated product tests, source-identity and artifact canaries,
  `ruby scripts/validate-agent-assets.rb`, `git diff --check`, staged
  `git diff --cached --check`, and the applicable fresh native gate pass.

Compatibility is limited to stable `0.4.0` snapshot values and ordering for the
snapshot lifetime; it is not a public ABI or authoring promise. The exit remains
Open if any consumer infers auth from a name or boolean, needs constructor
details, mutates a relation for integration, duplicates bearer/host/header
policy, or requires routine coordinated edits outside this provider contract.

## Explicit exclusions

No YAML fields, schemas, parsers, package provenance, author diagnostics,
environment/filesystem or persistent secret providers, OAuth, arbitrary auth,
hosts or headers, package loading/reload/distribution, public ABI, general
relation registry, SQL secret argument, DuckDB secret names, secret lookup,
bearer construction, transport enforcement, redaction, cancellation, or cleanup
belongs to this workstream.
