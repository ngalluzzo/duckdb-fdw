# Engineering Enablement plan: reproducible paginated-relation delivery

## Facilitation objective and routing

Status: **Delivered; cached and fresh product evidence satisfied**.

Make RFC 0007's responsibility-owned production modules, focused oracles,
controlled product proof, and `0.5.0` artifact reproducibly buildable and
difficult to omit from cached or fresh verification. Query Experience remains
accountable for the user outcome and owns its examples and product oracles.
Connector Experience, Relational Semantics, Remote Runtime, and Query
Experience retain the quality of their own source and tests. Engineering
Enablement owns only the shared version, build-graph, identity, and runner
mechanics that integrate those handoffs.

The lead agent retains integration authority and final Git history. Release
publication, tagging, and external delivery are outside this workstream.

## Owned delivery mechanics

| Shared artifact | Enablement responsibility |
| --- | --- |
| `CMakeLists.txt` | Register every final production and controlled translation unit exactly once; give each responsibility-focused oracle a build target with the correct linkage class |
| `extension_config.cmake` and current-version selectors | Select `0.5.0` consistently without changing the pinned DuckDB 1.5.4/macOS arm64 compatibility cell |
| `release/0.5.0/pins.json` | Bind the settled public and controlled source graphs, public contract, dependency inputs, and product cell; seal source digests only after provider handoffs stop changing |
| Historical source verification | Treat `0.4.0` pins and public contract as immutable historical evidence alongside `0.1.0` through `0.3.0` |
| Developer and fresh-product runners | Build, linkage-check, and run every focused target and product oracle handed off by the owning teams |
| Identity verifier tests | Fail closed for omitted, substituted, extra, linked, reordered, or version-inconsistent source and build inputs |

## Handoff and sequencing decisions

- CMake source groups follow provider responsibility; the public and controlled
  product graphs are observed from the finalized CMake targets rather than a
  second handwritten source inventory.
- The current native identity binds every regular file under `src/`, while the
  controlled identity binds the explicit private composition surface. Tests,
  examples, generated output, credentials, and absolute worktree paths do not
  enter the native product digest.
- All focused executables are checked against their intended linkage class:
  ordinary provider and adapter tests remain curl-free, while transport tests
  require the exact pinned system libcurl identity.
- `release/0.4.0` is never rewritten. Its canonical pins, public contract, and
  embedded source identities become historical constants before `0.5.0`
  becomes the current selector.
- Query supplies the final controlled composition and product-oracle manifest.
  Enablement wires and runs that manifest without changing its assertions or
  examples.

## Acceptance evidence

- `scripts/verify-source-identities.py` selects `0.5.0`, verifies the complete
  settled source set and public contract, and rejects drift in immutable
  `0.1.0` through `0.4.0` records.
- `test/python/source_identity_contract.py`,
  `scripts/test-native-dependencies.py`, and `scripts/test-native-dev.sh` pass
  their positive and fail-closed cases against the new current selector.
- CMake configuration emits a finalized product-source record matching the
  `0.5.0` pins in both content and order.
- Cached and fresh runners enumerate every focused `0.5.0` target, verify its
  linkage class, and execute it; the public and private controlled artifacts
  remain separately inventoried.
- `scripts/validate-agent-assets.rb`, shell syntax checks, native formatting,
  source/dependency identity checks, and staged/unstaged whitespace checks pass
  before goal closure.

## Interaction exits and open dependencies

| Partner | Interaction | Exit condition |
| --- | --- | --- |
| Connector Experience | Exited to X-as-a-Service | Final connector production and focused-test units are present in the CMake groups, source identity, cached build, and test run |
| Relational Semantics | Exited to X-as-a-Service | Final planning production, pagination fixtures, and focused tests build and run without hidden dependencies |
| Remote Runtime | Exited to X-as-a-Service | Final runtime modules and controlled/curl-free test targets are built, linkage-classified, and run |
| Query Experience | Collaboration complete | Query's settled production, controlled composition, examples, and three product oracles are wired and pass without Enablement owning their behavior |
| Lead agent | Integration complete | Fresh native product cell, final review set, staging, topology-shaped commits, and goal closure are complete |

## Delivered evidence

- `release/0.5.0/pins.json` seals 25 public translation units, 26 controlled
  translation units, the complete path-bound `src/` inventory, the explicit
  controlled composition surface, and the lead-authored public contract. The
  final digests are `2d093e9b…` for native sources, `ccaf06c…` for controlled
  sources, and `c0d7b2ff…` for the canonical public contract.
- `scripts/verify-source-identities.py` selects `0.5.0`; its 17 contract tests
  pass and now protect `0.4.0` as immutable historical evidence. The dependency
  verifier's 11 deterministic fake-SDK tests and native developer workflow
  guards also pass. `release/0.4.0` remains byte-identical.
- Both cached and fresh runners enumerate all 30 registered executable/private-
  curl targets and all four product-oracle entry points. CMake's configured
  public and controlled source record matches the sealed pin order and
  inventory.
- `make build` completed the full graph with all focused
  targets and both artifacts linked. After the active-close defect was fixed
  and the controlled digest refreshed, `make test` passed every focused target,
  93 SQL assertions, artifact inventory, and the anonymous, authenticated-user,
  and repository-pagination controlled product oracles. `make demo` passed
  against the public `0.5.0` artifact.
- Native formatting and staged/unstaged whitespace checks pass. The committed
  fresh cell completed 967 build steps, all 30 targets, 93 SQL assertions, and
  all product oracles; final adversarial fix-delta review reported no findings,
  and the delivery is recorded as topology-shaped Conventional Commits.
