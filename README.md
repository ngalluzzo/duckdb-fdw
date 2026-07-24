# duckdb-fdw

`duckdb-fdw` is a DuckDB extension that turns declarative HTTP and GraphQL
connector packages into typed table functions. Package authors declare request,
schema, authentication, pagination, and resource policy; users query the
resulting relations with ordinary DuckDB SQL.

The project is a source-built preview. It is FDW-like in purpose, but its public
surface is DuckDB-native and it does not implement PostgreSQL's FDW API.

## Quick start

The supported development cell is Apple Silicon arm64, macOS 26.5.2 build
`25F84`, DuckDB 1.5.4, and Python 3.14. Install the Xcode Command Line Tools and
Python 3.14, then run from the repository root:

```sh
make bootstrap
make demo
```

The first run downloads pinned, checksummed build dependencies. `make demo`
builds the unsigned extension, loads the maintained package at
`connectors/github`, and runs its anonymous REST relation against GitHub. The
live service is a compatibility demonstration; deterministic local services
are the correctness oracle in the test suite.

To use the extension directly, first print the exact artifact and pinned Python
paths:

```sh
make build
make paths
```

Then load the extension and an absolute local package root before querying a
generated function:

```sql
LOAD '/absolute/path/to/duckdb_api.duckdb_extension';

CALL duckdb_api_load_connector(
    package_root := '/absolute/path/to/duckdb-fdw/connectors/github'
);

SELECT id, login, site_admin
FROM github_duckdb_login_search_page()
ORDER BY login, id;
```

An accepted package publishes one table function per relation using the name
`<connector_id>_<relation_id>`. Loading validates and compiles the complete
package before publishing anything. The returned row reports the connector,
package and spec versions, digest, relation count, and whether publication
changed.

## GitHub package

The maintained GitHub package demonstrates anonymous REST, bearer-authenticated
REST, bounded sequential Link pagination, a conservative predicate mapping,
and bounded GraphQL cursor pagination.

| Function | Authentication | Result |
| --- | --- | --- |
| `github_duckdb_login_search_page()` | None | Up to three public users |
| `github_authenticated_user(secret := ...)` | Named `duckdb_api` credential | Current user identity |
| `github_authenticated_repositories(secret := ...)` | Named `duckdb_api` credential | A bounded repository page chain |
| `github_viewer_repository_metrics(secret := ...)` | Named `duckdb_api` credential | Bounded repository metrics through GraphQL |

For authenticated relations, create an explicitly named DuckDB credential. Do
not put a token in a connector package or committed SQL. A temporary config
credential is the simplest form:

```sql
CREATE TEMPORARY SECRET github_default (
    TYPE duckdb_api,
    PROVIDER config,
    TOKEN '<short-lived-github-token>'
);

SELECT id, full_name, visibility
FROM github_authenticated_repositories(secret := 'github_default')
WHERE visibility = 'private' AND archived = FALSE
ORDER BY id DESC
LIMIT 10;
```

The extension resolves the secret only during execution and restricts it to
the package-declared authenticator, placement, and destination. Bind,
`DESCRIBE`, `EXPLAIN`, and `PREPARE` remain network-free.

The closed provider/storage combinations are:

```sql
CREATE TEMPORARY SECRET github_default (
    TYPE duckdb_api, PROVIDER environment, VARIABLE 'GITHUB_TOKEN'
);

CREATE PERSISTENT SECRET github_default IN duckdb_api (
    TYPE duckdb_api, PROVIDER config, TOKEN '<github-token>'
);

CREATE PERSISTENT SECRET github_default IN duckdb_api (
    TYPE duckdb_api, PROVIDER environment, VARIABLE 'GITHUB_TOKEN'
);
```

Use `CREATE OR REPLACE` to rotate within one storage. A new scan observes the
new revision; an active scan retains its original snapshot across every page.
Drop a persistent credential with
`DROP PERSISTENT SECRET github_default FROM duckdb_api`. The persistent config
format is owner-private and bounded but not encrypted: prefer temporary or
environment-backed credentials when plaintext at rest is unacceptable.

The example runners use a hidden interactive token prompt and create only a
temporary secret. The repository examples emit schema and aggregate or boolean
completion evidence without printing repository rows, names, cursors, request
bodies, or credentials:

