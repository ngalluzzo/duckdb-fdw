#!/usr/bin/env bash

set -euo pipefail

usage() {
    echo "usage: scripts/run-installability-trial.sh [options]" >&2
    echo "  --supported-python PATH" >&2
    echo "  --mismatch-python PATH" >&2
    echo "  --artifact PATH" >&2
    echo "  --manifest PATH" >&2
    echo "  --manifest-anchor PATH" >&2
    echo "  --verifier PATH" >&2
    echo "  --oracle PATH" >&2
    echo "  --reproduction-one PATH" >&2
    echo "  --reproduction-two PATH" >&2
    echo "  --output-root PATH" >&2
}

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly DEFAULT_PRODUCT_ROOT="${REPOSITORY_ROOT}/.build/release-v0.1.0-product-f855dfb/product"
readonly DEFAULT_EVIDENCE_ROOT="${REPOSITORY_ROOT}/.build/release-v0.1.0-evidence-f855dfb"

SUPPORTED_PYTHON="${DEFAULT_PRODUCT_ROOT}/python-1.5.4/bin/python3"
MISMATCH_PYTHON="${DEFAULT_PRODUCT_ROOT}/python-1.5.3-mismatch/bin/python3"
ARTIFACT="${DEFAULT_EVIDENCE_ROOT}/duckdb_api.duckdb_extension"
MANIFEST="${DEFAULT_EVIDENCE_ROOT}/manifest/manifest.json"
MANIFEST_ANCHOR="${DEFAULT_EVIDENCE_ROOT}/manifest/manifest.sha256"
VERIFIER="${REPOSITORY_ROOT}/experiments/repeatable-installation/enablement/verify_trial_bundle.py"
ORACLE="${REPOSITORY_ROOT}/experiments/repeatable-installation/install_oracle.py"
REPRODUCTION_ONE="${REPOSITORY_ROOT}/.build/reproduction-v0.1.0-f855dfb/evidence-one"
REPRODUCTION_TWO="${REPOSITORY_ROOT}/.build/reproduction-v0.1.0-f855dfb/evidence-two"
OUTPUT_ROOT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --supported-python)
            SUPPORTED_PYTHON="${2:?--supported-python requires a path}"
            shift 2
            ;;
        --mismatch-python)
            MISMATCH_PYTHON="${2:?--mismatch-python requires a path}"
            shift 2
            ;;
        --artifact)
            ARTIFACT="${2:?--artifact requires a path}"
            shift 2
            ;;
        --manifest)
            MANIFEST="${2:?--manifest requires a path}"
            shift 2
            ;;
        --manifest-anchor)
            MANIFEST_ANCHOR="${2:?--manifest-anchor requires a path}"
            shift 2
            ;;
        --verifier)
            VERIFIER="${2:?--verifier requires a path}"
            shift 2
            ;;
        --oracle)
            ORACLE="${2:?--oracle requires a path}"
            shift 2
            ;;
        --reproduction-one)
            REPRODUCTION_ONE="${2:?--reproduction-one requires a path}"
            shift 2
            ;;
        --reproduction-two)
            REPRODUCTION_TWO="${2:?--reproduction-two requires a path}"
            shift 2
            ;;
        --output-root)
            OUTPUT_ROOT="${2:?--output-root requires a path}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            echo "unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

for executable in "${SUPPORTED_PYTHON}" "${MISMATCH_PYTHON}"; do
    if [[ ! -x "${executable}" ]]; then
        echo "required pinned Python host is not executable: ${executable}" >&2
        exit 1
    fi
done
for input in "${ARTIFACT}" "${MANIFEST}" "${MANIFEST_ANCHOR}" "${VERIFIER}" "${ORACLE}"; do
    if [[ ! -f "${input}" ]]; then
        echo "required trial input is not a file: ${input}" >&2
        exit 1
    fi
done
for evidence_root in "${REPRODUCTION_ONE}" "${REPRODUCTION_TWO}"; do
    if [[ ! -d "${evidence_root}" ]]; then
        echo "required reproduction evidence root is not a directory: ${evidence_root}" >&2
        exit 1
    fi
done

if [[ -z "${OUTPUT_ROOT}" ]]; then
    readonly OUTPUT_PARENT="${REPOSITORY_ROOT}/.build/repeatable-installation"
    mkdir -p "${OUTPUT_PARENT}"
    OUTPUT_ROOT="$(mktemp -d "${OUTPUT_PARENT}/trial.XXXXXX")"
else
    if [[ -e "${OUTPUT_ROOT}" || -L "${OUTPUT_ROOT}" ]]; then
        echo "trial output root already exists: ${OUTPUT_ROOT}" >&2
        exit 1
    fi
    mkdir -p "$(dirname "${OUTPUT_ROOT}")"
    mkdir "${OUTPUT_ROOT}"
fi
readonly OUTPUT_ROOT

readonly BUNDLE="${OUTPUT_ROOT}/bundle"
readonly FIXTURES="${OUTPUT_ROOT}/fixtures"
readonly ENABLEMENT="${REPOSITORY_ROOT}/experiments/repeatable-installation/enablement"

"${SUPPORTED_PYTHON}" -I "${ENABLEMENT}/verify_reproduced_artifacts.py" \
    "${REPRODUCTION_ONE}" "${REPRODUCTION_TWO}" \
    | tee "${OUTPUT_ROOT}/reproduction-result.json"
chmod 0444 "${OUTPUT_ROOT}/reproduction-result.json"

"${SUPPORTED_PYTHON}" -I "${ENABLEMENT}/assemble_bundle.py" \
    --artifact "${ARTIFACT}" \
    --manifest "${MANIFEST}" \
    --manifest-anchor "${MANIFEST_ANCHOR}" \
    --output "${BUNDLE}"

"${SUPPORTED_PYTHON}" -I "${ENABLEMENT}/make_negative_fixture.py" \
    --artifact "${BUNDLE}/duckdb_api.duckdb_extension" \
    --output "${FIXTURES}"

# Verify the complete five-file custody root immediately before consumer
# execution. Query still consumes the fixed three-path verifier contract.
"${SUPPORTED_PYTHON}" -I "${ENABLEMENT}/verify_assembled_bundle.py" "${BUNDLE}"

"${SUPPORTED_PYTHON}" -I "${ORACLE}" \
    --supported-python "${SUPPORTED_PYTHON}" \
    --mismatch-python "${MISMATCH_PYTHON}" \
    --artifact "${BUNDLE}/duckdb_api.duckdb_extension" \
    --manifest "${BUNDLE}/manifest.json" \
    --manifest-anchor "${BUNDLE}/manifest.sha256" \
    --verifier "${VERIFIER}" \
    --wrong-platform-artifact "${FIXTURES}/wrong-platform.duckdb_extension" \
    --corrupted-artifact "${FIXTURES}/corrupted/duckdb_api.duckdb_extension" \
    --negative-fixture-inventory "${FIXTURES}/negative-fixtures.json" \
    | tee "${OUTPUT_ROOT}/query-result.json"

chmod 0444 "${OUTPUT_ROOT}/query-result.json"
echo "trial output=${OUTPUT_ROOT}"
