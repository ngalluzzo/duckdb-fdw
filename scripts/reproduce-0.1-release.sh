#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    echo "usage: reproduce-0.1-release.sh NEW_OUTPUT_ROOT" >&2
    exit 2
fi

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "${REPOSITORY_ROOT}/scripts/lib/release-common.sh"
readonly OUTPUT_ROOT="$(release_resolve_path "$1")"
release_require_safe_generated_root "${REPOSITORY_ROOT}" "reproduction output root" "${OUTPUT_ROOT}"
release_require_new_root "reproduction output root" "${OUTPUT_ROOT}"
mkdir -p "${OUTPUT_ROOT}"
readonly PYTHON_DIR="$(dirname "$(command -v python3)")"
readonly GIT_DIR="$(dirname "$(command -v git)")"
readonly CLEAN_PATH="${PYTHON_DIR}:${GIT_DIR}:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"

for copy in one two; do
    checkout="${OUTPUT_ROOT}/checkout-${copy}"
    git clone --quiet --no-hardlinks "${REPOSITORY_ROOT}" "${checkout}"
    git -C "${checkout}" checkout --quiet v0.1.0
    mkdir -p "${OUTPUT_ROOT}/home-${copy}" "${OUTPUT_ROOT}/cache-${copy}" \
        "${OUTPUT_ROOT}/tmp-${copy}"
    env -i HOME="${OUTPUT_ROOT}/home-${copy}" XDG_CACHE_HOME="${OUTPUT_ROOT}/cache-${copy}" \
        TMPDIR="${OUTPUT_ROOT}/tmp-${copy}" PATH="${CLEAN_PATH}" LC_ALL=C \
        "${checkout}/scripts/run-0.1-release-gate.sh" \
        "${OUTPUT_ROOT}/build-${copy}" "${OUTPUT_ROOT}/evidence-${copy}" \
        2>&1 | tee "${OUTPUT_ROOT}/product-gate-${copy}.log"
done

python3 -I - "${OUTPUT_ROOT}/evidence-one/manifest/manifest.json" \
    "${OUTPUT_ROOT}/evidence-two/manifest/manifest.json" <<'PY'
import json
import pathlib
import sys

first = json.loads(pathlib.Path(sys.argv[1]).read_text())
second = json.loads(pathlib.Path(sys.argv[2]).read_text())
semantic_keys = (
    "source",
    "project",
    "dependencies",
    "toolchain",
    "content",
    "public_contract",
    "public_contract_sha256",
    "compatibility",
)
left = {key: first[key] for key in semantic_keys}
right = {key: second[key] for key in semantic_keys}
for value in (left, right):
    value["toolchain"].pop("compiler_path", None)
if left != right:
    raise SystemExit("cache-empty workspaces produced different semantic or behavior identities")
print("two cache-empty workspaces produced matching semantic and behavior identities")
PY
