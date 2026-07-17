# Query Experience plan: source build, load, and demo

## Outcome and status

Give a DuckDB user one exact source-development path from checkout to the
accepted fixture-backed query while describing the current state as an
unreleased `0.1.0` candidate. This Query-owned package is implemented on its
topic branch; integration with the Engineering Enablement Make targets remains
the provider dependency.

The initial plan treated the template-built DuckDB CLI as the matching load
host. Executable evidence showed that the template CLI reports `duckdb_api` as
`STATICALLY_LINKED` before `LOAD`, so it cannot prove direct local loading. The
corrected design uses the separately pinned DuckDB 1.5.4 Python host for the
demo and direct-load oracle. The static CLI remains test infrastructure only.

## Query-owned artifacts

| Artifact | Responsibility |
| --- | --- |
| `README.md` | Shortest path, developer commands, supported cell, evidence boundary, and explicit limitations |
| `CHANGELOG.md` | User-visible candidate capability and limitations |
| `docs/releases/0.1.0-notes.md` | Product-facing candidate notes distinct from the evidence runbook |
| `examples/first-trustworthy-query.sql` | Extension identity inspection and the unchanged accepted SQL |
| `examples/first_trustworthy_query.py` | Direct unsigned load through the pinned host, exact identity/schema/row checks, and readable output |
| `test/python/source_demo_contract.py` | Isolated direct-load, exact result, and redacted unknown-relation oracle without importing DuckDB into the contract process |

No production source, public contract, pin, build implementation, or release
runbook belongs to this package.

## Engineering Enablement provider contract

Query consumes the root `make help`, `make bootstrap`, `make build`,
`make test`, `make demo`, `make paths`, and `make verify` targets. In
particular:

- `make demo` invokes the example runner with the pinned Python host and the
  loadable artifact;
- `make test` invokes `source_demo_contract.py PINNED_PYTHON ARTIFACT` after
  the existing native, SQL, and inventory checks;
- `make paths` labels absolute `pinned_python`, `artifact`, and
  `static_test_cli` paths without presenting the static CLI as a clean host;
  and
- `make verify` uses a new build root and does not reuse developer state.

Engineering Enablement owns bootstrap, caching, synchronization, target
implementation, and gate mechanics. Query relies only on the documented Make
surface and path labels.

## Acceptance evidence and failure UX

- `make demo` loads the artifact unsigned in the pinned DuckDB 1.5.4 host and
  prints the accepted extension identity, direct-load mode, schema, and three
  rows.
- `source_demo_contract.py` copies the artifact into an empty directory, adds
  decoy local inputs and private-looking environment values, then proves that
  the example still returns the embedded fixture.
- The same oracle requires an unknown relation to exit nonzero with
  `[duckdb_api][bind] connector=example: unknown relation identifier` and no
  decoy value or local path.
- Existing private adapter oracles retain ownership of decode/schema redaction,
  cancellation, and teardown; no product fault-injection surface is added.
- Unsupported host/tool identities and stale authoritative build roots retain
  early actionable failures in the Enablement-owned gates.

## Compatibility, governance, and interaction exit

The supported cell remains exactly DuckDB 1.5.4 commit `08e34c447b`,
`osx_arm64`, macOS 26.5.1 Apple Silicon arm64, Apple clang 17.0.0/C++11, CMake
4.1.2, Ninja 1.13.0, and Python 3.14. The preview remains fixture-backed,
source-built, and unsigned, with all RFC 0001 exclusions preserved.

No RFC trigger applies: these docs, examples, and oracles restore the accepted
RFC 0001 build/direct-load narrative without changing SQL, diagnostics,
compatibility, load mode, or a semantic team interface. Expanding any of those
surfaces requires a fresh RFC assessment.

The Engineering Enablement facilitation exits when Query can maintain and run
the documented demo and oracle through the root Make surface without knowing
bootstrap internals, and the fresh product gate executes the same direct-load
contract. The interaction remains open until the two topic packages are
integrated and the combined gates pass.

## Worktree-safe package

- Frozen base: `fd713233ae8d56edf62adf26d9bff980ee369af3`.
- Branch: `goal/0.1-build-dx/query`.
- Query owns only the artifacts listed above.
- Expected overlap: none. Makefile, scripts, and their guard tests are the
  Engineering Enablement package; the lead owns integration and final history.
- Narrow oracle: `python3 test/python/source_demo_contract.py PINNED_PYTHON ARTIFACT`.
- Integration evidence: focused oracle, repository asset validation, source
  identity verification, whitespace checks, and the combined provider gate.
