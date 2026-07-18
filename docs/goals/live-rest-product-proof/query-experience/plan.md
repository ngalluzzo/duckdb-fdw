# Query Experience plan: live REST relation in DuckDB

## Outcome and status

Own the user-visible proof that a source-built loadable DuckDB extension can
bind offline, execute one native live REST relation, and return strictly typed
rows through ordinary SQL. The implementation and controlled/public query
oracles are complete; durable SQL and product-module promotion remain outside
this evidence trial.

## Query-owned workstream

| Artifact | Responsibility |
| --- | --- |
| `src/live_rest_extension.cpp` | DuckDB registration, bind-data ownership and copying, scan initialization, cancellation view, exception translation, stream consumption, and `DataChunk` output |
| `src/include/live_rest_product_proof_extension.hpp` | Narrow adapter-to-runtime construction boundary |
| `test/integration_success.py` | Direct load, identity, offline bind, schema, exact typed rows, one request per scan, prepared-plan immutability, and public compatibility |
| `README.md` | Trial SQL, build invocation, supported cell, and non-contract limitations |

Query consumes an immutable `LiveScanPlan` and the `ScanExecutor`/`BatchStream`
service. It does not know JSON parser state, libcurl handles, address classes,
or connector-authoring formats.

## Dependencies and interaction exit

- Relational Semantics provides the complete fixed plan and its offline
  snapshot oracle before Query bind state can be considered valid.
- Remote Runtime provides a synchronous bounded pull stream. Query may cancel
  and close it but must not reach into transport or decoder state.
- The lead owns the experiment CMake composition, runner, goal record, results,
  roadmap, review disposition, and Git history.

The Relational collaboration exits only when the adapter can copy and execute
the immutable plan without inferring SQL or remote meaning. The Remote Runtime
collaboration exits only when Query consumes the documented stream and safe
errors without transport knowledge. Because these are trial interfaces rather
than production team APIs, both exits remain open pending the promotion RFC.

## Acceptance evidence

- `LOAD` and `DESCRIBE` perform no HTTP request.
- The ordinary and prepared scans each produce the exact three-column DuckDB
  relation and exactly one controlled request.
- A prepared plan remains bound to its immutable authority even if the
  environment changes before execution.
- The opt-in public HTTPS scan returns one to three correctly typed rows.
- Interrupt terminates sub-second with a redacted error. Concurrent connection
  close waits safely for the hard query deadline; it is not represented as a
  close-triggered cancellation mechanism.

## RFC boundary

The function name, extension name, schema, DuckDB C++ integration, and direct
unsigned load are evidence choices. Promoting any of them to the product
requires the RFC and public-surface review named by the goal brief.
