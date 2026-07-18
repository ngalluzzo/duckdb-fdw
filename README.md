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
- [Changelog](CHANGELOG.md) records user-visible additions and limitations.
- [0.1.0 release notes](docs/releases/0.1.0-notes.md) describe the first native
  preview, its supported compatibility cell, and its explicit limitations.

The broader system-design documents remain proposals. RFC 0001 is the accepted
contract for the executable native `0.1.0` preview, while the connector
specification continues to use `duckdb_api/draft` until authoring compatibility
is intentionally published. The product-delivery, team-topology, and RFC
documents are the active operating model for turning product outcomes into
agent-led work.

## 0.1.0 native preview

Version `0.1.0` is a fixture-backed native preview, not a general package or a
live HTTP connector. It proves that one API-shaped relation can load locally
and return trustworthy, bounded DuckDB rows.

### Supported cell

The only supported product cell is DuckDB 1.5.4 at commit `08e34c447b`,
`osx_arm64`, on macOS 26.5.1 Apple Silicon arm64 with Apple clang 17.0.0 in
C++11 mode, CMake 4.1.2, Ninja 1.13.0, and Python 3.14. The developer bootstrap
downloads pinned, checksummed build dependencies, so its first run requires
network access.

Other DuckDB versions, operating systems, architectures, compilers, and load
modes are unsupported even if they happen to work.

### Run the first query

From the repository root on the supported cell, the shortest path is:

```sh
make demo
```

This bootstraps a reusable debug build, directly loads the unsigned local
artifact into the pinned DuckDB 1.5.4 Python host, and executes
[`examples/first-trustworthy-query.sql`](examples/first-trustworthy-query.sql).
The example checks extension identity and prints:

```text
DuckDB v1.5.4 (08e34c447b)
Extension duckdb_api 0.1.0 loaded=true installed=false mode=NOT_INSTALLED
Schema id BIGINT, name VARCHAR, active BOOLEAN
id      name    active
1       alpha   true
2       beta    false
3       gamma   true
```

### Developer commands

The root Make targets form the supported source-development interface:

| Command | Purpose |
| --- | --- |
| `make help` | List the supported targets and build overrides without building or using the network. |
| `make bootstrap` | Validate the supported host and prepare or refresh pinned tools, sources, and the Python host. |
| `make build` | Incrementally build reusable debug artifacts. Use `PROFILE=release` for a release-profile developer build. |
| `make test` | Build and run the focused native, SQL, inventory, and direct-load developer oracles. |
| `make demo` | Build as needed and run the first-query example through the pinned Python host. |
| `make paths` | Print absolute `key=value` paths for the current profile, including `pinned_python` and `artifact`. |
| `make verify` | Run the complete pre-tag product suite from a newly allocated build root rather than the developer cache. |

The reusable commands keep their state under `.build/dev` by default. Select a
profile or another developer root explicitly when needed:

```sh
make test PROFILE=release DUCKDB_API_DEV_ROOT=/absolute/developer-root
make paths PROFILE=release DUCKDB_API_DEV_ROOT=/absolute/developer-root
```

`make paths` may also report `static_test_cli`. That binary statically links the
extension for native and SQL testing; it is not a clean host and is not evidence
of direct artifact loading. The public demo inputs are `pinned_python` and
`artifact`.

`make build`, `make test`, `make demo`, and `make paths` may reuse developer
artifacts. `make verify` deliberately uses a fresh root and is stronger pre-tag
evidence, but it is still not the authoritative release gate. Authoritative
`v0.1.0` evidence requires the clean tagged product gate, Linux sanitizer cell,
manifest verification, and two-workspace reproduction documented in
[the 0.1.0 release runbook](docs/releases/0.1.0.md).

### Preview limitations

The preview embeds one deterministic `example.items` fixture. It does not
provide live HTTP, arbitrary connector loading, authentication, secrets,
pagination, retries, caching, GraphQL, binary installation, signing, or
automatic updates. Filtering, ordering, limits, and offsets remain DuckDB-owned;
there is no custom explain surface. The `duckdb_api/draft` connector shape is
internal evidence, not a stable authoring contract.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for repository and commit conventions.

## License

This project is licensed under the [MIT License](LICENSE).
