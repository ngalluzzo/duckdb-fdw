#!/usr/bin/env bash

set -euo pipefail

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly TEMP_ROOT="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' \
    "$(mktemp -d "${TMPDIR:-/tmp}/duckdb-api-product-root-guard.XXXXXX")")"
trap 'rm -rf "${TEMP_ROOT}"' EXIT

mkdir -p "${TEMP_ROOT}/existing"
readonly DOT_SEGMENT_ROOT="${TEMP_ROOT}/missing/../existing"
if output="$("${REPOSITORY_ROOT}/scripts/run-native-product-tests.sh" "${DOT_SEGMENT_ROOT}" debug 2>&1)"; then
    echo "dot-segment stale-root canary unexpectedly passed" >&2
    exit 1
fi
readonly EXPECTED="build root must not already exist: ${TEMP_ROOT}/existing"
if [[ "${output}" != *"${EXPECTED}"* ]]; then
    echo "dot-segment stale-root canary failed for the wrong reason" >&2
    echo "${output}" >&2
    exit 1
fi

echo "native product root guard canary passed"
