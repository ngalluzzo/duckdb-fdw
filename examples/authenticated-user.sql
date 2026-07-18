-- The Python runner creates this explicitly named temporary secret from an
-- interactive prompt. Bind stores only the name; execution resolves its
-- current in-memory value and Runtime restricts it to the fixed GET /user
-- bearer request.
SELECT id, login, site_admin
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'authenticated_user',
    secret := 'github_default'
);
