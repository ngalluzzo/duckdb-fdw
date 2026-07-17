# Native extension boundary trial

This directory contains decision evidence for the first product goal. It is a
temporary, non-public DuckDB extension built from the official native C++
extension template. It does not establish the project's final extension name,
SQL surface, connector syntax, shared runtime interfaces, compatibility
policy, or dependency layout.

The trial answers four bounded questions:

1. Can a version-pinned native extension be built and loaded on the recorded
   local DuckDB target?
2. Can a table function return deterministic typed rows over multiple bounded
   chunks?
3. Does state clean up after successful execution, an injected execution
   failure, and query interruption?
4. Can the resulting build and lifecycle evidence be reproduced by one
   command without committing generated DuckDB sources or build artifacts?

Run the complete trial from the repository root:

```sh
PATH="/path/to/cmake-and-ninja:$PATH" scripts/run-native-extension-trial.sh
```

Prerequisites are Git, Python 3.14 with `venv`, CMake, Ninja, and a C++11
toolchain. The script fetches the pinned official template and its pinned
submodules into `.build/`, builds and runs the SQLLogicTests, installs the exact
DuckDB Python wheel into an isolated generated environment, and runs the
interruption probe. Network access is used only to acquire build and test
dependencies; the extension and its tests perform no network I/O.

The experimental SQL names begin with `fdw_boundary_probe`. They must not be
copied into a product contract or treated as compatibility commitments.
