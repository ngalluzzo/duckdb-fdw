#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 2 ]]; then
    echo "usage: run-0.1-release-gate.sh NEW_BUILD_ROOT NEW_OUTPUT_ROOT" >&2
    exit 2
fi

readonly CALLER_REPOSITORY="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "${CALLER_REPOSITORY}/scripts/lib/release-common.sh"
readonly BUILD_ROOT="$(release_resolve_path "$1")"
readonly OUTPUT_ROOT="$(release_resolve_path "$2")"
readonly SOURCE_SNAPSHOT="${BUILD_ROOT}/release-source"
readonly PRODUCT_BUILD="${BUILD_ROOT}/product"
readonly SELECTED_ARTIFACT="${OUTPUT_ROOT}/duckdb_api.duckdb_extension"
readonly BEHAVIOR="${BUILD_ROOT}/behavior.json"
readonly MISMATCH_BEHAVIOR="${BUILD_ROOT}/mismatch-behavior.json"
readonly MANIFEST="${OUTPUT_ROOT}/manifest/manifest.json"
readonly MANIFEST_ANCHOR="${OUTPUT_ROOT}/manifest/manifest.sha256"

release_require_safe_generated_root "${CALLER_REPOSITORY}" "release build root" "${BUILD_ROOT}"
release_require_safe_generated_root "${CALLER_REPOSITORY}" "release output root" "${OUTPUT_ROOT}"
release_require_new_root "release build root" "${BUILD_ROOT}"
release_require_new_root "release output root" "${OUTPUT_ROOT}"
release_require_disjoint_roots "release build root" "${BUILD_ROOT}" \
    "release output root" "${OUTPUT_ROOT}"

"${CALLER_REPOSITORY}/scripts/release-source-guard.sh" "${BUILD_ROOT}"
readonly SOURCE_COMMIT="$(git -C "${CALLER_REPOSITORY}" rev-parse HEAD)"
readonly SOURCE_TREE="$(git -C "${CALLER_REPOSITORY}" rev-parse 'HEAD^{tree}')"
mkdir -p "${BUILD_ROOT}"
release_materialize_snapshot "${CALLER_REPOSITORY}" "${SOURCE_COMMIT}" "${SOURCE_SNAPSHOT}"
readonly REPOSITORY_ROOT="${SOURCE_SNAPSHOT}"
if [[ "$(git -C "${REPOSITORY_ROOT}" rev-parse 'HEAD^{tree}')" != "${SOURCE_TREE}" ]]; then
    echo "release source snapshot tree drifted" >&2
    exit 1
fi
"${REPOSITORY_ROOT}/scripts/release-source-guard.sh" "${PRODUCT_BUILD}"
"${REPOSITORY_ROOT}/scripts/test-release-guards.sh"
"${REPOSITORY_ROOT}/scripts/test-native-product-root-guard.sh"
"${REPOSITORY_ROOT}/scripts/test-release-wrapper-root-guards.sh"
"${REPOSITORY_ROOT}/scripts/run-native-product-tests.sh" "${PRODUCT_BUILD}" release
"${REPOSITORY_ROOT}/scripts/test-build-pin-binding.sh" \
    "${PRODUCT_BUILD}/extension-template"

readonly BUILT_ARTIFACT="${PRODUCT_BUILD}/extension-template/build/release/extension/duckdb_api/duckdb_api.duckdb_extension"
mkdir -p "${OUTPUT_ROOT}"
cp "${BUILT_ARTIFACT}" "${SELECTED_ARTIFACT}"
chmod 0444 "${SELECTED_ARTIFACT}"
readonly SELECTED_SHA256="$(shasum -a 256 "${SELECTED_ARTIFACT}" | awk '{print $1}')"

# Both supported- and mismatched-host oracles exercise the exact bytes selected
# for publication, under isolated Python and pip state.
readonly MISMATCH_ENV="${PRODUCT_BUILD}/python-1.5.3-mismatch"
readonly CLEAN_HOME="${BUILD_ROOT}/host-home"
readonly CLEAN_TMP="${BUILD_ROOT}/host-tmp"
readonly CLEAN_CACHE="${BUILD_ROOT}/host-cache"
readonly PYTHON_DIR="$(dirname "$(command -v python3)")"
mkdir -p "${CLEAN_HOME}" "${CLEAN_TMP}" "${CLEAN_CACHE}"
env -i HOME="${CLEAN_HOME}" TMPDIR="${CLEAN_TMP}" XDG_CACHE_HOME="${CLEAN_CACHE}" \
    PATH="${PYTHON_DIR}:/usr/bin:/bin:/usr/sbin:/sbin" \
    python3 -I -m venv "${MISMATCH_ENV}"
env -i HOME="${CLEAN_HOME}" TMPDIR="${CLEAN_TMP}" XDG_CACHE_HOME="${CLEAN_CACHE}" \
    PATH="${MISMATCH_ENV}/bin:${PYTHON_DIR}:/usr/bin:/bin:/usr/sbin:/sbin" \
    PIP_CONFIG_FILE=/dev/null \
    "${MISMATCH_ENV}/bin/python3" -I -m pip install --disable-pip-version-check \
        --no-deps --no-cache-dir --require-hashes \
        -r "${REPOSITORY_ROOT}/test/python/requirements-mismatch-macos-py314.txt"
env -i HOME="${CLEAN_HOME}" TMPDIR="${CLEAN_TMP}" XDG_CACHE_HOME="${CLEAN_CACHE}" \
    PATH="${MISMATCH_ENV}/bin:${PYTHON_DIR}:/usr/bin:/bin:/usr/sbin:/sbin" \
    "${MISMATCH_ENV}/bin/python3" -I \
        "${REPOSITORY_ROOT}/test/python/mismatched_host.py" "${SELECTED_ARTIFACT}" \
        >"${MISMATCH_BEHAVIOR}"