```sh
/absolute/pinned_python -I examples/authenticated_repositories.py \
  /absolute/duckdb_api.duckdb_extension

/absolute/pinned_python -I examples/viewer_repository_metrics.py \
  /absolute/duckdb_api.duckdb_extension
```

The corresponding SQL is in
[`examples/authenticated-repositories.sql`](examples/authenticated-repositories.sql)
and
[`examples/viewer-repository-metrics.sql`](examples/viewer-repository-metrics.sql).
Each file contains the complete load-then-query workflow and a package-root
placeholder for interactive use.

## Package lifecycle and inspection

Reload recompiles the active connector from its retained canonical root. A
compatible changed generation is published atomically; an identical reload
reports `changed = false`; a rejected reload leaves the active generation
unchanged.

```sql
CALL duckdb_api_reload_connector(connector := 'github');

SELECT * FROM duckdb_api_loaded_connectors();
SELECT * FROM duckdb_api_loaded_relations();
SELECT * FROM duckdb_api_relation_arguments();
```

The [GitHub package guide](connectors/github/README.md) explains its relations
and author validation. The
[connector specification](docs/CONNECTOR_SPECIFICATIONS.md) defines the local
package grammar, closed validation, diagnostics, and compatibility rules.

## SQL semantics and safety

DuckDB owns relational correctness. A remote restriction is used only when the
compiled proof says it cannot remove a DuckDB-true row; DuckDB retains any
required residual predicate. Filters, projections, ordering, limits, and
offsets otherwise remain local.

Execution is bounded, sequential unless independence is proven, cancelable,
and closed under the package and host network policies. Malformed responses,
GraphQL errors, invalid page transitions, unsafe destinations, cancellation,
or resource exhaustion fail the statement instead of returning a
complete-looking partial result. Plans and active package generations are
immutable for the lifetime of a scan.

One executor-local admission service also bounds concurrent provider work,
active scans, requests, retry/rate-limit waits, buffered bytes, and decoded
rows across global, connector, destination, credential-principal, and exact
operation bulkheads. Saturated work queues only within fixed deadlines or
fails locally before transport; an independently eligible connector can still
progress. The policy is a fixed host safety floor—there is no public tuning or
circuit breaker.

## Development

The root Makefile is the supported interface:

| Command | Purpose |
| --- | --- |
| `make help` | Show commands and supported overrides |
| `make bootstrap` | Prepare or repair the pinned developer environment |
| `make build` | Incrementally build the debug extension |
| `make test` | Run native, controlled-service, SQL, artifact, and demo contracts |
| `make demo` | Run the anonymous generated GitHub relation |
| `make paths` | Print the active build, Python, CLI, and extension paths |
| `make verify` | Run the complete product suite from a fresh build root |

Use `PROFILE=release` for a release-profile developer build or
`DUCKDB_API_DEV_ROOT=/absolute/path` to isolate reusable developer state. Start
with the [source guide](src/README.md) before changing production code and
[CONTRIBUTING.md](CONTRIBUTING.md) for repository practices.

## Compatibility and limitations

- The extension is not published or signed and cannot be installed from the
  DuckDB Community repository.
- `0.9.0` supports explicit local package loading only. Package discovery,
  remote registries, installation, updates, signatures, and trust policy are
  not distribution features yet.
- Authentication supports explicitly named `duckdb_api/config` and
  `duckdb_api/environment` credentials in temporary memory or the bounded
  persistent `duckdb_api` storage. Implicit selection, OAuth, and arbitrary
  external providers are not supported.
- There are no retries, rate-limit waits, parallel page requests, resume state,
  or caching.
- Compatibility is limited to the exact source-build cell above.
- `duckdb_api_scan(...)`, the deprecated migration surface for the four
  `0.7.0` built-in GitHub relations, was removed in `0.9.0` per accepted RFC
  0012. Load a package and use its generated functions instead.

See the [0.9.0 release notes](docs/releases/0.9.0-notes.md) for the complete
product contract and [CHANGELOG.md](CHANGELOG.md) for user-visible changes.
Architecture and runtime details are in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
and [docs/RUNTIME_CONTRACTS.md](docs/RUNTIME_CONTRACTS.md).

## License

Licensed under the [MIT License](LICENSE).
