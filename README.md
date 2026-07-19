# duckdb-fdw

`duckdb-fdw` is a DuckDB extension for querying well-structured HTTP APIs as
typed, resource-bounded relations. It is FDW-like in purpose, but its public
surface is DuckDB-native and it does not implement PostgreSQL's FDW API.

The project is currently a source-built preview. It exposes three fixed GitHub
relations while the general connector-authoring and distribution surfaces are
still under development.

## What works today

| Relation | Authentication | Result |
| --- | --- | --- |
| `github.duckdb_login_search_page` | None | Up to three public GitHub users with `id`, `login`, and `site_admin` |
| `github.authenticated_user` | Explicit temporary DuckDB secret | The current GitHub identity with `id`, `login`, and `site_admin` |
| `github.authenticated_repositories` | Explicit temporary DuckDB secret | A bounded page chain of repositories with `id`, `full_name`, `private`, `fork`, and `archived` |

The anonymous relation uses the current public SQL surface:

```sql
SELECT id, login, site_admin
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'duckdb_login_search_page'
);
```

These relations use fixed HTTPS operations and strict schemas. Bind,
`DESCRIBE`, `EXPLAIN`, and `PREPARE` do not perform network I/O. Filtering,
ordering, limits, and offsets are applied by DuckDB after the remote rows are
read. Execution is bounded and fails the statement on malformed data, an
invalid page transition, cancellation, or resource exhaustion; it does not
return a complete-looking partial result.

See the [0.5.0 release notes](docs/releases/0.5.0-notes.md) for the precise
behavior and execution limits.

## Quick start

The supported source-build environment is deliberately narrow: Apple Silicon
arm64, macOS 26.5.1 build `25F80`, and the pinned DuckDB 1.5.4 toolchain. The
bootstrap rejects other hosts rather than implying compatibility that has not
been tested. The first run downloads pinned, checksummed build dependencies.
Before running it, install Python 3.14 and the Xcode Command Line Tools that
provide the pinned Apple compiler, macOS SDK, `xcrun`, `make`, `git`, `nm`, and
`strings`; the host must also provide `curl`, `rsync`, `shasum`, `tar`, and
`unzip`. Bootstrap downloads project tools and dependencies, not these host
prerequisites.

From the repository root:

```sh
make bootstrap
make demo
```

`make demo` builds the extension, loads it directly into the pinned DuckDB
Python host, and runs the anonymous GitHub example. It requires outbound HTTPS
access to `api.github.com`; the public service is a compatibility demonstration,
while the test suite uses controlled local services for correctness.

## Authenticated examples

Build the artifact and print the pinned Python and extension paths:

```sh
make build
make paths
```

Use those paths with either example:

```sh
/absolute/pinned_python -I examples/authenticated_user.py \
  /absolute/duckdb_api.duckdb_extension

/absolute/pinned_python -I examples/authenticated_repositories.py \
  /absolute/duckdb_api.duckdb_extension
```

Both runners read a short-lived GitHub token from a hidden interactive prompt
and create only a temporary DuckDB secret. They do not accept a token through
an argument, environment variable, or file. The repository example prints its
schema, aggregate count, extension and relation identity, and fixed request
envelope, but never private repository rows or names.

The underlying SQL is in
[`examples/authenticated-user.sql`](examples/authenticated-user.sql) and
[`examples/authenticated-repositories.sql`](examples/authenticated-repositories.sql).

## Development

The root Makefile is the supported development interface:

| Command | Purpose |
| --- | --- |
| `make help` | Show commands and supported overrides without building |
| `make bootstrap` | Prepare or repair the pinned developer environment |
| `make build` | Incrementally build the debug extension |
| `make test` | Run native, controlled-service, SQL, artifact, and direct-load tests |
| `make demo` | Build as needed and run the anonymous live example |
| `make paths` | Print the active build, Python, CLI, and extension paths |
| `make verify` | Run the complete product suite from a fresh build root |

Use `PROFILE=release` for a release-profile developer build or
`DUCKDB_API_DEV_ROOT=/absolute/path` to isolate the reusable developer state.
Developer builds and tests are not release evidence.

Start with the [source guide](src/README.md) before changing production code.
Repository, Git, verification, and documentation practices are in
[CONTRIBUTING.md](CONTRIBUTING.md).

## Project documentation

- [Architecture](docs/ARCHITECTURE.md) explains the product model and
  correctness invariants.
- [Runtime contracts](docs/RUNTIME_CONTRACTS.md) define planning, execution,
  security, resource, and lifecycle behavior.
- [Connector specifications](docs/CONNECTOR_SPECIFICATIONS.md) describe the
  proposed declarative connector format. It is not yet a stable authoring
  contract.
- [Changelog](CHANGELOG.md) records user-visible changes and limitations.
- [Roadmap](ROADMAP.md) describes the intended progression toward `1.0.0`.
- [Team topology](docs/TEAM_TOPOLOGY.md), [product delivery](docs/PRODUCT_DELIVERY.md),
  and the [RFC process](docs/RFC_PROCESS.md) are contributor operating guides.

## Current limitations

- The extension is not published or signed and cannot be installed from the
  DuckDB Community repository.
- Only the three compiled-in GitHub relations above are available. Arbitrary
  URLs, headers, connector packages, and GraphQL execution are not supported.
- Authentication supports one explicitly named temporary `duckdb_api/config`
  bearer secret. Persistent and environment-backed providers, implicit secret
  selection, and OAuth are not supported.
- There are no retries, rate-limit waits, parallel page requests, resume state,
  or caching.
- The supported platform and DuckDB version are limited to the exact source
  build cell described above.

## License

Licensed under the [MIT License](LICENSE).
