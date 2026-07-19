# Native extension boundary trial

Status: completed experiment. It is retained to explain the first native
extension feasibility decision; permanent product code lives under `src/`.

## Question and result

The trial checked whether a version-pinned DuckDB C++ extension could build and
load, return deterministic typed rows over bounded chunks, and clean up after
success, injected failure, and interruption. It also tested whether one command
could reproduce the build and lifecycle evidence without committing generated
DuckDB sources or artifacts.

The answers and recorded environment are in [RESULTS.md](RESULTS.md).

## Reproduce

From the repository root:

```sh
PATH="/path/to/cmake-and-ninja:$PATH" \
  scripts/run-native-extension-trial.sh
```

Prerequisites are Git, Python 3.14 with `venv`, CMake, Ninja, a C++11 compiler,
and network access for pinned build and test dependencies. The extension and
its tests perform no network I/O.

The experimental SQL names begin with `fdw_boundary_probe`. They are not a
public compatibility surface. For current source-build and test instructions,
use the repository [README](../../README.md).
