# Engineering Enablement plan: constrained native HTTPS build and gates

## Outcome and status

Transfer the build, dependency-identity, controlled-test, artifact-inventory,
source-identity, and fresh-product practices required to deliver RFC 0005's
single supported `0.3.0` native cell. Query Experience remains accountable for
the product outcome. Remote Runtime owns libcurl lifecycle and network behavior;
Enablement owns only the reusable build and evidence service that proves the
declared dependency reached the intended targets and no others.

This workstream begins with this plan only. Root CMake composition and gate
implementation wait for the final committed Connector, Relational Semantics,
Remote Runtime, and Query source inventories. Enablement will not guess target
membership from the experiment or introduce compatibility shims around an
unfinished provider interface.

The branch is `goal/0.3-live-rest/enablement` in the isolated
`.worktrees/first-live-rest/enablement` worktree. Provider commits are consumed
unchanged in the order selected by the lead agent; this workstream does not
rewrite their history or edit their owned source.

## Owned build and evidence files

| Artifact | Engineering Enablement responsibility |
| --- | --- |
| `CMakeLists.txt` | Compose the final provider and Query source inventories into responsibility-named target groups; discover the constrained SDK libcurl; link only transport-bearing production, controlled, focused-test, and identity-probe targets; build but never install the private controlled extension |
| `Makefile` and `scripts/native-dev.sh` | Preserve `build`, `test`, `demo`, `paths`, and `verify` as the stable receiving-team service and keep their failure guidance current |
| `extension_config.cmake` | Advance the permanent `duckdb_api` extension identity to `0.3.0` without adding another installed extension |
| `scripts/lib/native-dev-environment.sh` | Select the current `0.3.0` native pins, derive the SDK through `xcrun`, verify the host/toolchain/SDK dependency cell before configuration, and retain the reusable cell lock and custody rules |
| `scripts/lib/native-dev-build.sh` | Pass only verified SDK/libcurl inputs into the pinned template build; run the final responsibility-matched native, SQL, controlled, artifact, and public-demo commands supplied by their owners |
| `scripts/run-native-product-tests.sh` | Reproduce the same dependency and product contract in a new build root with no reusable developer state and emit exact artifact and dependency evidence |
| `scripts/write-observed-dependencies.py` | Record current native source/build dependencies from an explicit current pins file without changing historical `0.1.0` evidence semantics |
| `scripts/verify-native-dependencies.py` | Verify the selected SDK/libcurl input, configured target, built artifact, and runtime probe identities against `0.3.0` pins before accepting the cell |
| `scripts/test-native-dependencies.py` | Exercise deterministic fake-cell drift and malformed-record counterexamples without relying on the workstation SDK as the test oracle |
| `test/cpp/native_dependency_identity.cpp` | Report the runtime libcurl version, SSL-backend string, and feature bits from the same `CURL::libcurl` target used by transport-bearing product targets |
| `scripts/verify-source-identities.py` | Validate the `0.3.0` project/version record and the final Connector snapshot, controlled fixture, and Query-owned public-contract identities after their exact paths are committed |
| `scripts/verify-loadable-inventory.sh` | Preserve the sole public init symbol and reject wrong curl linkage, loopback/test authority, fault selectors, extra entry points, and test-only controls in the installed artifact |
| `scripts/test-native-dev.sh` | Cover current-pin selection, command routing, dependency-verifier invocation, controlled/public artifact separation, and actionable drift failures |
| `release/0.3.0/pins.json` | Declare the exact current project, DuckDB/template/CI-tools/tool, product-cell, SDK/libcurl, source, and public-contract identities |
| `AGENTS.md` | Keep the authoritative current-development and fresh-product command descriptions synchronized if their evidence or invocation changes |
| `docs/goals/first-live-rest-relation/engineering-enablement/plan.md` | Record this bounded facilitation, ownership, evidence, and exit contract |

The exact source lists inside `CMakeLists.txt`, the controlled artifact target,
and the source-identity input paths are integration facts, not decisions for
this planning commit. They are filled from the final provider and Query
commits. No file under `src/`, provider-owned `test/cpp` or `test/python`
support, examples, `release/0.3.0/public_contract.json`, or shared architecture,
connector, and runtime contracts is owned here.

