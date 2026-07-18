# Environment and dependency bootstrap for the native developer cell.
# This file is sourced by scripts/native-dev.sh.

readonly PINS_FILE="${REPOSITORY_ROOT}/release/0.4.0/pins.json"
readonly REQUIREMENTS_FILE="${REPOSITORY_ROOT}/test/python/requirements-macos-py314.txt"
readonly DEFAULT_DEV_ROOT="${REPOSITORY_ROOT}/.build/dev"
readonly TEMPLATE_URL="https://github.com/duckdb/extension-template.git"

pin_value() {
    python3 -I - "${PINS_FILE}" "$1" <<'PY'
import json
import pathlib
import sys

value = json.loads(pathlib.Path(sys.argv[1]).read_text())
for component in sys.argv[2].split("."):
    value = value[component]
if not isinstance(value, (str, int, float)):
    raise SystemExit(f"pin is not a scalar: {sys.argv[2]}")
print(value)
PY
}

readonly DUCKDB_VERSION="$(pin_value dependencies.duckdb.version)"
readonly DUCKDB_COMMIT="$(pin_value dependencies.duckdb.commit)"
readonly DUCKDB_TREE="$(pin_value dependencies.duckdb.tree)"
readonly DUCKDB_GIT_DESCRIBE="$(pin_value dependencies.duckdb.git_describe)"
readonly TEMPLATE_COMMIT="$(pin_value dependencies.extension_template.commit)"
readonly TEMPLATE_TREE="$(pin_value dependencies.extension_template.tree)"
readonly CI_TOOLS_COMMIT="$(pin_value dependencies.extension_ci_tools.commit)"
readonly CI_TOOLS_TREE="$(pin_value dependencies.extension_ci_tools.tree)"
readonly CMAKE_URL="$(pin_value tools.cmake_macos_universal.url)"
readonly CMAKE_SHA256="$(pin_value tools.cmake_macos_universal.sha256)"
readonly NINJA_URL="$(pin_value tools.ninja_macos.url)"
readonly NINJA_SHA256="$(pin_value tools.ninja_macos.sha256)"
readonly EXPECTED_HOST="$(pin_value product_cell.host)"
readonly EXPECTED_HOST_BUILD="$(pin_value product_cell.host_build)"
readonly EXPECTED_ARCHITECTURE="$(pin_value product_cell.architecture)"
readonly EXPECTED_COMPILER="$(pin_value product_cell.compiler)"
readonly EXPECTED_CMAKE="$(pin_value product_cell.cmake)"
readonly EXPECTED_NINJA="$(pin_value product_cell.ninja)"
readonly DUCKDB_PLATFORM="$(pin_value product_cell.duckdb_platform)"
readonly EXPECTED_SDK_VERSION="$(pin_value system_dependencies.macos_sdk.version)"
readonly EXPECTED_SDK_BUILD="$(pin_value system_dependencies.macos_sdk.build_version)"

readonly DEV_ROOT="$(python3 -I -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' \
    "${DUCKDB_API_DEV_ROOT:-${DEFAULT_DEV_ROOT}}")"
readonly OWNER_MARKER="${DEV_ROOT}/.duckdb-api-native-dev"
readonly LOCK_ROOT="${DEV_ROOT}/.lock"
readonly TOOL_ROOT="${DEV_ROOT}/tools"
readonly CMAKE_ARCHIVE="${TOOL_ROOT}/cmake.tar.gz"
readonly NINJA_ARCHIVE="${TOOL_ROOT}/ninja.zip"
readonly CMAKE_ROOT="${TOOL_ROOT}/cmake"
readonly NINJA_ROOT="${TOOL_ROOT}/ninja"
readonly CMAKE_BIN="${CMAKE_ROOT}/CMake.app/Contents/bin/cmake"
readonly NINJA_BIN="${NINJA_ROOT}/ninja"
readonly TEMPLATE_ROOT="${DEV_ROOT}/extension-template"
readonly PYTHON_ENV="${DEV_ROOT}/python-${DUCKDB_VERSION}"
readonly PINNED_PYTHON="${PYTHON_ENV}/bin/python3"
readonly PYTHON_REQUIREMENTS_STATE="${PYTHON_ENV}/.requirements.sha256"
readonly SOURCE_STATE="${DEV_ROOT}/source-state.sha256"
readonly OBSERVED_DEPENDENCIES="${DEV_ROOT}/observed-dependencies.json"

SDK_ROOT=""
SDK_CURL_INCLUDE_DIR=""
SDK_CURL_LIBRARY=""

LOCK_HELD=0
TEMP_ROOTS=()

