#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 2 ]]; then
    echo "usage: sanitizer-source-guard.sh EXPECTED_COMMIT VERIFIED_BASE_IMAGE" >&2
    exit 2
fi

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly EXPECTED_COMMIT="$1"
readonly VERIFIED_IMAGE="$2"
readonly REQUIRED_IMAGE='docker.io/library/ubuntu:24.04@sha256:4fbb8e6a8395de5a7550b33509421a2bafbc0aab6c06ba2cef9ebffbc7092d90'

if [[ "${VERIFIED_IMAGE}" != "${REQUIRED_IMAGE}" ]]; then
    echo "sanitizer base-image identity is not the release pin" >&2
    exit 1
fi
if [[ "$(git -C "${REPOSITORY_ROOT}" rev-parse HEAD)" != "${EXPECTED_COMMIT}" ]]; then
    echo "sanitizer source commit does not match the verified launcher identity" >&2
    exit 1
fi
if [[ -n "$(git -C "${REPOSITORY_ROOT}" status --porcelain --untracked-files=all)" ]]; then
    echo "sanitizer source worktree is not clean" >&2
    exit 1
fi
"${REPOSITORY_ROOT}/scripts/verify-source-identities.py" >/dev/null

echo "sanitizer source and image guard passed"