## Receiving teams and dependency direction

- **Remote Runtime — receiving owner of the platform dependency practice:**
  supplies the final transport-bearing source/test targets and owns checked
  process-lifetime libcurl initialization, feature verification, cleanup,
  transport, policy, decode, and lifecycle oracles. Enablement links and
  verifies those targets without reimplementing runtime meaning.
- **Query Experience — receiving owner of the product-artifact practice:**
  supplies the installed and private controlled entry points, product
  composition, direct-load oracles, public contract, examples, and the exact
  artifact exclusion vocabulary. Enablement builds and inventories them but
  does not own SQL, adapter behavior, authority selection, or expected rows.
- **Connector Experience — provider of pinned source identity:** supplies the
  canonical immutable native metadata and direct snapshot oracle. Enablement
  records its committed identity without duplicating connector fields.
- **Relational Semantics — provider of focused target inventory:** supplies the
  immutable planner sources and tests. They remain curl-free; Enablement's
  build graph makes an accidental curl dependency visible and failing.
- **Lead agent — integration owner:** selects the final provider commits,
  resolves cross-workstream inventory changes, propagates shared contracts,
  runs final adversarial review, and owns the coherent delivery history.

Dependency direction remains Connector metadata to Relational planning to
Remote execution to Query's DuckDB edge. Build composition names those
responsibilities but never becomes an alternate provider API. Domain test
failures return to the owning team; Enablement does not weaken or rewrite an
oracle to obtain a green aggregate gate.

## Constrained macOS libcurl cell

`release/0.3.0/pins.json` instantiates the RFC-selected native cell rather than
changing the historical `0.1.0` or `0.2.0` release records. It carries:

- macOS 26.5.1 build `25F80`, `osx_arm64`, Apple clang 17.0.0, C++11, pinned
  CMake and Ninja identities, DuckDB 1.5.4, the extension template commit and
  tree, and its extension-ci-tools commit and tree;
- Command Line Tools SDK 15.5 derived only through `xcrun`, the expected
  relative curl include directory and `usr/lib/libcurl.4.tbd`, a canonical
  relative-name-and-content digest of every accepted curl header, the stub
  digest, and the stub/install-name relationship;
- configured libcurl version 8.7.1 through
  `find_package(CURL 8.7.1 EXACT REQUIRED)` and imported target
  `CURL::libcurl`, with include and library paths confined to the verified SDK;
- artifact install name `/usr/lib/libcurl.4.dylib`; and
- runtime libcurl 8.7.1, the observed SSL-backend string, and the required
  `CURL_VERSION_THREADSAFE` feature bit.

Absolute developer paths are observations, never pins. The header-tree digest
uses sorted paths relative to the pinned SDK include root plus file bytes, so
two equivalent SDK locations produce the same identity. The verifier follows
the accepted stub link only within the SDK, rejects missing, extra, or changed
headers and path escapes, and rejects Homebrew, pkg-config, environment, or
other ambient curl selection. The OS trust store and platform TLS
implementation remain declared cell inputs; no claim is made that their bytes
are redistributed or independently frozen.

The current native build scripts may gain a current-product pin reader, but the
historical `release_pin` service and all `0.1.0` release and sanitizer commands
remain bound to `release/0.1.0/pins.json`. The native developer and fresh
product paths do not add vcpkg, FetchContent, vendored curl/TLS sources, or a
second dependency manager.

## Build composition and artifact custody

Once final inventories are available, `CMakeLists.txt` will:

1. keep Connector, planning, Remote Runtime, Query composition/adapter, test
   support, and private controlled entry points in distinct source groups;
2. link `CURL::libcurl` only to targets that contain the production curl
   transport, the Runtime-owned curl tests, the private controlled artifact
   when it exercises that transport, and the native dependency identity probe;
3. prove Connector/planner/adapter-fake-only targets remain curl-free;
4. build one installed `duckdb_api` artifact from only the production entry
   point and product composition;
