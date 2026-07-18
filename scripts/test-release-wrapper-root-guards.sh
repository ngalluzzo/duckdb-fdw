#!/usr/bin/env bash

set -euo pipefail

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "${REPOSITORY_ROOT}/scripts/lib/release-common.sh"
readonly TEMP_ROOT="$(python3 -I -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' \
    "$(mktemp -d "${TMPDIR:-/tmp}/duckdb-api-release-root-guards.XXXXXX")")"
trap 'rm -rf "${TEMP_ROOT}"' EXIT

expect_failure() {
    local label="$1"
    local expected="$2"
    shift 2
    local output
    if output="$("$@" 2>&1)"; then
        echo "release root canary unexpectedly passed: ${label}" >&2
        exit 1
    fi
    if [[ "${output}" != *"${expected}"* ]]; then
        echo "release root canary failed for the wrong reason: ${label}" >&2
        echo "${output}" >&2
        exit 1
    fi
    echo "release root canary passed: ${label}"
}

mkdir -p "${TEMP_ROOT}/existing-build" "${TEMP_ROOT}/existing-output"
expect_failure stale-build \
    "release build root must not already exist: ${TEMP_ROOT}/existing-build" \
    "${REPOSITORY_ROOT}/scripts/run-0.1-release-gate.sh" \
    "${TEMP_ROOT}/missing/../existing-build" "${TEMP_ROOT}/new-output"
expect_failure stale-output \
    "release output root must not already exist: ${TEMP_ROOT}/existing-output" \
    "${REPOSITORY_ROOT}/scripts/run-0.1-release-gate.sh" \
    "${TEMP_ROOT}/new-build" "${TEMP_ROOT}/missing/../existing-output"

expect_failure build-inside-tracked-source \
    "release build root inside the repository must be under ${REPOSITORY_ROOT}/.build" \
    "${REPOSITORY_ROOT}/scripts/run-0.1-release-gate.sh" \
    "${REPOSITORY_ROOT}/src/generated-release-build" "${TEMP_ROOT}/safe-output"
expect_failure output-inside-tracked-source \
    "release output root inside the repository must be under ${REPOSITORY_ROOT}/.build" \
    "${REPOSITORY_ROOT}/scripts/run-0.1-release-gate.sh" \
    "${TEMP_ROOT}/safe-build" "${REPOSITORY_ROOT}/test/generated-release-output"
expect_failure overlapping-roots \
    "release build root and release output root must not overlap" \
    "${REPOSITORY_ROOT}/scripts/run-0.1-release-gate.sh" \
    "${TEMP_ROOT}/overlap" "${TEMP_ROOT}/overlap/evidence"

echo "release wrapper root guard canaries passed"
