# Engineering Enablement plan: reusable native developer cell

## Objective and boundary

Provide Query Experience with a pinned, reusable source bootstrap and native
build service for the active first-trustworthy-query goal. The service removes
extension-template, dependency, tool, and artifact-path knowledge from the
ordinary developer loop while preserving Query Experience ownership of the SQL
demo and its public-host oracle.

Developer builds are explicitly non-release evidence. The unchanged fresh
product runner, tagged release gate, sanitizer cell, manifests, negative
canaries, and two-workspace reproduction remain authoritative.

## Provider contract

The root `Makefile` provides `help`, `bootstrap`, `build`, `test`, `demo`,
`paths`, and `verify`, with `PROFILE=debug|release` and an optional
`DUCKDB_API_DEV_ROOT`. When the Makefile is copied into the pinned DuckDB
extension template, it instead includes the template's extension Makefile so
the existing fresh runner retains its build behavior.

`scripts/native-dev.sh` is the thin command dispatcher. The environment service
under `scripts/lib/` reads dependency and tool identities from
`release/0.1.0/pins.json`, verifies the supported host and compiler, and owns
checksummed tools, exact source checkouts, and the pinned Python host. The build
service beside it owns tracked-source synchronization, incremental Ninja state,
test and demo execution, paths, and fresh-runner delegation. Its `paths` output
distinguishes the statically linked template test CLI from the pinned Python
host and loadable artifact used for the public direct-load demo.

Query Experience provides:

- `examples/first_trustworthy_query.py ARTIFACT`, run by the pinned Python host;
- optional `test/python/source_demo_contract.py PINNED_PYTHON ARTIFACT`; and
- the CMake/native test inventory already consumed by the fresh runners.

## State, freshness, and evidence

The default state is the current worktree's ignored `.build/dev` directory.
An ownership marker prevents accidental cross-worktree reuse. A portable lock
serializes synchronization and builds. Source files are staged from the
tracked projection, hashed, mirrored with deletion, verified at the
destination, and followed by an atomically replaced digest marker. An unchanged
digest leaves file mtimes and incremental objects intact.

The focused workflow guards prove root help and usage, extension-template
delegation, and that the developer script reads rather than copies release
identities. The full developer test runs the five focused native binaries,
SQLLogicTest, loadable-artifact inventory, and the Query-owned direct-load
contract when present. `make verify` allocates a new root and delegates to the
unchanged fresh product runner without reusing developer state.

## Ownership, integration, and exit

Engineering Enablement owns this plan, the thin developer dispatcher, the
environment and build services, their workflow guards, and the root Makefile
dispatch. Query Experience owns README guidance, examples, public-host
assertions, product source, CMake target inventory, and the
native/sanitizer/release runners. The shared integration seam is limited to the
documented Make targets plus the `PINNED_PYTHON` and `ARTIFACT` arguments.

Facilitation exits when Query Experience can bootstrap, build, test, and run
the demo without template or artifact-path knowledge; can maintain the demo
and oracle independently; observes stale/pin/usage guards fail for the intended
reason; and independently runs the unchanged fresh and release evidence paths.

No new RFC is required: this is a reversible implementation of the accepted
RFC 0001 facilitation contract. A mandatory cross-team operating rule, changed
compatibility cell, relaxed fresh gate, or movement of domain-oracle ownership
would require a new RFC assessment.
