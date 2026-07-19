# Live REST product proof

Status: completed experiment. The permanent native implementation now lives
under `src/`; this directory is retained as decision evidence, not product
source.

## Question and result

This trial asked whether a DuckDB table function could execute one fixed HTTPS
request during scan, decode a strict typed relation, enforce network and
resource bounds, cancel cooperatively, and close safely while bind remained
offline.

It proved that boundary on the recorded Darwin arm64 environment using a
temporary extension named `duckdb_api_live_rest_proof`. The detailed findings,
limitations, and observed toolchain are in [RESULTS.md](RESULTS.md).

## Reproduce

From the repository root:

```sh
experiments/live-rest-product-proof/scripts/run-live-rest-product-proof.sh
```

Prerequisites are Git, Python 3.14 with `venv`, CMake, Ninja, the macOS libcurl
SDK, a C++11 compiler, and network access for pinned build dependencies. The
default run uses controlled loopback services for success, failure, lifecycle,
and cancellation tests. Pass `--real` to additionally query the public GitHub
API; that check is compatibility evidence, not the correctness oracle.

The trial has its own source, tests, CMake project, and Makefile. Do not copy its
SQL names, dependency layout, or source structure into the product. Current
behavior and developer commands are documented in the repository
[README](../../README.md).
