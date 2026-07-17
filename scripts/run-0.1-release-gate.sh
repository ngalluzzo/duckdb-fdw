#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 2 ]]; then
    echo "usage: run-0.1-release-gate.sh NEW_BUILD_ROOT NEW_OUTPUT_ROOT" >&2
    exit 2
fi

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BUILD_ROOT="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$1")"
readonly OUTPUT_ROOT="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$2")"
readonly BUILT_ARTIFACT="${BUILD_ROOT}/extension-template/build/release/extension/duckdb_api/duckdb_api.duckdb_extension"
readonly SELECTED_ARTIFACT="${OUTPUT_ROOT}/duckdb_api.duckdb_extension"
readonly BEHAVIOR="${BUILD_ROOT}/behavior.json"
readonly MANIFEST="${OUTPUT_ROOT}/manifest/manifest.json"
readonly MANIFEST_ANCHOR="${OUTPUT_ROOT}/manifest/manifest.sha256"

if [[ -e "${OUTPUT_ROOT}" ]]; then
    echo "release output root must not already exist: ${OUTPUT_ROOT}" >&2
    exit 1
fi
"${REPOSITORY_ROOT}/scripts/release-source-guard.sh" "${BUILD_ROOT}"
"${REPOSITORY_ROOT}/scripts/test-release-guards.sh"
"${REPOSITORY_ROOT}/scripts/test-native-product-root-guard.sh"
"${REPOSITORY_ROOT}/scripts/test-release-wrapper-root-guards.sh"
"${REPOSITORY_ROOT}/scripts/run-native-product-tests.sh" "${BUILD_ROOT}" release

readonly MISMATCH_ENV="${BUILD_ROOT}/python-1.5.3-mismatch"
python3 -m venv "${MISMATCH_ENV}"
"${MISMATCH_ENV}/bin/python3" -m pip install --disable-pip-version-check --no-deps \
    --require-hashes -r "${REPOSITORY_ROOT}/test/python/requirements-mismatch-macos-py314.txt"
"${MISMATCH_ENV}/bin/python3" "${REPOSITORY_ROOT}/test/python/mismatched_host.py" "${BUILT_ARTIFACT}"

"${BUILD_ROOT}/python-1.5.4/bin/python3" \
    "${REPOSITORY_ROOT}/test/python/artifact_contract.py" "${BUILT_ARTIFACT}" >"${BEHAVIOR}"
mkdir -p "${OUTPUT_ROOT}"
cp "${BUILT_ARTIFACT}" "${SELECTED_ARTIFACT}"
"${REPOSITORY_ROOT}/scripts/write-release-manifest.py" \
    "${REPOSITORY_ROOT}" "${BUILD_ROOT}" "${SELECTED_ARTIFACT}" "${BEHAVIOR}" "${MANIFEST}"
shasum -a 256 "${MANIFEST}" >"${MANIFEST_ANCHOR}"
"${REPOSITORY_ROOT}/scripts/verify-loadable-inventory.sh" "${SELECTED_ARTIFACT}"
"${REPOSITORY_ROOT}/scripts/verify-release-manifest.py" \
    "${REPOSITORY_ROOT}" "${MANIFEST}" "${SELECTED_ARTIFACT}" "${MANIFEST_ANCHOR}"

readonly CANARY_ROOT="${BUILD_ROOT}/tamper-canaries"
mkdir -p "${CANARY_ROOT}/artifact" "${CANARY_ROOT}/manifest"
cp "${SELECTED_ARTIFACT}" "${CANARY_ROOT}/artifact/duckdb_api.duckdb_extension"
cp "${MANIFEST}" "${CANARY_ROOT}/artifact/manifest.json"
cp "${MANIFEST_ANCHOR}" "${CANARY_ROOT}/artifact/manifest.sha256"
printf 'tamper' >>"${CANARY_ROOT}/artifact/duckdb_api.duckdb_extension"
if "${REPOSITORY_ROOT}/scripts/verify-release-manifest.py" "${REPOSITORY_ROOT}" \
    "${CANARY_ROOT}/artifact/manifest.json" "${CANARY_ROOT}/artifact/duckdb_api.duckdb_extension" \
    "${CANARY_ROOT}/artifact/manifest.sha256" >/dev/null 2>&1; then
    echo "artifact tamper canary unexpectedly passed" >&2
    exit 1
fi
cp "${MANIFEST}" "${CANARY_ROOT}/manifest/manifest.json"
cp "${MANIFEST_ANCHOR}" "${CANARY_ROOT}/manifest/manifest.sha256"
cp "${SELECTED_ARTIFACT}" "${CANARY_ROOT}/manifest/duckdb_api.duckdb_extension"
printf ' ' >>"${CANARY_ROOT}/manifest/manifest.json"
if "${REPOSITORY_ROOT}/scripts/verify-release-manifest.py" "${REPOSITORY_ROOT}" \
    "${CANARY_ROOT}/manifest/manifest.json" "${CANARY_ROOT}/manifest/duckdb_api.duckdb_extension" \
    "${CANARY_ROOT}/manifest/manifest.sha256" >/dev/null 2>&1; then
    echo "manifest tamper canary unexpectedly passed" >&2
    exit 1
fi
mkdir -p "${CANARY_ROOT}/reanchored-manifest"
cp "${MANIFEST}" "${CANARY_ROOT}/reanchored-manifest/manifest.json"
cp "${SELECTED_ARTIFACT}" "${CANARY_ROOT}/reanchored-manifest/duckdb_api.duckdb_extension"
python3 - "${CANARY_ROOT}/reanchored-manifest/manifest.json" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
manifest = json.loads(path.read_text())
manifest["public_contract"]["rows"][0][1] = "tampered"
path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
PY
shasum -a 256 "${CANARY_ROOT}/reanchored-manifest/manifest.json" > \
    "${CANARY_ROOT}/reanchored-manifest/manifest.sha256"
if "${REPOSITORY_ROOT}/scripts/verify-release-manifest.py" "${REPOSITORY_ROOT}" \
    "${CANARY_ROOT}/reanchored-manifest/manifest.json" \
    "${CANARY_ROOT}/reanchored-manifest/duckdb_api.duckdb_extension" \
    "${CANARY_ROOT}/reanchored-manifest/manifest.sha256" >/dev/null 2>&1; then
    echo "reanchored manifest tamper canary unexpectedly passed" >&2
    exit 1
fi

chmod 0444 "${SELECTED_ARTIFACT}" "${MANIFEST}" "${MANIFEST_ANCHOR}"
echo "0.1.0 product-cell release gate passed"
echo "artifact=${SELECTED_ARTIFACT}"
echo "manifest=${MANIFEST}"
