SELECT extension_name, extension_version, loaded, installed, install_mode
FROM duckdb_extensions()
WHERE extension_name = 'duckdb_api';

-- This relation is the zero-to-three rows in one fixed public GitHub search
-- response page. ORDER BY is evaluated by DuckDB; public row identity and
-- service order are intentionally not part of the preview contract.
SELECT id, login, site_admin
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'duckdb_login_search_page'
)
ORDER BY login, id;
