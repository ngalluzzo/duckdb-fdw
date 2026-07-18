# Engineering Enablement plan: reproducible authenticated-relation delivery

## Facilitation objective and routing

Status: **Active; Facilitation open**.

Make RFC 0006's permanent provider modules, focused oracles, controlled product
oracle, and `0.4.0` public artifact reproducibly buildable and difficult to omit
from the cached or fresh gates. Query Experience remains accountable for the
user outcome and public contract. Connector Experience, Relational Semantics,
Remote Runtime, and Query Experience retain the quality and documentation of
their own source and tests.

Engineering Enablement is activated because this goal adds production and test
translation units, changes the controlled-artifact input graph, advances the
current source and release identities, and expands the exact test inventory.
The lead agent retains integration authority. Enablement owns only the shared
mechanics that make those team handoffs repeatable.

## Owned delivery mechanics

| Shared artifact | Enablement responsibility |
| --- | --- |
| `CMakeLists.txt` | Register every handed-off production, test, and private-support translation unit under its responsibility-specific source group; preserve focused targets and exact linkage classes; keep the installed and controlled compositions explicit |
| `extension_config.cmake` and root build version references | Advance the current extension and private controlled artifact to `0.4.0` without changing the pinned DuckDB 1.5.4/macOS arm64 compatibility cell |
| `release/0.4.0/pins.json` | Record the current project identity, unchanged verified dependencies and toolchain inputs, the public-contract digest, and a path-bound identity for the complete permanent native product input set |
| Current-version source/dependency verifiers and their forward tests | Select and validate `0.4.0`, preserve historical `0.1.0` through `0.3.0` records, and fail closed for omitted, substituted, extra, linked, or version-inconsistent inputs |
| Native developer and fresh-product runners | Execute the complete focused, controlled, SQL, direct-load, dependency, source-identity, and artifact inventories in cached and new build roots |
| Installed-artifact inventory | Preserve the sole public init symbol, exact system-libcurl linkage, and exclusion of controlled authority, private test seams, loopback identities, and synthetic credential fixtures from installed outputs |

The current single-file `src/connector.cpp` identity is no longer sufficient
once responsibility-owned production code spans multiple files. The `0.4.0`
identity must bind normalized repository-relative paths and bytes for every
production input to both public and controlled artifacts, plus the public
contract. It must not make test files, generated build output, absolute
worktree paths, or a live credential part of the release identity.

## Provider handoff contract

Each provider supplies a final, reviewed manifest of its permanent translation
units, public headers, focused test entry points, private test support, required
compile definitions, link dependencies, and installed/private classification.
Enablement wires that manifest; it does not infer domain ownership from include
accidents or copy provider implementation into build scripts.

| Provider | Exact handoff expected | Build classification and provider-owned proof |
| --- | --- | --- |
| Connector Experience | `connector.cpp`, `connector_catalog.cpp`, their two public headers, `connector_contract_tests.cpp`, `connector_catalog_contract_tests.cpp`, and Connector-only test support | Production and focused tests remain DuckDB- and curl-free; Connector owns the two-relation catalog, invariant, provenance, and credential-absence assertions |
| Query Experience | `scan_request.cpp`; the dedicated DuckDB secret module; adapter, extension, and product-composition units; secret/request/adapter tests; controlled composition and entry units; SQL and split authenticated product suites; source demo; and final `release/0.4.0/public_contract.json` | Secret/adapter targets link only the pinned DuckDB and declared provider interfaces; controlled-only sources never enter an install rule; Query owns SQL, secret lifecycle, prepared rotation/drop, user diagnostics, demo, and public-contract meaning |
| Relational Semantics | `scan_plan.cpp`, `scan_planner.cpp`, their public headers, both focused planner/plan tests, and Semantics-local request support | All targets remain DuckDB-, Secret Manager-, transport-, and curl-free; Semantics owns relation selection, cardinality, ownership, policy mapping, and offline-plan assertions |
| Remote Runtime | `authorization.cpp`, `execution_error.cpp`, the fixed authenticator and executor/transport/decoder units, public Runtime headers, focused authorization/execution/runtime tests, and explicitly private curl/socket support | Capability/interface tests remain curl-free; only transport-bearing product and focused targets link the pinned system libcurl; Runtime owns bearer, host/placement, redaction, concurrency, cancellation, and cleanup assertions |

If a provider changes that manifest, its handoff names the reason and the
affected target. Undeclared source discovery, broad globs, and a single
catch-all test executable are rejected because they can hide both missing
production code and cross-team coupling.

## Build and artifact integration

- Keep Connector metadata, Query request/adapter, Semantics planning, Runtime
  interface, Runtime transport, Query composition, and private controlled
  support as separate CMake source groups. A target consumes the smallest
  complete groups its oracle actually exercises.