cleanup_native_dev() {
    local temporary
    for temporary in ${TEMP_ROOTS[@]+"${TEMP_ROOTS[@]}"}; do
        if [[ -n "${temporary}" && -e "${temporary}" ]]; then
            rm -rf "${temporary}"
        fi
    done
    if [[ "${LOCK_HELD}" -eq 1 ]]; then
        rm -f "${LOCK_ROOT}/pid"
        rmdir "${LOCK_ROOT}" 2>/dev/null || true
    fi
}
trap cleanup_native_dev EXIT

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing required command: $1" >&2
        exit 1
    fi
}

initialize_dev_root() {
    local owner
    case "${DEV_ROOT}/" in
        "${REPOSITORY_ROOT}/" | "${REPOSITORY_ROOT}/src/"* | "${REPOSITORY_ROOT}/test/"* | \
            "${REPOSITORY_ROOT}/fixtures/"*)
            echo "developer state root overlaps repository sources: ${DEV_ROOT}" >&2
            exit 1
            ;;
    esac
    mkdir -p "${DEV_ROOT}"
    if [[ -e "${OWNER_MARKER}" ]]; then
        owner="$(sed -n 's/^repository=//p' "${OWNER_MARKER}")"
        if [[ "${owner}" != "${REPOSITORY_ROOT}" ]]; then
            echo "developer state root belongs to another worktree: ${DEV_ROOT}" >&2
            exit 1
        fi
    elif find "${DEV_ROOT}" -mindepth 1 -maxdepth 1 -print -quit | grep -q .; then
        echo "developer state root is not empty and has no ownership marker: ${DEV_ROOT}" >&2
        exit 1
    else
        printf 'repository=%s\n' "${REPOSITORY_ROOT}" >"${OWNER_MARKER}"
    fi
}

acquire_lock() {
    local previous_pid=""
    initialize_dev_root
    if ! mkdir "${LOCK_ROOT}" 2>/dev/null; then
        if [[ -f "${LOCK_ROOT}/pid" ]]; then
            previous_pid="$(cat "${LOCK_ROOT}/pid")"
        fi
        if [[ "${previous_pid}" =~ ^[0-9]+$ ]] && kill -0 "${previous_pid}" 2>/dev/null; then
            echo "developer cell is busy in process ${previous_pid}: ${DEV_ROOT}" >&2
            exit 1
        fi
        rm -f "${LOCK_ROOT}/pid"
        if ! rmdir "${LOCK_ROOT}" 2>/dev/null || ! mkdir "${LOCK_ROOT}" 2>/dev/null; then
            echo "developer cell lock cannot be recovered: ${LOCK_ROOT}" >&2
            exit 1
        fi
    fi
    printf '%s\n' "$$" >"${LOCK_ROOT}/pid"
    LOCK_HELD=1
}

verify_host() {
    local actual_compiler
    local actual_host
    local actual_python
    local actual_host_build
    local actual_sdk_build
    local actual_sdk_version
    for command in c++ curl git make nm python3 rsync shasum strings sw_vers tar unzip xcrun; do
        require_command "${command}"
    done
    if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "${EXPECTED_ARCHITECTURE}" ]]; then
        echo "native developer cell requires Darwin ${EXPECTED_ARCHITECTURE}" >&2
        exit 1
    fi
    actual_host="macOS $(sw_vers -productVersion)"
    if [[ "${actual_host}" != "${EXPECTED_HOST}" ]]; then
        echo "native developer cell requires ${EXPECTED_HOST}; found ${actual_host}" >&2
        exit 1
    fi
    actual_host_build="$(sw_vers -buildVersion)"
    if [[ "${actual_host_build}" != "${EXPECTED_HOST_BUILD}" ]]; then
        echo "native developer cell requires host build ${EXPECTED_HOST_BUILD}; found ${actual_host_build}" >&2
        exit 1
    fi
    actual_python="$(python3 -I -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
    if [[ "${actual_python}" != "3.14" ]]; then
        echo "native developer cell requires Python 3.14; found ${actual_python}" >&2
        exit 1
    fi
    actual_compiler="$(c++ --version | head -n 1)"
    if [[ "${actual_compiler}" != "${EXPECTED_COMPILER}"* ]]; then
        echo "native developer cell requires ${EXPECTED_COMPILER}; found ${actual_compiler}" >&2
        exit 1
    fi
    SDK_ROOT="$(xcrun --sdk macosx --show-sdk-path)"
    actual_sdk_version="$(xcrun --sdk macosx --show-sdk-version)"
    actual_sdk_build="$(xcrun --sdk macosx --show-sdk-build-version)"
    local verified
    verified="$(python3 -I -B "${REPOSITORY_ROOT}/scripts/verify-native-dependencies.py" \
        inputs "${PINS_FILE}" "${SDK_ROOT}" "${actual_host#macOS }" "${actual_host_build}" \
        "$(uname -m)" "${actual_sdk_version}" "${actual_sdk_build}")"
    SDK_ROOT="$(python3 -I -c 'import json,sys; print(json.loads(sys.argv[1])["sdk_root"])' "${verified}")"
    SDK_CURL_INCLUDE_DIR="${SDK_ROOT}/$(pin_value system_dependencies.macos_sdk.curl_include_dir)"
    SDK_CURL_LIBRARY="${SDK_ROOT}/$(pin_value system_dependencies.macos_sdk.curl_stub)"
}

