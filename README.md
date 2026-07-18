# DuckDB “Any API” Connector

This project builds a DuckDB-native relational adapter for well-structured HTTP
and GraphQL APIs. Connectors describe remote relations and authorized request
plans; the runtime executes bounded work and returns strict typed batches;
DuckDB retains relational semantics such as filtering, ordering, limits, and
offsets.

The project is FDW-like in purpose but does not implement the PostgreSQL FDW
API. Its public contract is DuckDB-native.

## Project documents

- [Product roadmap](ROADMAP.md) defines the intended SemVer progression toward
  a stable `1.0.0` contract.
- [Product delivery](docs/PRODUCT_DELIVERY.md) defines how outcomes are shaped,
  proven, and handed back.
- [Team topology](docs/TEAM_TOPOLOGY.md) and its linked charters define team
  accountability and interaction boundaries.
- [RFC process](docs/RFC_PROCESS.md) and [template](docs/RFC_TEMPLATE.md) define
  durable shared decisions.
- [Architecture](docs/ARCHITECTURE.md) defines product invariants, relational
  semantics, security policy, and operational boundaries.
- [Connector specification](docs/CONNECTOR_SPECIFICATIONS.md) describes the
  proposed declarative package format.
- [Runtime contracts](docs/RUNTIME_CONTRACTS.md) defines compiled IR, planning,
  execution, and lifecycle contracts.
- [Changelog](CHANGELOG.md) records user-visible additions and limitations.
- [0.1.0 release notes](docs/releases/0.1.0-notes.md) preserve the historical
  fixture-preview contract.

[RFC 0006](docs/rfcs/0006-capability-scoped-authenticated-relation.md) is the
accepted decision for the source-built `0.4.0` authenticated preview, on top of
RFC 0005's live REST base. The declarative connector specification remains
`duckdb_api/draft`: this release compiles two native product relations and does
not publish connector authoring compatibility.

## 0.4.0 authenticated REST preview

Version `0.4.0` preserves the bounded, compiled-in public GitHub relation:

```sql
SELECT id, login, site_admin
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'duckdb_login_search_page'
);
```

The relation is exactly the zero-to-three `items` in one fixed GitHub search
response page. It returns required `id BIGINT`, `login VARCHAR`, and
`site_admin BOOLEAN` values. Missing, null, incompatible, malformed, or
over-budget remote values fail the query instead of being silently coerced.

The fixed source request is an unauthenticated HTTPS GET to `api.github.com`.
Bind, `DESCRIBE`, and `PREPARE` remain offline; the request begins only when a
scan executes. Filtering, ordering, limits, and offsets are evaluated by
DuckDB over the returned base page.

Public-service row identity and service order are not guaranteed. The
controlled test service is the deterministic correctness oracle; the public
GitHub query is current-service compatibility evidence.

It also adds one fixed authenticated relation. A user creates one explicitly
named temporary secret and queries the identity authorized by its current
token:

```sql
CREATE TEMPORARY SECRET github_default (
    TYPE duckdb_api,
    PROVIDER config,
    TOKEN 'github-token-value'
);

SELECT id, login, site_admin
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'authenticated_user',
    secret := 'github_default'
);
```

The secret name is validated at bind, but its value is resolved from DuckDB's
temporary `memory` storage only when each execution initializes. Preparing,
describing, and explaining the query do not resolve the secret or perform
network I/O. Replacing or dropping the secret affects the next execution of an
existing prepared statement. Persistent and environment-backed secrets,
implicit selection, token arguments, caller-selected URLs or headers, and
redirects are not supported.

### Supported cell

The supported product cell is DuckDB 1.5.4 at commit `08e34c447b`,
`osx_arm64`, on macOS 26.5.1 build `25F80` and Apple Silicon arm64, with Apple
clang 17.0.0 in C++11 mode, CMake 4.1.2, Ninja 1.13.0, Python 3.14, and the
platform libcurl 8.7.1 cell recorded by RFC 0005. The developer bootstrap
downloads pinned, checksummed build dependencies, so its first run requires
network access.

