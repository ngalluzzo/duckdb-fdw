#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    echo "usage: release-source-guard.sh NEW_BUILD_ROOT" >&2
    exit 2
fi

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "${REPOSITORY_ROOT}/scripts/lib/release-common.sh"
readonly BUILD_ROOT="$(release_resolve_path "$1")"
readonly EXPECTED_TAG="v0.1.0"

release_require_safe_generated_root "${REPOSITORY_ROOT}" "release build root" "${BUILD_ROOT}"
release_require_new_root "release build root" "${BUILD_ROOT}"
if [[ -n "$(git -C "${REPOSITORY_ROOT}" status --porcelain --untracked-files=all)" ]]; then
    echo "release source worktree is not clean" >&2
    exit 1
fi
if [[ -n "$(git -C "${REPOSITORY_ROOT}" ls-files --others --ignored --exclude-standard -- \
    src test fixtures CMakeLists.txt Makefile extension_config.cmake)" ]]; then
    echo "release source contains ignored bytes in build-input paths" >&2
    exit 1
fi
if ! git -C "${REPOSITORY_ROOT}" rev-parse --verify --quiet "refs/tags/${EXPECTED_TAG}^{commit}" >/dev/null; then
    echo "release tag does not exist: ${EXPECTED_TAG}" >&2
    exit 1
fi
readonly HEAD_COMMIT="$(git -C "${REPOSITORY_ROOT}" rev-parse HEAD)"
readonly TAG_COMMIT="$(git -C "${REPOSITORY_ROOT}" rev-parse "${EXPECTED_TAG}^{commit}")"
if [[ "${HEAD_COMMIT}" != "${TAG_COMMIT}" ]]; then
    echo "release tag and HEAD are not identical" >&2
    exit 1
fi
readonly HEAD_TREE="$(git -C "${REPOSITORY_ROOT}" rev-parse 'HEAD^{tree}')"
readonly TAG_TREE="$(git -C "${REPOSITORY_ROOT}" rev-parse "${EXPECTED_TAG}^{tree}")"
if [[ "${HEAD_TREE}" != "${TAG_TREE}" ]]; then
    echo "release tag and HEAD trees are not identical" >&2
    exit 1
fi
python3 -I "${REPOSITORY_ROOT}/scripts/verify-source-identities.py" >/dev/null

echo "release source guard passed"
echo "source_commit=${HEAD_COMMIT}"
echo "source_tree=${HEAD_TREE}"
echo "source_tag=${EXPECTED_TAG}"
