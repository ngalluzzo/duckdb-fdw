#!/usr/bin/env bash

set -euo pipefail

readonly TRIAL_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly REPOSITORY_ROOT="$(cd "${TRIAL_ROOT}/../.." && pwd)"
readonly BUILD_ROOT="${DUCKDB_FDW_LIVE_REST_PROOF_BUILD_DIR:-${REPOSITORY_ROOT}/.build/live-rest-product-proof}"
readonly TEMPLATE_COMMIT="cfaf3e236008e782d27f4341b0ee036002d0a449"
readonly DUCKDB_COMMIT="08e34c447bae34eaee3723cac61f2878b6bdf787"
readonly CI_TOOLS_COMMIT="b777c70d30942cca5bef62d6d4fa23a13362f398"
readonly TEMPLATE_URL="https://github.com/duckdb/extension-template.git"
readonly DUCKDB_VERSION="1.5.4"
readonly DUCKDB_GIT_DESCRIBE="v1.5.4-0-g08e34c447b"
readonly TRIAL_DUCKDB_PLATFORM="osx_arm64"
readonly PYTHON_VERSION="3.14"
readonly SOURCE_ROOT="${BUILD_ROOT}/extension-template-${TEMPLATE_COMMIT}"
readonly EXTENSION_PATH="${SOURCE_ROOT}/build/debug/extension/live_rest_product_proof/live_rest_product_proof.duckdb_extension"
readonly NATIVE_TEST_ROOT="${SOURCE_ROOT}/build/debug/extension/live_rest_product_proof"
readonly PYTHON_ENV="${BUILD_ROOT}/python-${DUCKDB_VERSION}"

run_real=0
if [[ "${1:-}" == "--real" ]]; then
    run_real=1
    shift
fi
if [[ "$#" -ne 0 ]]; then
    echo "usage: $0 [--real]" >&2
    exit 1
fi

for command in git python3 cmake ninja make diff c++ otool; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "missing required command: ${command}" >&2
        exit 1
    fi
done

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    echo "this bounded proof currently records only the Darwin arm64 target" >&2
    exit 1
fi
if [[ "$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')" != "${PYTHON_VERSION}" ]]; then
    echo "this bounded proof requires Python ${PYTHON_VERSION} for the hashed target wheel" >&2
    exit 1
fi

assert_clean_checkout() {
    local checkout="$1"
    local label="$2"
    local status

    status="$(git -C "${checkout}" status --porcelain --untracked-files=all)"
    if [[ -n "${status}" ]]; then
        echo "${label} checkout contains unverified changes:" >&2
        echo "${status}" >&2
        exit 1
    fi
}

