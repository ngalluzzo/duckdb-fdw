SELECT extension_name, extension_version, loaded, installed, install_mode
FROM duckdb_extensions()
WHERE extension_name = 'duckdb_api';

SELECT id, name, active
FROM duckdb_api_scan(
    connector := 'example',
    relation := 'items'
)
ORDER BY id;
