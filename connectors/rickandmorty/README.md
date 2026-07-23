# Rick and Morty connector package

This `duckdb_api/v1` package exposes two read-only relations against the free,
public [Rick and Morty API](https://rickandmortyapi.com/). It is the
repository's second, independently authored example package, kept
deliberately unlike [`connectors/github`](../github): every relation here is
anonymous (the upstream API accepts no credential of any kind), and the
upstream response envelope, host, and JSON shape are entirely different from
GitHub's.

## Relations

| Function | Authentication | Columns |
| --- | --- | --- |
| `rickandmorty_pilot_episode()` | None | `id BIGINT`, `name VARCHAR`, `air_date VARCHAR`, `episode_code VARCHAR` |
| `rickandmorty_character_search(status := ...)` | None | `id BIGINT`, `name VARCHAR`, `status VARCHAR`, `species VARCHAR`, `origin_name VARCHAR`, `episode VARCHAR[]` |

`character_search` declares one relation input, `status`, bound directly into
the upstream `status` query parameter when supplied and omitted otherwise.
Unlike `github_authenticated_repositories`'s `visibility` predicate (a WHERE-
clause pushdown mapped from the relation's own output column), `status` here
is an explicit named call argument with no predicate mapping — this package
declares no `predicates:` block.

Both relations fetch exactly one page: the upstream API's pagination is a
`next`/`prev` absolute-URL pair embedded in the response body's `info` object,
not the `Link: rel=next` header `duckdb_api/v1`'s REST pagination requires
(see `docs/CONNECTOR_SPECIFICATIONS.md`'s REST operations section). This
package intentionally does not attempt to reinterpret that pagination as
`link_next`; the mismatch is recorded as delivery-goal evidence rather than
worked around.

## Load and query

Build and load the extension, then pass the absolute path of this directory:

```sql
CALL duckdb_api_load_connector(
    package_root := '/absolute/path/to/duckdb-fdw/connectors/rickandmorty'
);

SELECT id, name, air_date, episode_code
FROM rickandmorty_pilot_episode();

SELECT id, name, status, species, episode, len(episode) AS episode_count
FROM rickandmorty_character_search(status := 'Alive');
```

`episode` preserves the upstream ordered array of episode URLs as a DuckDB
`VARCHAR[]`. An empty upstream array remains an empty list rather than NULL.

`CALL duckdb_api_reload_connector(connector := 'rickandmorty')` recompiles the
same retained package root. Reload compatibility follows package SemVer and
the normalized package contract; incompatible changes leave the active
generation unchanged.

## Validate changes

Loading compiles and validates all semantic source before publication. A
failure returns a source-located, redacted diagnostic and publishes nothing.
From the repository root, run:

```sh
make build
make test
```

See the [connector package specification](../../docs/CONNECTOR_SPECIFICATIONS.md)
for the exact source grammar, diagnostics, resource ceilings, and
compatibility rules. Semantic changes belong in `connector.yaml` or
`relations/` and require an appropriate package-version change. `README.md`
and fixtures do not enter the semantic package digest.
