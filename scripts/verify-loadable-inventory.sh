#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    echo "usage: verify-loadable-inventory.sh PATH_TO_EXTENSION" >&2
    exit 2
fi

readonly ARTIFACT="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
if [[ ! -f "${ARTIFACT}" ]]; then
    echo "extension artifact does not exist: ${ARTIFACT}" >&2
    exit 1
fi

case "$(uname -s)" in
    Darwin)
        exported="$(nm -gU "${ARTIFACT}")"
        expected='^[[:xdigit:]]+ T _duckdb_api_duckdb_cpp_init$'
        ;;
    Linux)
        exported="$(nm -D --defined-only "${ARTIFACT}")"
        expected='^[[:xdigit:]]+ T duckdb_api_duckdb_cpp_init$'
        ;;
    *)
        echo "unsupported inventory host: $(uname -s)" >&2
        exit 1
        ;;
esac

if [[ "$(printf '%s\n' "${exported}" | sed '/^[[:space:]]*$/d' | wc -l | tr -d ' ')" -ne 1 ]] ||
   ! printf '%s\n' "${exported}" | grep -Eq "${expected}"; then
    echo "loadable artifact exported an unexpected public symbol inventory:" >&2
    printf '%s\n' "${exported}" >&2
    exit 1
fi

readonly FORBIDDEN='FixtureScenario|MALFORMED|TYPE_MISMATCH|BLOCKING|UNKNOWN_FAILURE|duckdb_api_(stats|test)|top-secret|test-only-fixture|DUCKDB_API_FIXTURE_SCENARIO|DUCKDB_API_CONNECTOR_PATH'
if strings "${ARTIFACT}" | grep -E "${FORBIDDEN}" >/dev/null; then
    echo "loadable artifact contains a forbidden test-control identifier" >&2
    exit 1
fi

echo "loadable artifact public-symbol and test-control inventory passed"
