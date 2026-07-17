#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    echo "usage: release-source-guard.sh NEW_BUILD_ROOT" >&2
    exit 2
fi

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BUILD_ROOT="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$1")"
readonly EXPECTED_TAG="v0.1.0"

if [[ -e "${BUILD_ROOT}" ]]; then
    echo "release build root must not already exist: ${BUILD_ROOT}" >&2
    exit 1
fi
if [[ -n "$(git -C "${REPOSITORY_ROOT}" status --porcelain --untracked-files=all)" ]]; then
    echo "release source worktree is not clean" >&2
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
"${REPOSITORY_ROOT}/scripts/verify-source-identities.py" >/dev/null

echo "release source guard passed"
echo "source_commit=${HEAD_COMMIT}"
echo "source_tag=${EXPECTED_TAG}"