assert_template_changes_are_overlaid() {
    local status_line
    local path

    while IFS= read -r status_line; do
        [[ -z "${status_line}" ]] && continue
        path="${status_line:3}"
        case "${path}" in
            CMakeLists.txt | Makefile | extension_config.cmake | vcpkg.json | src/* | test/*)
                ;;
            *)
                echo "template checkout contains an unverified tracked change: ${status_line}" >&2
                exit 1
                ;;
        esac
    done < <(git -C "${SOURCE_ROOT}" status --porcelain --untracked-files=no)
}

mkdir -p "${BUILD_ROOT}"

if [[ ! -d "${SOURCE_ROOT}/.git" ]]; then
    git init "${SOURCE_ROOT}"
fi
if ! git -C "${SOURCE_ROOT}" remote get-url origin >/dev/null 2>&1; then
    git -C "${SOURCE_ROOT}" remote add origin "${TEMPLATE_URL}"
elif [[ "$(git -C "${SOURCE_ROOT}" remote get-url origin)" != "${TEMPLATE_URL}" ]]; then
    echo "template checkout origin does not match ${TEMPLATE_URL}" >&2
    exit 1
fi

current_template_commit="$(git -C "${SOURCE_ROOT}" rev-parse HEAD 2>/dev/null || true)"
if [[ "${current_template_commit}" != "${TEMPLATE_COMMIT}" ]]; then
    git -C "${SOURCE_ROOT}" fetch --depth 1 origin "${TEMPLATE_COMMIT}"
    git -C "${SOURCE_ROOT}" checkout --detach FETCH_HEAD
fi

if [[ "$(git -C "${SOURCE_ROOT}" rev-parse HEAD)" != "${TEMPLATE_COMMIT}" ]]; then
    echo "template checkout does not match pinned commit ${TEMPLATE_COMMIT}" >&2
    exit 1
fi
assert_template_changes_are_overlaid
git -C "${SOURCE_ROOT}" submodule update --init --recursive --depth 1
if [[ "$(git -C "${SOURCE_ROOT}/duckdb" rev-parse HEAD)" != "${DUCKDB_COMMIT}" ]]; then
    echo "DuckDB checkout does not match pinned commit ${DUCKDB_COMMIT}" >&2
    exit 1
fi
if [[ "$(git -C "${SOURCE_ROOT}/extension-ci-tools" rev-parse HEAD)" != "${CI_TOOLS_COMMIT}" ]]; then
    echo "extension CI tools checkout does not match pinned commit ${CI_TOOLS_COMMIT}" >&2
    exit 1
fi
assert_clean_checkout "${SOURCE_ROOT}/duckdb" "DuckDB"
assert_clean_checkout "${SOURCE_ROOT}/extension-ci-tools" "extension CI tools"

rm -rf "${SOURCE_ROOT}/src" "${SOURCE_ROOT}/test"
rm -f "${SOURCE_ROOT}/vcpkg.json"
cp -R "${TRIAL_ROOT}/src" "${SOURCE_ROOT}/src"
cp -R "${TRIAL_ROOT}/test" "${SOURCE_ROOT}/test"
cp "${TRIAL_ROOT}/CMakeLists.txt" "${SOURCE_ROOT}/CMakeLists.txt"
cp "${TRIAL_ROOT}/extension_config.cmake" "${SOURCE_ROOT}/extension_config.cmake"
cp "${TRIAL_ROOT}/Makefile" "${SOURCE_ROOT}/Makefile"

diff -qr "${TRIAL_ROOT}/src" "${SOURCE_ROOT}/src"
diff -qr "${TRIAL_ROOT}/test" "${SOURCE_ROOT}/test"
diff -q "${TRIAL_ROOT}/CMakeLists.txt" "${SOURCE_ROOT}/CMakeLists.txt"
diff -q "${TRIAL_ROOT}/extension_config.cmake" "${SOURCE_ROOT}/extension_config.cmake"
diff -q "${TRIAL_ROOT}/Makefile" "${SOURCE_ROOT}/Makefile"
assert_template_changes_are_overlaid

# Rebuild every object from the pinned sources; opaque CMake cache state is not
# accepted as evidence for this network/lifecycle proof.
rm -rf "${SOURCE_ROOT}/build/debug"
GEN=ninja DISABLE_SANITIZER=1 DUCKDB_PLATFORM="${TRIAL_DUCKDB_PLATFORM}" \
    OVERRIDE_GIT_DESCRIBE="${DUCKDB_GIT_DESCRIBE}" \
    make -C "${SOURCE_ROOT}" EXT_DEBUG_FLAGS="-DCMAKE_CXX_STANDARD=11" debug

"${NATIVE_TEST_ROOT}/live_rest_plan_tests"
"${NATIVE_TEST_ROOT}/live_rest_json_decoder_tests"
"${NATIVE_TEST_ROOT}/live_rest_http_scan_runtime_tests"
"${NATIVE_TEST_ROOT}/live_rest_network_policy_tests"
runtime_curl_version="$("${NATIVE_TEST_ROOT}/live_rest_curl_identity")"
if [[ -z "${runtime_curl_version}" ]]; then
    echo "linked libcurl runtime did not report its version" >&2
    exit 1
fi

if [[ ! -x "${PYTHON_ENV}/bin/python3" ]]; then
    python3 -m venv "${PYTHON_ENV}"
fi
"${PYTHON_ENV}/bin/python3" -m pip install --disable-pip-version-check --no-deps --force-reinstall \
    --require-hashes -r "${TRIAL_ROOT}/test/requirements.txt"
if [[ "$("${PYTHON_ENV}/bin/python3" -c 'import duckdb; print(duckdb.__version__)')" != "${DUCKDB_VERSION}" ]]; then
    echo "Python DuckDB package does not match required version ${DUCKDB_VERSION}" >&2
    exit 1
fi

integration_arguments=("${EXTENSION_PATH}")
if [[ "${run_real}" -eq 1 ]]; then
    integration_arguments+=(--real)
fi
"${PYTHON_ENV}/bin/python3" "${TRIAL_ROOT}/test/integration_success.py" "${integration_arguments[@]}"
"${PYTHON_ENV}/bin/python3" "${TRIAL_ROOT}/test/integration_failures.py" "${EXTENSION_PATH}"

configured_compiler="$(sed -n 's/^CMAKE_CXX_COMPILER:FILEPATH=//p' \
    "${SOURCE_ROOT}/build/debug/CMakeCache.txt")"
if [[ -z "${configured_compiler}" || ! -x "${configured_compiler}" ]]; then
    echo "CMake did not record an executable C++ compiler" >&2
    exit 1
fi
configured_curl="$(sed -n \
    -e 's/^CURL_LIBRARY_RELEASE:FILEPATH=//p' \
    -e 's/^CURL_LIBRARY:FILEPATH=//p' \
    "${SOURCE_ROOT}/build/debug/CMakeCache.txt" | head -n 1)"
if [[ -z "${configured_curl}" ]]; then
    echo "CMake did not record the resolved libcurl library" >&2
    exit 1
fi
readonly DEPENDENCY_EVIDENCE="${SOURCE_ROOT}/build/debug/live_rest_dependency_evidence.txt"
if [[ ! -f "${DEPENDENCY_EVIDENCE}" ]]; then
    echo "CMake did not write live REST dependency evidence" >&2
    exit 1
fi
evidence_curl_library="$(sed -n 's/^libcurl_libraries=//p' "${DEPENDENCY_EVIDENCE}")"
evidence_curl_include="$(sed -n 's/^libcurl_include_dirs=//p' "${DEPENDENCY_EVIDENCE}")"
evidence_curl_version="$(sed -n 's/^libcurl_version=//p' "${DEPENDENCY_EVIDENCE}")"
if [[ "${evidence_curl_library}" != "${configured_curl}" || -z "${evidence_curl_include}" || \
      -z "${evidence_curl_version}" ]]; then
    echo "configured libcurl evidence is incomplete or inconsistent" >&2
    exit 1
fi
linked_curl="$(otool -L "${EXTENSION_PATH}" | awk '$1 ~ /libcurl/ {print $1}')"
if [[ -z "${linked_curl}" || "$(printf '%s\n' "${linked_curl}" | wc -l | tr -d ' ')" != "1" ]]; then
    echo "loadable artifact does not record exactly one libcurl linkage" >&2
    exit 1
fi
identity_linked_curl="$(otool -L "${NATIVE_TEST_ROOT}/live_rest_curl_identity" | \
    awk '$1 ~ /libcurl/ {print $1}')"
if [[ "${identity_linked_curl}" != "${linked_curl}" ]]; then
    echo "runtime identity probe and loadable artifact use different libcurl linkages" >&2
    exit 1
fi

echo "live REST product proof passed"
echo "template_commit=${TEMPLATE_COMMIT}"
echo "duckdb_commit=${DUCKDB_COMMIT}"
echo "ci_tools_commit=${CI_TOOLS_COMMIT}"
echo "duckdb_version=${DUCKDB_VERSION}"
echo "platform=${TRIAL_DUCKDB_PLATFORM}"
echo "compiler_path=${configured_compiler}"
echo "compiler=$("${configured_compiler}" --version | head -n 1)"
echo "libcurl_configured_library=${configured_curl}"
echo "libcurl_include_dirs=${evidence_curl_include}"
echo "libcurl_linkage=${linked_curl}"
echo "libcurl_configured_version=${evidence_curl_version}"
echo "libcurl_runtime_version=${runtime_curl_version}"
echo "cmake=$(cmake --version | head -n 1)"
echo "ninja=$(ninja --version)"
