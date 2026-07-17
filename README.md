# DuckDB “Any API” Connector

This project defines a DuckDB-native relational adapter for well-structured
HTTP and GraphQL APIs. Connectors translate relational scan requests into
authorized remote request plans, declare the exact semantics of pushdown, and
return bounded columnar batches to DuckDB.

The project is FDW-like in purpose but does not implement the PostgreSQL FDW
API. Its public contract is DuckDB-native.

## Design documents

- [Product delivery](docs/PRODUCT_DELIVERY.md) defines how product outcomes are
  shaped into agent-led goals, proven, and handed back.
- [Architecture](docs/ARCHITECTURE.md) defines product invariants, integration
  profiles, relational semantics, and operational boundaries.
- [Connector specification](docs/CONNECTOR_SPECIFICATIONS.md) defines the
  declarative package format and its validation rules.
- [Runtime contracts](docs/RUNTIME_CONTRACTS.md) defines the compiled IR,
  planning contracts, execution interfaces, and policy capabilities.

The three system-design documents are proposals. The connector specification
uses `duckdb_api/draft` until a compatibility version is intentionally
published. The product-delivery document is the active operating process for
turning product outcomes into agent-led work.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for repository and commit conventions.
