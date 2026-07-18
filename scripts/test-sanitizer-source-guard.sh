#!/usr/bin/env bash

set -euo pipefail

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
readonly TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/duckdb-api-sanitizer-guards.XXXXXX")"
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
git -C "${TEMP_ROOT}/clean" tag --force v0.1.0 HEAD
"${TEMP_ROOT}/clean/scripts/sanitizer-source-guard.sh" "${TEMP_ROOT}/clean-build" >/dev/null

git clone --quiet --local "${REPOSITORY_ROOT}" "${TEMP_ROOT}/dirty"
git -C "${TEMP_ROOT}/dirty" tag --force v0.1.0 HEAD
printf '\n' >>"${TEMP_ROOT}/dirty/README.md"
expect_failure dirty-source "${TEMP_ROOT}/dirty/scripts/sanitizer-source-guard.sh" \
    "${TEMP_ROOT}/dirty-build"

git clone --quiet --local "${REPOSITORY_ROOT}" "${TEMP_ROOT}/tag-mismatch"
git -C "${TEMP_ROOT}/tag-mismatch" tag --force v0.1.0 HEAD^
expect_failure tag-mismatch "${TEMP_ROOT}/tag-mismatch/scripts/sanitizer-source-guard.sh" \
    "${TEMP_ROOT}/tag-mismatch-build"

git clone --quiet --local "${REPOSITORY_ROOT}" "${TEMP_ROOT}/ignored-input"
git -C "${TEMP_ROOT}/ignored-input" tag --force v0.1.0 HEAD
printf 'src/ignored-shadow.hpp\n' >>"${TEMP_ROOT}/ignored-input/.git/info/exclude"
touch "${TEMP_ROOT}/ignored-input/src/ignored-shadow.hpp"
expect_failure ignored-build-input "${TEMP_ROOT}/ignored-input/scripts/sanitizer-source-guard.sh" \
    "${TEMP_ROOT}/ignored-input-build"

echo "sanitizer source guard canaries passed"
