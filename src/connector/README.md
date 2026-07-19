# Connector source package

**Owning charter:** [Connector Experience](../../docs/teams/CONNECTOR_EXPERIENCE.md)

This package owns the immutable connector catalog model, its closed pagination
and resource declarations, and the native GitHub catalog composition. It
performs deterministic construction, validation, and safe explanation only; it
does not plan SQL, execute requests, resolve credentials, or perform network
I/O.

## Provider boundary

The public provider facades remain `duckdb_api/connector.hpp`, which exposes
`BuildNativeGithubConnector`, and `duckdb_api/connector_catalog.hpp`, which
exposes the immutable compiled catalog values consumed by Query Experience,
Relational Semantics, and Remote Runtime. Connector-private collaboration
between implementation units lives in `duckdb_api/internal/connector/` and is
not a consumer API.

Production units may depend on the C++ standard library and Connector's public
or private headers. They must not depend on query requests, scan plans, runtime
execution, transport, DuckDB integration, or consumer test internals. Consumers
depend on the public compiled values; deterministic private construction access
and catalog factories live under `test/cpp/connector/support/`.

## Implementation units

| Unit | Primary reason to change |
| --- | --- |
| `native_github_composition.cpp` | The installed native GitHub relation inventory or its declared catalog policy changes. |
| `catalog_model.cpp` | Compiled catalog value validation, immutable ownership, lookup, or safe explanation changes. |
| `pagination_declaration.cpp` | The intrinsic laws or explanation of the closed compiled pagination declaration change. |
| `resource_ceiling_declaration.cpp` | The intrinsic laws or explanation of compiled resource-ceiling declarations change. |
