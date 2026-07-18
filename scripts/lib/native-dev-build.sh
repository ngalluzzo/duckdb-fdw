# Source synchronization, incremental build, and consumer operations.
# This file is sourced after scripts/lib/native-dev-environment.sh.

tree_digest() {
    python3 -I - "$1" <<'PY'
import hashlib
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
result = hashlib.sha256()
for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file()):
    relative = path.relative_to(root).as_posix().encode()
    result.update(len(relative).to_bytes(8, "big"))
    result.update(relative)
    content = path.read_bytes()
    result.update(len(content).to_bytes(8, "big"))
    result.update(content)
print(result.hexdigest())
PY
}

tree_digest_projection() (
    local stage
    stage="$(mktemp -d "${DEV_ROOT}/projection.XXXXXX")"
    trap 'rm -rf "${stage}"' EXIT
    rsync -a "${TEMPLATE_ROOT}/src/" "${stage}/src/"
    rsync -a "${TEMPLATE_ROOT}/test/" "${stage}/test/"
    rsync -a "${TEMPLATE_ROOT}/fixtures/" "${stage}/fixtures/"
    cp "${TEMPLATE_ROOT}/CMakeLists.txt" "${TEMPLATE_ROOT}/Makefile" \
        "${TEMPLATE_ROOT}/extension_config.cmake" "${stage}/"
    tree_digest "${stage}"
)

sync_sources() {
    local destination_digest
    local source_digest
    local stage
    local status
    status="$(git -C "${REPOSITORY_ROOT}" status --porcelain --untracked-files=all -- \
        src test fixtures CMakeLists.txt Makefile extension_config.cmake | sed -n '/^??/p')"
    if [[ -n "${status}" ]]; then
        echo "native developer sync accepts tracked files only; add or remove:" >&2
        echo "${status}" >&2
        exit 1
    fi
    stage="$(mktemp -d "${DEV_ROOT}/sync.XXXXXX")"
    TEMP_ROOTS+=("${stage}")
    git -C "${REPOSITORY_ROOT}" ls-files -z -- \
        src test fixtures CMakeLists.txt Makefile extension_config.cmake |
        rsync -a --from0 --files-from=- "${REPOSITORY_ROOT}/" "${stage}/"
    source_digest="$(tree_digest "${stage}")"
    if [[ -f "${SOURCE_STATE}" && "$(cat "${SOURCE_STATE}")" == "${source_digest}" ]]; then
        destination_digest="$(tree_digest_projection "${TEMPLATE_ROOT}")"
        if [[ "${destination_digest}" == "${source_digest}" ]]; then
            return
        fi
        echo "repairing stale developer source projection" >&2
    fi
    rm -f "${SOURCE_STATE}"
    rsync -a --delete "${stage}/src/" "${TEMPLATE_ROOT}/src/"
    rsync -a --delete "${stage}/test/" "${TEMPLATE_ROOT}/test/"
    rsync -a --delete "${stage}/fixtures/" "${TEMPLATE_ROOT}/fixtures/"
    cp "${stage}/CMakeLists.txt" "${stage}/Makefile" "${stage}/extension_config.cmake" "${TEMPLATE_ROOT}/"
    "${CMAKE_BIN}" -E rm -f "${TEMPLATE_ROOT}/vcpkg.json"
    destination_digest="$(tree_digest_projection "${TEMPLATE_ROOT}")"
    if [[ "${destination_digest}" != "${source_digest}" ]]; then
        echo "developer source synchronization digest mismatch" >&2
        exit 1
    fi
    printf '%s\n' "${source_digest}" >"${SOURCE_STATE}.tmp.$$"
    mv "${SOURCE_STATE}.tmp.$$" "${SOURCE_STATE}"
}

build_paths() {
    local profile="$1"
    PROFILE="${profile}"
    BUILD_ROOT="${TEMPLATE_ROOT}/build/${profile}"
    STATIC_TEST_CLI="${BUILD_ROOT}/duckdb"
    ARTIFACT="${BUILD_ROOT}/extension/duckdb_api/duckdb_api.duckdb_extension"
    NATIVE_TEST_ROOT="${BUILD_ROOT}/extension/duckdb_api"
}