download_verified() {
    local url="$1"
    local sha256="$2"
    local destination="$3"
    local temporary
    if [[ -f "${destination}" ]]; then
        if ! printf '%s  %s\n' "${sha256}" "${destination}" | shasum -a 256 -c - >/dev/null; then
            echo "cached tool archive checksum mismatch: ${destination}" >&2
            exit 1
        fi
        return
    fi
    mkdir -p "$(dirname "${destination}")"
    temporary="${destination}.download.$$"
    TEMP_ROOTS+=("${temporary}")
    curl -fL --retry 3 -o "${temporary}" "${url}"
    printf '%s  %s\n' "${sha256}" "${temporary}" | shasum -a 256 -c - >/dev/null
    mv "${temporary}" "${destination}"
}

bootstrap_tools() {
    local temporary
    download_verified "${CMAKE_URL}" "${CMAKE_SHA256}" "${CMAKE_ARCHIVE}"
    download_verified "${NINJA_URL}" "${NINJA_SHA256}" "${NINJA_ARCHIVE}"
    if [[ ! -x "${CMAKE_BIN}" ]] ||
       [[ "$("${CMAKE_BIN}" --version 2>/dev/null | head -n 1)" != "cmake version ${EXPECTED_CMAKE}" ]]; then
        temporary="${CMAKE_ROOT}.extract.$$"
        TEMP_ROOTS+=("${temporary}")
        mkdir -p "${temporary}"
        tar -xzf "${CMAKE_ARCHIVE}" -C "${temporary}" --strip-components=1
        rm -rf "${CMAKE_ROOT}"
        mv "${temporary}" "${CMAKE_ROOT}"
    fi
    if [[ ! -x "${NINJA_BIN}" ]] ||
       [[ "$("${NINJA_BIN}" --version 2>/dev/null)" != "${EXPECTED_NINJA}" ]]; then
        temporary="${NINJA_ROOT}.extract.$$"
        TEMP_ROOTS+=("${temporary}")
        mkdir -p "${temporary}"
        unzip -oq "${NINJA_ARCHIVE}" -d "${temporary}"
        rm -rf "${NINJA_ROOT}"
        mv "${temporary}" "${NINJA_ROOT}"
    fi
    if [[ "$("${CMAKE_BIN}" --version | head -n 1)" != "cmake version ${EXPECTED_CMAKE}" ]]; then
        echo "bootstrapped CMake identity mismatch" >&2
        exit 1
    fi
    if [[ "$("${NINJA_BIN}" --version)" != "${EXPECTED_NINJA}" ]]; then
        echo "bootstrapped Ninja identity mismatch" >&2
        exit 1
    fi
}

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