5. build the controlled entry point as a separately named, non-installable
   test artifact with an unambiguous path that cannot replace the public
   artifact; and
6. install and report only the permanent `duckdb_api` extension.

The CMake configure result records the imported target's include directory,
library/stub path, and version. The post-build verifier compares those values
with the SDK pins, checks `otool -L` on both the installed artifact and runtime
identity probe, and rejects an artifact that does not name exactly the selected
system libcurl. It also confirms that targets not authorized to carry curl have
no libcurl linkage.

## Drift canaries and acceptance evidence

Deterministic gate tests must prove failure for:

- a changed host build, SDK version, curl header set or bytes, stub bytes or
  link target, configured curl version, SDK-relative path, artifact install
  name, runtime version, SSL-backend identity, or missing thread-safe bit;
- a curl include/library selected outside the verified SDK, including an
  ambient prefix or environment override;
- a missing or extra transport-bearing target, or libcurl linked into a
  connector, planner, or adapter-only target;
- a private controlled artifact installed, reported as the product artifact,
  or sharing an output path or entry-point identity with the public artifact;
- loopback authority, authority-selection environment keys/settings/arguments,
  fault scenarios, controlled canaries, extra public functions, or test-only
  symbols in the installed artifact;
- drift in the accepted extension version, Connector snapshot, controlled
  response fixture, Query public contract, or their adjacent `0.3.0` pins;
  and
- accidental reads from or edits to historical release pins or Community
  enablement inputs when executing the native `0.3.0` path.

`make build` proves the constrained incremental build. `make test` runs the
focused owner oracles plus the private controlled direct-load path and public
artifact inventory without treating GitHub as the correctness oracle.
`make demo` performs only the accepted current-service compatibility narrative.
`make verify` delegates to `scripts/run-native-product-tests.sh` with a new
root, no developer cache, the same dependency checks, all focused and
controlled oracles, SQLLogicTests, artifact inventory, and the clean-host
direct-load contract. Source, fixture, build, dependency, or release-evidence
changes require both the fast source/dependency identity checks and that fresh
gate under `AGENTS.md`.

Implementation completion additionally requires the documentation/agent
asset validator, working- and cached-diff whitespace checks, focused
dependency-gate tests, the ordinary developer commands, a fresh product root,
and an audit of the final CMake target/link dependency graph. Exact public
GitHub rows and order are never gate inputs.

## No-publication boundary

This workstream does not edit or regenerate Community descriptors,
`release/0.2.0/enablement/`, publication or signing workflows, package/YAML
inputs, registry metadata, support-matrix claims, or ordinary-user installation
instructions. It does not initialize the root Community/upstream submodules for
native Make goals, add a vcpkg manifest, fetch or bundle curl/TLS sources, copy
platform dependency bytes into an artifact, or select another platform cell.

The installed extension remains a source-built controlled preview governed by
the accepted RFC 0004 boundary. A future distribution or additional-cell goal
must make its own dependency, redistribution, trust, and evidence decisions;
this facilitation cannot silently authorize them.

## Facilitation exit

The interaction remains **Open** until final implementation evidence shows:

- Remote Runtime can add or maintain a transport-bearing target and diagnose
  dependency/lifecycle gate failures without Enablement editing runtime code;
- Query Experience can build, test, inventory, and demonstrate the permanent
  and private artifacts through the stable Make commands without learning
  SDK, curl, transport, or dependency-verifier internals;
- Connector and Relational focused targets remain independently buildable and
  curl-free, with their source identities consumed rather than recreated by
  the gate;
- every dependency and artifact drift canary fails before unsupported bytes or
  authority can be accepted, while the selected cell passes both reusable and
  fresh paths; and
- the final source, include, target-link, install, test, and evidence
  dependencies match this responsibility map and no ordinary delivery step
  requires Enablement approval.

When those conditions are demonstrated, ownership of the domain oracles and
normal gate operation returns to the receiving teams. Enablement retains the
small reusable build and identity service but does not become the release
decision owner, runtime quality owner, or permanent approval queue.
