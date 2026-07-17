#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    echo "usage: reproduce-0.1-release.sh NEW_OUTPUT_ROOT" >&2
    exit 2
fi

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly OUTPUT_ROOT="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$1")"
if [[ -e "${OUTPUT_ROOT}" ]]; then
    echo "reproduction output root must not already exist: ${OUTPUT_ROOT}" >&2
    exit 1
fi
mkdir -p "${OUTPUT_ROOT}"

for copy in one two; do
    checkout="${OUTPUT_ROOT}/checkout-${copy}"
    git clone --quiet --local "${REPOSITORY_ROOT}" "${checkout}"
    git -C "${checkout}" checkout --quiet v0.1.0
    HOME="${OUTPUT_ROOT}/home-${copy}" XDG_CACHE_HOME="${OUTPUT_ROOT}/cache-${copy}" \
        "${checkout}/scripts/run-0.1-release-gate.sh" \
        "${OUTPUT_ROOT}/build-${copy}" "${OUTPUT_ROOT}/evidence-${copy}"
done

python3 - "${OUTPUT_ROOT}/evidence-one/manifest/manifest.json" \
    "${OUTPUT_ROOT}/evidence-two/manifest/manifest.json" <<'PY'
import json
import pathlib
import sys

first = json.loads(pathlib.Path(sys.argv[1]).read_text())
second = json.loads(pathlib.Path(sys.argv[2]).read_text())
semantic_keys = ("source", "project", "dependencies", "toolchain", "content", "public_contract", "public_contract_sha256")
left = {key: first[key] for key in semantic_keys}
right = {key: second[key] for key in semantic_keys}
for value in (left, right):
    value["toolchain"].pop("compiler_path", None)
if left != right:
    raise SystemExit("cache-empty workspaces produced different semantic or behavior identities")
print("two cache-empty workspaces produced matching semantic and behavior identities")
PY
