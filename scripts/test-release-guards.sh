#!/usr/bin/env bash

set -euo pipefail

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
readonly TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/duckdb-api-guards.XXXXXX")"
trap '"${CMAKE_COMMAND:-cmake}" -E remove_directory "${TEMP_ROOT}"' EXIT

clone_case() {
    local name="$1"
    local destination="${TEMP_ROOT}/${name}"
    git clone --quiet --local "${REPOSITORY_ROOT}" "${destination}"
    printf '%s\n' "${destination}"
}

expect_failure() {
    local label="$1"
    shift
    if "$@" >/dev/null 2>&1; then
        echo "negative canary unexpectedly passed: ${label}" >&2
        exit 1
    fi
    echo "negative canary passed: ${label}"
}

clean="$(clone_case clean)"
"${clean}/scripts/release-source-guard.sh" "${TEMP_ROOT}/clean-build" >/dev/null

dirty_tracked="$(clone_case dirty-tracked)"
printf '\n' >>"${dirty_tracked}/README.md"
expect_failure dirty-tracked "${dirty_tracked}/scripts/release-source-guard.sh" "${TEMP_ROOT}/dirty-tracked-build"

dirty_untracked="$(clone_case dirty-untracked)"
touch "${dirty_untracked}/unexpected.txt"
expect_failure dirty-untracked "${dirty_untracked}/scripts/release-source-guard.sh" "${TEMP_ROOT}/dirty-untracked-build"

ignored_input="$(clone_case ignored-input)"
printf 'src/ignored-shadow.hpp\n' >>"${ignored_input}/.git/info/exclude"
touch "${ignored_input}/src/ignored-shadow.hpp"
expect_failure ignored-build-input "${ignored_input}/scripts/release-source-guard.sh" \
    "${TEMP_ROOT}/ignored-input-build"

tag_mismatch="$(clone_case tag-mismatch)"
git -C "${tag_mismatch}" tag --force v0.1.0 HEAD^
expect_failure tag-mismatch "${tag_mismatch}/scripts/release-source-guard.sh" "${TEMP_ROOT}/tag-mismatch-build"

version_mismatch="$(clone_case version-mismatch)"
perl -pi -e 's/EXTENSION_VERSION "0\.1\.0"/EXTENSION_VERSION "0.1.1"/' "${version_mismatch}/extension_config.cmake"
git -C "${version_mismatch}" add extension_config.cmake
git -C "${version_mismatch}" -c user.name=canary -c user.email=canary@example.invalid \
    commit --quiet -m 'test: mutate version'
git -C "${version_mismatch}" tag --force v0.1.0 HEAD
expect_failure version-mismatch "${version_mismatch}/scripts/release-source-guard.sh" "${TEMP_ROOT}/version-mismatch-build"

fixture_mutation="$(clone_case fixture-mutation)"
printf ' ' >>"${fixture_mutation}/fixtures/example/items.json"
git -C "${fixture_mutation}" add fixtures/example/items.json
git -C "${fixture_mutation}" -c user.name=canary -c user.email=canary@example.invalid \
    commit --quiet -m 'test: mutate fixture'
git -C "${fixture_mutation}" tag --force v0.1.0 HEAD
expect_failure fixture-mutation "${fixture_mutation}/scripts/release-source-guard.sh" "${TEMP_ROOT}/fixture-mutation-build"

embedded_mutation="$(clone_case embedded-mutation)"
perl -pi -e 's/alpha/altered/' "${embedded_mutation}/src/include/duckdb_api/embedded_example.hpp"
git -C "${embedded_mutation}" add src/include/duckdb_api/embedded_example.hpp
git -C "${embedded_mutation}" -c user.name=canary -c user.email=canary@example.invalid \
    commit --quiet -m 'test: mutate embedded fixture'
git -C "${embedded_mutation}" tag --force v0.1.0 HEAD
expect_failure embedded-mutation "${embedded_mutation}/scripts/release-source-guard.sh" \
    "${TEMP_ROOT}/embedded-mutation-build"

contract_mutation="$(clone_case contract-mutation)"
perl -pi -e 's/alpha/altered/' "${contract_mutation}/release/0.1.0/public_contract.json"
git -C "${contract_mutation}" add release/0.1.0/public_contract.json
git -C "${contract_mutation}" -c user.name=canary -c user.email=canary@example.invalid \
    commit --quiet -m 'test: mutate public contract'
git -C "${contract_mutation}" tag --force v0.1.0 HEAD
expect_failure contract-mutation "${contract_mutation}/scripts/release-source-guard.sh" \
    "${TEMP_ROOT}/contract-mutation-build"

existing_root="$(clone_case existing-root)"
mkdir "${TEMP_ROOT}/existing-build"
expect_failure existing-build-root "${existing_root}/scripts/release-source-guard.sh" "${TEMP_ROOT}/existing-build"

echo "release source negative canaries passed"