assert_template_overlay_only() {
    local path
    local status_line
    while IFS= read -r status_line; do
        [[ -z "${status_line}" ]] && continue
        path="${status_line:3}"
        case "${path}" in
            .cache/clangd/* | CMakeLists.txt | Makefile | extension_config.cmake | vcpkg.json | cmake/* | \
                src/* | test/* | fixtures/*)
                ;;
            *)
                echo "template checkout contains an unverified change: ${status_line}" >&2
                exit 1
                ;;
        esac
    done < <(git -C "${TEMPLATE_ROOT}" status --porcelain --untracked-files=all --ignore-submodules=all)
}

bootstrap_template() {
    local current_commit=""
    if [[ ! -e "${TEMPLATE_ROOT}" ]]; then
        mkdir -p "${TEMPLATE_ROOT}"
        git init -q "${TEMPLATE_ROOT}"
    elif [[ ! -d "${TEMPLATE_ROOT}/.git" ]]; then
        echo "developer template root is not a Git checkout: ${TEMPLATE_ROOT}" >&2
        exit 1
    fi
    if ! git -C "${TEMPLATE_ROOT}" remote get-url origin >/dev/null 2>&1; then
        git -C "${TEMPLATE_ROOT}" remote add origin "${TEMPLATE_URL}"
    elif [[ "$(git -C "${TEMPLATE_ROOT}" remote get-url origin)" != "${TEMPLATE_URL}" ]]; then
        echo "developer template origin mismatch" >&2
        exit 1
    fi
    current_commit="$(git -C "${TEMPLATE_ROOT}" rev-parse --verify HEAD 2>/dev/null || true)"
    if [[ -z "${current_commit}" ]]; then
        git -C "${TEMPLATE_ROOT}" fetch --depth 1 origin "${TEMPLATE_COMMIT}"
        git -C "${TEMPLATE_ROOT}" checkout -q --detach FETCH_HEAD
    elif [[ "${current_commit}" != "${TEMPLATE_COMMIT}" ]]; then
        echo "developer template commit mismatch: ${current_commit}" >&2
        exit 1
    fi
    assert_template_overlay_only
    git -C "${TEMPLATE_ROOT}" submodule update --init --recursive --depth 1
    if [[ "$(git -C "${TEMPLATE_ROOT}" rev-parse HEAD)" != "${TEMPLATE_COMMIT}" ]] ||
       [[ "$(git -C "${TEMPLATE_ROOT}" rev-parse 'HEAD^{tree}')" != "${TEMPLATE_TREE}" ]] ||
       [[ "$(git -C "${TEMPLATE_ROOT}/duckdb" rev-parse HEAD)" != "${DUCKDB_COMMIT}" ]] ||
       [[ "$(git -C "${TEMPLATE_ROOT}/duckdb" rev-parse 'HEAD^{tree}')" != "${DUCKDB_TREE}" ]] ||
       [[ "$(git -C "${TEMPLATE_ROOT}/extension-ci-tools" rev-parse HEAD)" != "${CI_TOOLS_COMMIT}" ]] ||
       [[ "$(git -C "${TEMPLATE_ROOT}/extension-ci-tools" rev-parse 'HEAD^{tree}')" != "${CI_TOOLS_TREE}" ]]; then
        echo "pinned developer source identity mismatch" >&2
        exit 1
    fi
    assert_clean_checkout "${TEMPLATE_ROOT}/duckdb" "DuckDB"
    assert_clean_checkout "${TEMPLATE_ROOT}/extension-ci-tools" "extension CI tools"
    python3 -I "${REPOSITORY_ROOT}/scripts/write-observed-dependencies.py" \
        "${REPOSITORY_ROOT}" "${TEMPLATE_ROOT}" "${PINS_FILE}" \
        "${OBSERVED_DEPENDENCIES}" >/dev/null
}

bootstrap_python() {
    local actual_identity=""
    local requirements_digest
    requirements_digest="$(shasum -a 256 "${REQUIREMENTS_FILE}" | awk '{print $1}')"
    if [[ -x "${PINNED_PYTHON}" ]]; then
        actual_identity="$("${PINNED_PYTHON}" -I - "${DEV_ROOT}" <<'PY' 2>/dev/null || true
import pathlib
import sys

environment = pathlib.Path(sys.prefix).resolve()
root = pathlib.Path(sys.argv[1]).resolve()
if root != environment and root not in environment.parents:
    raise SystemExit(1)
import duckdb

version = duckdb.connect().execute("PRAGMA version").fetchone()
print(f"{version[0]}|{version[1]}")
PY
)"
    fi
    if [[ ! -f "${PYTHON_REQUIREMENTS_STATE}" ||
          "$(cat "${PYTHON_REQUIREMENTS_STATE}" 2>/dev/null || true)" != "${requirements_digest}" ||
          "${actual_identity}" != "v${DUCKDB_VERSION}|${DUCKDB_COMMIT:0:10}" ]]; then
        rm -rf "${PYTHON_ENV}"
        python3 -I -m venv "${PYTHON_ENV}"
        "${PINNED_PYTHON}" -I -m pip install --disable-pip-version-check --no-deps \
            --require-hashes -r "${REQUIREMENTS_FILE}"
        actual_identity="$("${PINNED_PYTHON}" -I -c \
            'import duckdb; v=duckdb.connect().execute("PRAGMA version").fetchone(); print(f"{v[0]}|{v[1]}")')"
        if [[ "${actual_identity}" != "v${DUCKDB_VERSION}|${DUCKDB_COMMIT:0:10}" ]]; then
            echo "pinned Python DuckDB host identity mismatch" >&2
            exit 1
        fi
        printf '%s\n' "${requirements_digest}" >"${PYTHON_REQUIREMENTS_STATE}.tmp.$$"
        mv "${PYTHON_REQUIREMENTS_STATE}.tmp.$$" "${PYTHON_REQUIREMENTS_STATE}"
    fi
}

prepare_cell() {
    verify_host
    acquire_lock
    bootstrap_tools
    bootstrap_template
    sync_sources
    bootstrap_python
}
