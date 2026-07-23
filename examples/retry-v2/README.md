# Bounded retry v2 package example

This package demonstrates the smallest `duckdb_api/v2` REST relation that opts
into bounded replay-safe retry. Load the directory with
`duckdb_api_load_connector(package_root := '/absolute/path/to/examples/retry-v2')`.

The example endpoint is illustrative and is not contacted by repository tests.
V2 does not make an operation retryable by itself: the compiler must prove the
complete operation is a replayable read, and Runtime may retry only before the
current page is accepted or exposed.
