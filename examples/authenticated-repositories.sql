-- The result is a mutable duplicate-preserving bag. This privacy-safe live
-- example records schema and aggregate count only; use a local ORDER BY when
-- displaying rows in an interactive query. A late page or budget failure
-- fails the statement instead of returning a complete-looking partial count.
DESCRIBE SELECT id, full_name, private, fork, archived
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'authenticated_repositories',
    secret := 'github_default'
);

SELECT count(*) AS repository_count
FROM duckdb_api_scan(
    connector := 'github',
    relation := 'authenticated_repositories',
    secret := 'github_default'
);