run_build() {
    local extra_flags_name
    local profile="$1"
    prepare_cell
    build_paths "${profile}"
    "${REPOSITORY_ROOT}/scripts/verify-source-identities.py" >/dev/null
    "${REPOSITORY_ROOT}/scripts/check-native-format.sh"
    if [[ "${profile}" == "debug" ]]; then
        extra_flags_name="EXT_DEBUG_FLAGS"
    else
        extra_flags_name="EXT_RELEASE_FLAGS"
    fi
    mkdir -p "${DEV_ROOT}/home" "${DEV_ROOT}/tmp" "${DEV_ROOT}/cache"
    env -i HOME="${DEV_ROOT}/home" TMPDIR="${DEV_ROOT}/tmp" XDG_CACHE_HOME="${DEV_ROOT}/cache" \
        PATH="${CMAKE_ROOT}/CMake.app/Contents/bin:${NINJA_ROOT}:$(dirname "$(command -v python3)"):/usr/bin:/bin:/usr/sbin:/sbin" \
        GEN=ninja DISABLE_SANITIZER=1 DUCKDB_PLATFORM="${DUCKDB_PLATFORM}" \
        OVERRIDE_GIT_DESCRIBE="${DUCKDB_GIT_DESCRIBE}" \
        make -C "${TEMPLATE_ROOT}" \
            "${extra_flags_name}=-DCMAKE_CXX_STANDARD=11 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DDUCKDB_API_VERIFIED_SDK_ROOT=${SDK_ROOT} -DCURL_NO_CURL_CMAKE=ON -DCURL_INCLUDE_DIR=${SDK_CURL_INCLUDE_DIR} -DCURL_LIBRARY=${SDK_CURL_LIBRARY}" \
            "${profile}"
    if [[ ! -x "${STATIC_TEST_CLI}" ]]; then
        echo "native build did not produce expected static test CLI: ${STATIC_TEST_CLI}" >&2
        exit 1
    fi
    if [[ ! -f "${ARTIFACT}" ]]; then
        echo "native build did not produce expected loadable artifact: ${ARTIFACT}" >&2
        exit 1
    fi
    python3 -I -B "${REPOSITORY_ROOT}/scripts/verify-native-dependencies.py" \
        configuration "${PINS_FILE}" "${SDK_ROOT}" \
        "${NATIVE_TEST_ROOT}/duckdb_api_native_dependencies.json" >/dev/null
}

print_paths() {
    printf 'profile=%s\n' "${PROFILE}"
    printf 'dev_root=%s\n' "${DEV_ROOT}"
    printf 'source_root=%s\n' "${TEMPLATE_ROOT}"
    printf 'build_root=%s\n' "${BUILD_ROOT}"
    printf 'pinned_python=%s\n' "${PINNED_PYTHON}"
    printf 'static_test_cli=%s\n' "${STATIC_TEST_CLI}"
    printf 'artifact=%s\n' "${ARTIFACT}"
    echo "developer_evidence=non-release"
}

run_tests() {
    local contract="${REPOSITORY_ROOT}/test/python/source_demo_contract.py"
    python3 -I -B "${REPOSITORY_ROOT}/scripts/test-native-dependencies.py"
    "${NATIVE_TEST_ROOT}/duckdb_api_connector_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_scan_planner_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_fixture_decoder_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_fixture_stream_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_adapter_tests"
    (
        cd "${TEMPLATE_ROOT}"
        "./build/${PROFILE}/test/unittest" --require duckdb_api 'test/*'
    )
    "${REPOSITORY_ROOT}/scripts/verify-loadable-inventory.sh" "${ARTIFACT}"
    if [[ ! -f "${contract}" ]]; then
        echo "required Query Experience demo contract is missing: ${contract}" >&2
        exit 1
    fi
    python3 -I "${contract}" "${PINNED_PYTHON}" "${ARTIFACT}"
}

run_demo() {
    local demo="${REPOSITORY_ROOT}/examples/first_trustworthy_query.py"
    local isolated
    if [[ ! -f "${demo}" ]]; then
        echo "query-owned first-query demo is not present: ${demo}" >&2
        exit 1
    fi
    isolated="$(mktemp -d "${DEV_ROOT}/demo.XXXXXX")"
    TEMP_ROOTS+=("${isolated}")
    mkdir -p "${isolated}/home" "${isolated}/tmp" "${isolated}/cache" "${isolated}/config"
    env -i HOME="${isolated}/home" TMPDIR="${isolated}/tmp" \
        XDG_CACHE_HOME="${isolated}/cache" XDG_CONFIG_HOME="${isolated}/config" \
        PATH="$(dirname "${PINNED_PYTHON}"):/usr/bin:/bin" \
        "${PINNED_PYTHON}" -I "${demo}" "${ARTIFACT}"
}

run_verify() {
    local profile="$1"
    local verify_parent
    local verify_root
    mkdir -p "${REPOSITORY_ROOT}/.build"
    verify_parent="$(mktemp -d "${REPOSITORY_ROOT}/.build/verify.XXXXXX")"
    verify_root="${verify_parent}/build"
    echo "fresh_build_root=${verify_root}"
    echo "developer_cache_reused=false"
    "${REPOSITORY_ROOT}/scripts/run-native-product-tests.sh" "${verify_root}" "${profile}"
}
