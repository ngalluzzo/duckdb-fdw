# Live REST product proof

This directory contains decision evidence for the live REST product goal. It
builds a distinct, temporary DuckDB extension from the official native C++
extension template and proves one real HTTP-to-relation execution path. The
trial does not establish a public extension name, SQL spelling, connector
syntax, runtime API, or compatibility promise.

The no-argument table function `duckdb_api_live_rest_proof()` plans one fixed,
unauthenticated GitHub user-search request and returns the non-null schema
`id BIGINT, login VARCHAR, site_admin BOOLEAN`. Bind creates an immutable plan
without network I/O. Scan initialization alone receives network authority.
The execution profile has fixed response, record, string, batch, and wall-time
budgets and disables redirects, proxies, cookies, authentication, retries, and
pagination. Its bounded concrete transport uses the macOS cell's libcurl with
TLS peer and host verification and permits only HTTP and HTTPS. This is
dependency evidence for the trial, not a production dependency decision.

Run the complete controlled proof from the repository root:

```sh
experiments/live-rest-product-proof/scripts/run-live-rest-product-proof.sh
```

Prerequisites are Git, Python 3.14 with `venv`, CMake, Ninja, the macOS libcurl
SDK, and a C++11 toolchain on Darwin arm64. The runner fetches the pinned
official template and submodules into `.build/`, builds the loadable artifact
from a fresh build tree, runs the focused plan and runtime tests, and directly
loads the artifact in the pinned DuckDB Python host. The integration oracle starts its own
loopback HTTP servers and runs separate success and failure/lifecycle oracles
covering offline bind, the exact request, strictly typed rows, bounded
failures, sub-second cooperative cancellation, and a connection close that
waits safely for the hard query deadline.

To additionally exercise the same artifact against the public GitHub API, pass
`--real`. That opt-in request is an upstream compatibility demonstration only;
the controlled service remains the correctness oracle.

The source contains no connector package or distribution integration. Nothing
under this experiment should be copied into a stable product contract without
the product and RFC checkpoints required by `AGENTS.md`.
