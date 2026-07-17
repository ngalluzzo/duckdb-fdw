# DuckDB “Any API” Connector

This project defines a DuckDB-native relational adapter for well-structured
HTTP and GraphQL APIs. Connectors translate relational scan requests into
authorized remote request plans, declare the exact semantics of pushdown, and
return bounded columnar batches to DuckDB.

The project is FDW-like in purpose but does not implement the PostgreSQL FDW
API. Its public contract is DuckDB-native.

## Project documents

- [Product roadmap](ROADMAP.md) defines the intended SemVer release progression
  from the first executable preview through the stable `1.0.0` contract.
- [Product delivery](docs/PRODUCT_DELIVERY.md) defines how product outcomes are
  shaped into agent-led goals, proven, and handed back.
- [Team topology](docs/TEAM_TOPOLOGY.md) and its linked charters define the
  value streams, team types, accountability, review lenses, and interaction
  model.
- [RFC process](docs/RFC_PROCESS.md) and [template](docs/RFC_TEMPLATE.md) define
  how durable shared decisions are proposed, reviewed, and propagated.
- [Architecture](docs/ARCHITECTURE.md) defines product invariants, integration
  profiles, relational semantics, and operational boundaries.
- [Connector specification](docs/CONNECTOR_SPECIFICATIONS.md) defines the
  declarative package format and its validation rules.
- [Runtime contracts](docs/RUNTIME_CONTRACTS.md) defines the compiled IR,
  planning contracts, execution interfaces, and policy capabilities.

The broader system-design documents remain proposals. RFC 0001 is the accepted
contract for the executable native `0.1.0` preview, while the connector
specification continues to use `duckdb_api/draft` until authoring compatibility
is intentionally published. The product-delivery, team-topology, and RFC
documents are the active operating model for turning product outcomes into
agent-led work.

## Native preview

The preview is a source-built native C++ extension for one exact compatibility
cell. From a clean checkout on the supported macOS arm64 host, run the complete
product test build in a new path:

```sh
scripts/run-native-product-tests.sh /absolute/new/build-root release
```

Start the matching DuckDB 1.5.4 host with unsigned local extensions enabled,
load the artifact path printed by the command, and query:

```sql
SELECT id, name, active
FROM duckdb_api_scan(
    connector := 'example',
    relation := 'items'
)
ORDER BY id;
```

The result is `(1, 'alpha', true)`, `(2, 'beta', false)`, and
`(3, 'gamma', true)`. The preview embeds this deterministic fixture; it does
not provide live HTTP, arbitrary connector loading, authentication, pagination,
GraphQL, or binary installation.

The authoritative tagged release command, Linux sanitizer evidence command,
manifest format, and reproduction handoff are documented in
[the 0.1.0 release runbook](docs/releases/0.1.0.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for repository and commit conventions.