- Add focused targets for the new Connector catalog, Query secret integration,
  and Runtime authorization contract instead of folding their entry points into
  existing monolithic suites. Existing responsibility-specific targets remain
  independently runnable.
- Build the public artifact from all permanent provider units and the installed
  Query composition/entry only. Build the controlled artifact from the same
  permanent product path, replacing only the explicitly private composition
  and entry and adding declared controlled support.
- Keep the controlled artifact under the private output directory, absent from
  DuckDB's install repository and every install rule. Keep test-only compile
  definitions, private symbols, authority selectors, socket observers, and
  loopback services out of public and static product artifacts.
- Maintain an explicit target-to-linkage inventory: curl-free focused targets
  must reject libcurl; transport-bearing targets must name exactly the pinned
  system libcurl. New DuckDB-secret tests may link pinned DuckDB but do not gain
  transport authority merely because the product artifact does.
- Extend the installed-artifact canary with the private identifiers introduced
  by the final handoffs. Legitimate public policy strings such as
  `Authorization` are not treated as credential leakage; runtime-generated
  token sentinels remain the owning teams' secure oracles and are never printed
  by an Enablement failure message.

## Version, identity, and gate workstreams

The following work can proceed in parallel with provider implementation:

- prepare a `0.4.0` release-record shape that preserves the exact external
  dependency and product-cell pins;
- make current-version selection in the source, native-dependency, developer,
  and fresh-build gates advance coherently rather than leaving hidden `0.3.0`
  paths;
- define the path-bound multi-source identity and negative mutation tests; and
- reserve responsibility-specific target and artifact classifications from the
  provider handoff contract.

Final source paths and digests, target inventories, private-symbol canaries,
and the public-contract digest wait for the reviewed provider integration.
Enablement never invents those values early to make a partial branch green.

Deterministic forward tests must prove at least:

- `extension_config.cmake`, release pins, public contract, native dependency
  verifier, controlled artifact, and compiled metadata all agree on `0.4.0`;
- historical release records remain byte/canonical-identity stable;
- removing, adding, renaming, or mutating one declared production input fails
  source identity, while a worktree path change does not;
- every handed-off focused target is built, run, and checked against its exact
  linkage class in cached and fresh workflows;
- the public artifact exports one init symbol and contains no controlled entry,
  private test seam, loopback authority, or committed synthetic credential;
- the controlled artifact is built and exercised but cannot enter the install
  repository; and
- cached `make build`, `make test`, and `make demo` and one fresh `make verify
  PROFILE=debug` reach the same permanent product modules. The operator-supplied
  GitHub compatibility check remains opt-in Query evidence and never becomes a
  credentialed CI gate.

Completion also runs `ruby scripts/validate-agent-assets.rb`,
`scripts/verify-source-identities.py`,
`python3 -I -B scripts/test-native-dependencies.py`, the relevant build-workflow
forward tests, `git diff --check`, staged `git diff --cached --check`, and the
fresh native product runner from a new build root.

## Code documentation expectations

- CMake comments explain responsibility grouping, installed versus private
  composition, and any non-obvious linkage or registration order; they do not
  restate source lists.
- Gate code documents current-versus-historical identity authority, canonical
  path/digest rules, provenance assumptions, sensitive-output handling, and why
  a fail-closed check exists.
- Failure messages identify the target, path, linkage class, or inventory
  surface without echoing token values, captured headers, response bodies,
  personal rows, or unrestricted dependency diagnostics.
- Provider APIs, lifecycle, relational meaning, credential policy, and domain
  algorithms remain documented beside provider-owned code. Enablement does not
  duplicate or approve that rationale in build files.

## Non-responsibilities

Engineering Enablement does not choose public SQL, secret semantics, relation
schema, cardinality, auth policy, bearer construction, network behavior,
relational ownership, diagnostics, or acceptance of the product outcome. It
does not write provider domain tests, perform the opt-in live query on a user's
behalf, publish or tag a release, broaden the compatibility cell, weaken an
oracle to fit the build, or become the permanent reviewer for another team.

## Facilitation exit and handback

The interaction is **Satisfied** when the integrated graph proves that every
provider handoff is present in the correct independently runnable target, the
public and controlled artifacts differ only at declared private composition
seams, all version/source/linkage/inventory checks pass in cached and fresh
workflows, and each receiving team can run and diagnose its focused target plus
the documented product gate without Enablement intervention.

At exit, providers maintain their source manifests, domain oracles, and code
documentation; Query maintains the public contract and user evidence;
Enablement maintains only the reusable build, identity, and inventory
mechanisms. A later provider source addition should require a bounded manifest
update, not renewed cross-team design or an Enablement approval queue. Domain
quality and product acceptance explicitly return to their charter owners.