Other DuckDB versions, operating systems, architectures, compilers, libcurl
cells, and load modes are unsupported even if they happen to work. The
artifact is loaded directly as an unsigned local source build; it is not a
published installable extension.

### Run the anonymous live query

From the repository root on the supported cell:

```sh
make demo
```

This builds as needed, directly loads the local artifact into the pinned DuckDB
Python host, and runs
[`examples/first-live-rest-relation.sql`](examples/first-live-rest-relation.sql)
through [`examples/first_live_rest_relation.py`](examples/first_live_rest_relation.py).
The example validates extension identity, the strict three-column schema, and
the zero-to-three-row ceiling, then prints the rows observed from GitHub. It
does not assert specific public logins or public response order.

The demo requires DNS and outbound HTTPS access to `api.github.com`. It does
not use credentials, proxy environment variables, `.netrc`, `.curlrc`, or a
caller-selected URL. GitHub availability and anonymous rate limits can still
cause a bounded, redacted query failure.

### Run the authenticated live query

Build the artifact, print its path, then run the authenticated example with the
pinned Python host reported by `make paths`:

```sh
make build
make paths
/absolute/pinned_python -I examples/authenticated_user.py /absolute/duckdb_api.duckdb_extension
```

The runner reads a short-lived GitHub token from a hidden interactive prompt;
it does not accept a token argument, environment variable, or file. It creates
only a temporary secret, runs
[`examples/authenticated-user.sql`](examples/authenticated-user.sql), and
prints the one typed identity row. The token and returned identity are not
written to repository evidence. HTTP `401` and `403` remain distinct redacted
authentication and authorization failures.

### Developer commands

The root Make targets form the supported source-development interface:

| Command | Purpose |
| --- | --- |
| `make help` | List supported targets and build overrides without building or using the network. |
| `make bootstrap` | Validate the supported host and prepare or refresh pinned tools, sources, and the Python host. |
| `make build` | Incrementally build reusable debug artifacts. Use `PROFILE=release` for a release-profile developer build. |
| `make test` | Build and run focused native, controlled-service, SQL, inventory, and direct-load developer oracles. |
| `make demo` | Build as needed and execute the public GitHub example through the pinned Python host. |
| `make paths` | Print absolute `key=value` paths for the current profile, including `pinned_python` and `artifact`. |
| `make verify` | Run the complete product suite from a newly allocated build root instead of the developer cache. |

Reusable commands keep state under `.build/dev` by default. Select a profile
or another developer root explicitly when needed:

```sh
make test PROFILE=release DUCKDB_API_DEV_ROOT=/absolute/developer-root
make paths PROFILE=release DUCKDB_API_DEV_ROOT=/absolute/developer-root
```

`make paths` may also report `static_test_cli`. That binary statically links
the extension for native and SQL testing; it is not clean-host direct-load
evidence. The public demo inputs are `pinned_python` and `artifact`.

`make build`, `make test`, `make demo`, and `make paths` may reuse developer
artifacts. `make verify` deliberately uses a fresh root and is the stronger
product-development gate.

### Preview limitations

- Only the compiled-in `github.duckdb_login_search_page` and
  `github.authenticated_user` relations exist. `example.items` was removed in
  `0.3.0`.
- The relation represents one fixed public response page, not every GitHub user
  or every matching search result. Public row identity, order, and availability
  are not product guarantees.
- Authentication is limited to one explicit temporary `duckdb_api/config`
  bearer secret for `github.authenticated_user`. There is no persistent or
  environment provider, implicit selection, OAuth, caller-selected URL or
  header, pagination, retry, caching, provider expansion, GraphQL execution,
  or custom explain surface.
- Connector YAML/package loading, registries, signing, binary publication,
  automatic installation, and update support remain excluded.
- The `duckdb_api/draft` connector shape is design material, not a stable
  authoring contract or public native ABI.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for repository and commit conventions.

## License

This project is licensed under the [MIT License](LICENSE).
