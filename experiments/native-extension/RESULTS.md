# Native extension boundary trial results

**Status:** Passed on the recorded target

**Purpose:** Decision evidence only. These results do not accept a public SQL,
connector, runtime, compatibility, or distribution contract.

## Decision context

The original trial proposed the official pure-Rust C Extension API template.
Inspection showed that the template still describes itself as experimental and
currently sets `USE_UNSTABLE_C_API=1`, which pins its artifacts to an exact
DuckDB version. The product manager directed the project to use the established
native path instead.

The completed trial therefore uses DuckDB's official native C++ extension
template. This path is proven and well-supported by DuckDB's build and test
tooling, but it consumes DuckDB's internal C++ extension interface and is also
version-coupled. The result supports using native C++ for the first executable
slice; it does not prove the roadmap's intended portable stable-C-API boundary
for `1.0.0`.

## Recorded target

| Dimension | Value |
| --- | --- |
| Host | macOS 26.5.1, Apple Silicon arm64 |
| DuckDB | `v1.5.4 (Variegata) 08e34c447b` |
| DuckDB commit | `08e34c447bae34eaee3723cac61f2878b6bdf787` |
| Native extension template | `cfaf3e236008e782d27f4341b0ee036002d0a449` |
| Extension CI tools | `b777c70d30942cca5bef62d6d4fa23a13362f398` |
| Compiler | Apple clang 17.0.0 (`clang-1700.0.13.5`) |
| C++ language mode | C++11, explicitly passed to CMake |
| CMake | 4.1.2 |
| Ninja | 1.13.0 |
| Python test host | 3.14.3 with `duckdb==1.5.4` |
| Extension version | `0.0.0-boundary-trial` |
| Installation mode | Direct unsigned load of a local experimental artifact |

The runner passes an explicit `v1.5.4-0-g08e34c447b` source identity to
DuckDB's build. A shallow checkout without that value silently reports
`v0.0.1`, which is unsuitable as compatibility evidence.

## Reproduction

From the repository root, with CMake and Ninja on `PATH`:

```sh
scripts/run-native-extension-trial.sh
```

The command fetches only pinned upstream sources into ignored `.build/`
storage. It builds the native extension and DuckDB test host, runs the
SQLLogicTest, creates an isolated Python environment for DuckDB 1.5.4, and
runs the interruption test. It starts from a fresh debug build tree, rejects
unverified changes in the pinned DuckDB and extension-CI-tool checkouts,
verifies the experiment overlay byte for byte, and invokes the SQL test host
directly with the probe marked as required so missing static linkage cannot
skip the test. The target-specific Python wheel is force-reinstalled on every
run using the SHA-256 hash in `test/requirements.txt`, then its installed
version is verified.

## Evidence

### Build and load

- The native extension compiled as both a statically linked test extension and
  a loadable `.duckdb_extension` artifact.
- DuckDB 1.5.4 loaded the artifact directly with unsigned-extension loading
  enabled.
- `duckdb_extensions()` reported version `0.0.0-boundary-trial` and showed
  `fdw_boundary_probe` as loaded but not installed, which is the intended
  experimental mode.
- No extension or test code performs network I/O. Network access during the
  runner is limited to fetching the pinned template, DuckDB sources, build
  tooling submodule, and exact Python test wheel.

### Bounded deterministic output

The SQLLogicTest passed 29 assertions. Its primary scan produced:

```text
rows=5000
sum(row_id)=12497500
min(batch_id)=0
max(batch_id)=39
```

The probe therefore returned exactly 5,000 typed rows over 40 explicit chunks
of at most 128 rows. Payload values were deterministic, and the implementation
rejects batch sizes above DuckDB's `STANDARD_VECTOR_SIZE`.

### Success and failure cleanup

In-process counters, observed from a second connection before another statement
could touch the producer connection, proved that global scan state was
destroyed after:

- two successful scans: `opened=2`, `closed=2`, `chunks=42`;
- a bind-time invalid-bound failure, which opened no scan state; and
- an injected execution failure after 350 emitted rows, after which
  `opened=3`, `closed=3`, `chunks=46`;
- an injected failure exactly at the 10-row end boundary, after which
  `opened=4`, `closed=4`, `chunks=48`; and
- an injected failure at the zero-row end boundary, after which
  `opened=5`, `closed=5`, `chunks=48`.

The injected failure is experimental test behavior, not a proposed diagnostic
contract.

### Interruption

A Python thread executed a deliberately slow scan while an observer connection
waited until the probe had produced a chunk and entered its next interruptible
wait. Another thread then called DuckDB's connection interrupt. The query
returned an interruption error and the worker stopped within the five-second
oracle. The final counters were:

```text
opened=1 closed=1 chunks=1 interruptions=1 active_waiters=0
```

This proves that a native table function can observe the DuckDB interrupt while
inside its callback, abort cooperatively, leave no active wait, and destroy its
global state on this target. It does not
yet prove cancellation across an HTTP request, async worker, bounded channel,
or connector runtime. The number of chunks produced before the synchronized
interrupt is intentionally timing-dependent; the oracle requires it to be
nonzero rather than asserting a scheduler-dependent count.

## Build-system finding

DuckDB's normal debug build enables sanitizers. On this macOS 26.5.1 host, the
generated platform helper recursively entered AddressSanitizer initialization
inside the dynamic loader and spun before `main`. The recorded trial disables
ASan and UBSan for this local debug build so the official platform and test
programs can start.

That workaround is a limitation, not a clean safety result. The product build
must regain sanitizer evidence on a supported CI target rather than treating
this host-specific run as a replacement. The hang was outside the extension's
code, during sanitizer runtime initialization.

## RFC consequences

The first-query product RFC can proceed with the following evidence:

- Native C++ is a feasible implementation boundary for the first executable
  DuckDB table-function slice.
- The initial compatibility claim must name exact DuckDB, platform, and build
  cells; this trial proves only DuckDB 1.5.4 on `osx_arm64`.
- The first slice can require bounded pull batches and cooperative interruption
  without inventing a Rust FFI or async-runtime boundary.
- Native C++ version coupling must be explicit. The RFC must not describe this
  trial as proof of the portable stable-C-API profile.
- The experimental SQL names and counter interfaces must not survive into the
  public contract by accident.
- Release and distribution evidence remains open: the trial uses unsigned
  direct loading, does not install from a repository, and does not select a
  project license.

The trial does not decide connector syntax, the accepted SQL narrative, REST
execution, fixture representation, relational planning interfaces, error
categories, or the final `1.0.0` integration profile. Those remain inside the
required RFC boundary.

## Upstream references

- [Official DuckDB native extension template](https://github.com/duckdb/extension-template/tree/cfaf3e236008e782d27f4341b0ee036002d0a449)
- [Official experimental Rust extension template](https://github.com/duckdb/extension-template-rs/tree/0a839a1b445cc96d5321c67db9274d0ae6e9f869)
- [DuckDB extension versioning](https://duckdb.org/docs/extensions/versioning_of_extensions)
- [DuckDB release cycle and extension API stability](https://duckdb.org/docs/current/dev/release_cycle)
