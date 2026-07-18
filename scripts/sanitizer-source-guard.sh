#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    echo "usage: sanitizer-source-guard.sh NEW_BUILD_ROOT" >&2
    exit 2
fi

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "${REPOSITORY_ROOT}/scripts/lib/release-common.sh"
readonly BUILD_ROOT="$(release_resolve_path "$1")"

# Image provenance belongs to the outer Docker launcher. The inner source guard
# accepts no caller-authored commit or image assertions; it measures the exact
# detached, tagged source mounted into the container.
"${REPOSITORY_ROOT}/scripts/release-source-guard.sh" "${BUILD_ROOT}" >/dev/null
echo "sanitizer source guard passed"
echo "source_commit=$(git -C "${REPOSITORY_ROOT}" rev-parse HEAD)"
echo "source_tree=$(git -C "${REPOSITORY_ROOT}" rev-parse 'HEAD^{tree}')"
