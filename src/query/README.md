# Query source package

**Owning charter:** [Query Experience](../../docs/teams/QUERY_EXPERIENCE.md)

This package owns the path from a DuckDB SQL question to an immutable,
protocol-neutral request, the assembly of the installed provider services, and
the DuckDB-facing registration, bind, initialization, scan, credential, and
extension-entry boundaries. It does not compile connector packages, classify
relational operations, implement transport or authentication, or reconstruct
query structure that DuckDB does not expose.

## Provider boundaries

Stable Query interfaces remain in `src/include/duckdb_api/` as
`scan_request.hpp`, `product_composition.hpp`, and `duckdb_secret.hpp`; the
stable extension header remains `src/include/duckdb_api_extension.hpp`. Query
consumes Connector Experience's immutable `CompiledConnector`, Relational
Semantics' immutable `ScanPlan`, and Remote Runtime's `ScanExecutor`,
`BatchStream`, `ExecutionControl`, structured errors, authorization capability,
and production runtime factory only through their public headers. Production
code does not include provider-private headers. Request and adapter tests use
Connector's named `connector/support/` fixtures, the controlled integration
uses Runtime's `runtime/support/` loopback service, and Relational Semantics
consumes Query's public-builder fixture through `query/support/`.

## DuckDB callback and lifecycle path

The installed extension entry point builds the product composition and calls
`RegisterDuckdbApi`. Registration completes the DuckDB secret type and provider
before publishing `duckdb_api_scan`. Bind reads constant relation metadata,
builds a conservative `ScanRequest`, asks Relational Semantics for one immutable
plan, and copies that plan into DuckDB bind state without network I/O. Global
initialization resolves any named temporary-memory secret into an
execution-scoped authorization capability and opens exactly one Runtime stream.
Each scan pull validates a bounded typed batch before writing a `DataChunk`.
Interruption cancels the stream, exhaustion marks it complete, and global-state
destruction cancels unfinished work and closes the stream without throwing.
The adapter owns translation across the DuckDB exception boundary; providers
retain ownership of their safe structured failures.

## Implementation units

| Unit | Primary reason to change |
| --- | --- |
| `scan_request.cpp` | Protocol-neutral request identity, conservative DuckDB capability reporting, or deterministic request snapshot behavior changes. |
| `product_composition.cpp` | The installed selection or assembly of public Connector and Runtime provider services changes. |
| `duckdb/table_function_adapter.cpp` | DuckDB table-function registration, bind/init/scan callbacks, typed `DataChunk` transfer, cancellation, close, or execution-error translation changes. |
| `duckdb/extension_entrypoint.cpp` | Installed extension identity, load sequencing, version exposure, or initialization exception containment changes. |
| `duckdb/secret_integration.cpp` | DuckDB Secret Manager registration, temporary-memory secret validation, exact-name resolution, or authorization-capability transfer changes. |
