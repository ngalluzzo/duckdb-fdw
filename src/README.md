# Source ownership and dependency map

Production source is partitioned by durable responsibility, with one owning
charter and one primary reason to change per package. Package-local
`sources.cmake` files are the build inventories that owning teams maintain;
the root build composes those inventories without redefining ownership.

```text
Connector metadata -----> Relational planning <----- Query request
       |                         |                         ^
       |                         v                         |
       |                     ScanPlan -----> Remote Runtime
       |                         |                 |
       +-----------------> DuckDB adapter <--------+
```

Arrows point from provider to consumer. Query supplies the protocol-neutral
request consumed by Semantics, while its DuckDB adapter consumes the immutable
connector, plan, and runtime stream services. Runtime executes a complete plan;
it does not reconstruct Connector or Query state or reclassify relational
meaning.

| Package | Owning charter | Provides | May consume |
| --- | --- | --- | --- |
| [`connector/`](connector/) | [Connector Experience](../docs/teams/CONNECTOR_EXPERIENCE.md) | Immutable compiled connector metadata and native catalog composition | Standard library and Connector-private helpers |
| [`semantics/`](semantics/) | [Relational Semantics](../docs/teams/RELATIONAL_SEMANTICS.md) | Deterministic `ScanRequest` to immutable `ScanPlan` planning | Connector and Query public values |
| [`runtime/`](runtime/) | [Remote Runtime](../docs/teams/REMOTE_RUNTIME.md) | Bounded authenticated execution, transport, decoding, pagination, policy, and stream lifecycle | Immutable public `ScanPlan` and execution controls |
| [`query/`](query/) | [Query Experience](../docs/teams/QUERY_EXPERIENCE.md) | Protocol-neutral requests, installed composition, and DuckDB integration | Public Connector, Semantics, and Runtime services |

Stable provider facades share the `src/include/duckdb_api/` namespace so
consumers do not depend on physical implementation layout. Private headers are
grouped beneath `src/include/duckdb_api/internal/<provider>/` and are not team
APIs. Tests mirror production packages under `test/cpp/`; a consumer links a
bounded provider fixture service or an explicit integration target instead of
compiling and constructing provider internals.
