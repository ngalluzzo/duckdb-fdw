#!/usr/bin/env bash

set -euo pipefail

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/duckdb-api-sanitizer-guards.XXXXXX")"
readonly IMAGE='docker.io/library/ubuntu:24.04@sha256:4fbb8e6a8395de5a7550b33509421a2bafbc0aab6c06ba2cef9ebffbc7092d90'
trap 'rm -rf "${TEMP_ROOT}"' EXIT

expect_failure() {
    local label="$1"
    shift
    if "$@" >/dev/null 2>&1; then
        echo "negative sanitizer canary unexpectedly passed: ${label}" >&2
        exit 1
    fi
    echo "negative sanitizer canary passed: ${label}"
}

git clone --quiet --local "${REPOSITORY_ROOT}" "${TEMP_ROOT}/clean"
readonly COMMIT="$(git -C "${TEMP_ROOT}/clean" rev-parse HEAD)"
"${TEMP_ROOT}/clean/scripts/sanitizer-source-guard.sh" "${COMMIT}" "${IMAGE}" >/dev/null

git clone --quiet --local "${REPOSITORY_ROOT}" "${TEMP_ROOT}/dirty"
printf '\n' >>"${TEMP_ROOT}/dirty/README.md"
expect_failure dirty-source "${TEMP_ROOT}/dirty/scripts/sanitizer-source-guard.sh" "${COMMIT}" "${IMAGE}"
expect_failure wrong-commit "${TEMP_ROOT}/clean/scripts/sanitizer-source-guard.sh" \
    '0000000000000000000000000000000000000000' "${IMAGE}"
expect_failure wrong-image "${TEMP_ROOT}/clean/scripts/sanitizer-source-guard.sh" "${COMMIT}" \
    'docker.io/library/ubuntu:24.04@sha256:wrong'

echo "sanitizer source guard canaries passed"