env -i HOME="${CLEAN_HOME}" TMPDIR="${CLEAN_TMP}" XDG_CACHE_HOME="${CLEAN_CACHE}" \
    PATH="${PRODUCT_BUILD}/python-1.5.4/bin:${PYTHON_DIR}:/usr/bin:/bin:/usr/sbin:/sbin" \
    "${PRODUCT_BUILD}/python-1.5.4/bin/python3" -I \
        "${REPOSITORY_ROOT}/test/python/artifact_contract.py" "${SELECTED_ARTIFACT}" \
        >"${BEHAVIOR}"
if [[ "$(shasum -a 256 "${SELECTED_ARTIFACT}" | awk '{print $1}')" != "${SELECTED_SHA256}" ]]; then
    echo "selected release artifact changed while behavior was tested" >&2
    exit 1
fi

# Recheck the detached snapshot after all build and load operations. The build
# lives outside it, so any source mutation is observable here.
"${REPOSITORY_ROOT}/scripts/release-source-guard.sh" "${BUILD_ROOT}/final-source-guard"
python3 -I "${REPOSITORY_ROOT}/scripts/write-release-manifest.py" \
    "${REPOSITORY_ROOT}" "${PRODUCT_BUILD}" "${SELECTED_ARTIFACT}" "${BEHAVIOR}" \
    "${MISMATCH_BEHAVIOR}" "${MANIFEST}"
shasum -a 256 "${MANIFEST}" >"${MANIFEST_ANCHOR}"
"${REPOSITORY_ROOT}/scripts/verify-loadable-inventory.sh" "${SELECTED_ARTIFACT}"
python3 -I "${REPOSITORY_ROOT}/scripts/verify-release-manifest.py" \
    "${REPOSITORY_ROOT}" "${MANIFEST}" "${SELECTED_ARTIFACT}" "${MANIFEST_ANCHOR}"

readonly CANARY_ROOT="${BUILD_ROOT}/tamper-canaries"
mkdir -p "${CANARY_ROOT}/artifact" "${CANARY_ROOT}/manifest"
cp "${SELECTED_ARTIFACT}" "${CANARY_ROOT}/artifact/duckdb_api.duckdb_extension"
cp "${MANIFEST}" "${CANARY_ROOT}/artifact/manifest.json"
cp "${MANIFEST_ANCHOR}" "${CANARY_ROOT}/artifact/manifest.sha256"
chmod u+w "${CANARY_ROOT}/artifact/duckdb_api.duckdb_extension"
printf 'tamper' >>"${CANARY_ROOT}/artifact/duckdb_api.duckdb_extension"
if python3 -I "${REPOSITORY_ROOT}/scripts/verify-release-manifest.py" "${REPOSITORY_ROOT}" \
    "${CANARY_ROOT}/artifact/manifest.json" "${CANARY_ROOT}/artifact/duckdb_api.duckdb_extension" \
    "${CANARY_ROOT}/artifact/manifest.sha256" >/dev/null 2>&1; then
    echo "artifact tamper canary unexpectedly passed" >&2
    exit 1
fi
cp "${MANIFEST}" "${CANARY_ROOT}/manifest/manifest.json"
cp "${MANIFEST_ANCHOR}" "${CANARY_ROOT}/manifest/manifest.sha256"
cp "${SELECTED_ARTIFACT}" "${CANARY_ROOT}/manifest/duckdb_api.duckdb_extension"
printf ' ' >>"${CANARY_ROOT}/manifest/manifest.json"
if python3 -I "${REPOSITORY_ROOT}/scripts/verify-release-manifest.py" "${REPOSITORY_ROOT}" \
    "${CANARY_ROOT}/manifest/manifest.json" "${CANARY_ROOT}/manifest/duckdb_api.duckdb_extension" \
    "${CANARY_ROOT}/manifest/manifest.sha256" >/dev/null 2>&1; then
    echo "manifest tamper canary unexpectedly passed" >&2
    exit 1
fi
mkdir -p "${CANARY_ROOT}/reanchored-manifest"
cp "${MANIFEST}" "${CANARY_ROOT}/reanchored-manifest/manifest.json"
cp "${SELECTED_ARTIFACT}" "${CANARY_ROOT}/reanchored-manifest/duckdb_api.duckdb_extension"
python3 -I - "${CANARY_ROOT}/reanchored-manifest/manifest.json" <<'PY'
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
if python3 -I "${REPOSITORY_ROOT}/scripts/verify-release-manifest.py" "${REPOSITORY_ROOT}" \
    "${CANARY_ROOT}/reanchored-manifest/manifest.json" \
    "${CANARY_ROOT}/reanchored-manifest/duckdb_api.duckdb_extension" \
    "${CANARY_ROOT}/reanchored-manifest/manifest.sha256" >/dev/null 2>&1; then
    echo "reanchored manifest tamper canary unexpectedly passed" >&2
    exit 1
fi

if [[ "$(shasum -a 256 "${SELECTED_ARTIFACT}" | awk '{print $1}')" != "${SELECTED_SHA256}" ]]; then
    echo "selected release artifact changed after manifest anchoring" >&2
    exit 1
fi
chmod 0444 "${SELECTED_ARTIFACT}" "${MANIFEST}" "${MANIFEST_ANCHOR}"
echo "0.1.0 product-cell release gate passed"
echo "artifact=${SELECTED_ARTIFACT}"
echo "manifest=${MANIFEST}"
