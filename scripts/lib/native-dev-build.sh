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
    rsync -a "${TEMPLATE_ROOT}/cmake/" "${stage}/cmake/"
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
        src test cmake CMakeLists.txt Makefile extension_config.cmake | sed -n '/^??/p')"
    if [[ -n "${status}" ]]; then
        echo "native developer sync accepts tracked files only; add or remove:" >&2
        echo "${status}" >&2
        exit 1
    fi
    stage="$(mktemp -d "${DEV_ROOT}/sync.XXXXXX")"
    TEMP_ROOTS+=("${stage}")
    git -C "${REPOSITORY_ROOT}" ls-files -z -- \
        src test cmake CMakeLists.txt Makefile extension_config.cmake |
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
    rsync -a --delete "${stage}/cmake/" "${TEMPLATE_ROOT}/cmake/"
    rm -rf "${TEMPLATE_ROOT}/fixtures"
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
    CONTROLLED_ARTIFACT="${BUILD_ROOT}/private/duckdb_api_controlled.duckdb_extension"
}

run_build() {
    local extra_flags_name
    local profile="$1"
    local target
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
    # Repair reusable cells created before the controlled artifact moved out of
    # DuckDB's install-repository glob. Removing the generated output makes
    # Ninja rebuild it only at the private path below.
    if [[ -d "${BUILD_ROOT}" ]]; then
        find "${BUILD_ROOT}" -type f \
            -name 'duckdb_api_controlled.duckdb_extension' \
            ! -path "${CONTROLLED_ARTIFACT}" -delete
    fi
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
    if [[ ! -f "${CONTROLLED_ARTIFACT}" ]]; then
        echo "native build did not produce expected private controlled artifact: ${CONTROLLED_ARTIFACT}" >&2
        exit 1
    fi
    if find "${BUILD_ROOT}/repository" -type f \
        -name 'duckdb_api_controlled.duckdb_extension' -print -quit | grep -q .; then
        echo "private controlled artifact entered DuckDB's install repository" >&2
        exit 1
    fi
    python3 -I -B "${REPOSITORY_ROOT}/scripts/verify-native-dependencies.py" \
        configuration "${PINS_FILE}" "${SDK_ROOT}" \
        "${NATIVE_TEST_ROOT}/duckdb_api_native_dependencies.json" >/dev/null
    python3 -I -B "${REPOSITORY_ROOT}/scripts/verify-native-product-sources.py" \
        "${PINS_FILE}" "${NATIVE_TEST_ROOT}/duckdb_api_product_sources.json" >/dev/null
    "${NATIVE_TEST_ROOT}/duckdb_api_native_dependency_identity" \
        >"${DEV_ROOT}/observed-native-runtime.json"
    python3 -I -B "${REPOSITORY_ROOT}/scripts/verify-native-dependencies.py" \
        runtime "${PINS_FILE}" "${DEV_ROOT}/observed-native-runtime.json" >/dev/null
    python3 -I -B "${REPOSITORY_ROOT}/scripts/verify-native-dependencies.py" \
        linkage "${PINS_FILE}" transport \
        "${NATIVE_TEST_ROOT}/duckdb_api_native_dependency_identity" >/dev/null
    python3 -I -B "${REPOSITORY_ROOT}/scripts/verify-native-dependencies.py" \
        linkage "${PINS_FILE}" transport "${CONTROLLED_ARTIFACT}" >/dev/null
    for target in \
        duckdb_api_connector_tests \
        duckdb_api_connector_catalog_fixture_tests \
        duckdb_api_failsafe_yaml_tests \
        duckdb_api_package_digest_tests \
        duckdb_api_package_source_tests \
        duckdb_api_local_package_compiler_tests \
        duckdb_api_local_package_reload_fixture_tests \
        duckdb_api_scan_request_tests \
        duckdb_api_typed_value_adapter_tests \
        duckdb_api_scan_planner_tests \
        duckdb_api_scan_plan_contract_tests \
        duckdb_api_scan_plan_pagination_contract_tests \
        duckdb_api_scan_plan_fixture_tests \
        duckdb_api_graphql_semantics_tests \
        duckdb_api_package_rest_planning_tests \
        duckdb_api_repository_graphql_fixture_consumer_tests \
        duckdb_api_execution_contract_tests \
        duckdb_api_runtime_generation_contract_tests \
        duckdb_api_runtime_generation_lifecycle_tests \
        duckdb_api_authorization_contract_tests \
        duckdb_api_network_policy_tests \
        duckdb_api_uri_reference_tests \
        duckdb_api_link_pagination_tests \
        duckdb_api_scan_resource_accounting_tests \
        duckdb_api_request_validation_tests \
        duckdb_api_http_transport_contract_tests \
        duckdb_api_decoded_page_buffer_tests \
        duckdb_api_json_decoder_tests \
        duckdb_api_json_root_array_decoder_tests \
        duckdb_api_graphql_response_decoder_tests \
        duckdb_api_graphql_cursor_pagination_tests \
        duckdb_api_controlled_runtime_scenario_tests \
        duckdb_api_http_scan_executor_tests \
        duckdb_api_http_scan_pagination_tests \
        duckdb_api_http_scan_executor_policy_tests \
        duckdb_api_graphql_paginated_scan_tests \
        duckdb_api_graphql_plan_admission_tests \
        duckdb_api_package_http_execution_tests \
        duckdb_api_duckdb_secret_tests \
        duckdb_api_adapter_tests \
        duckdb_api_graphql_query_contract_tests \
        duckdb_api_graphql_product_contract_tests \
        duckdb_api_adapter_stream_contract_tests \
        duckdb_api_package_generation_composition_tests \
        duckdb_api_package_query_surface_tests \
        duckdb_api_package_product_contract_tests; do
        python3 -I -B "${REPOSITORY_ROOT}/scripts/verify-native-dependencies.py" \
            linkage "${PINS_FILE}" curl-free "${NATIVE_TEST_ROOT}/${target}" >/dev/null
    done
    for target in \
        duckdb_api_curl_http_transport_tests \
        duckdb_api_curl_http_budget_tests \
        duckdb_api_curl_http_lifecycle_tests \
        duckdb_api_curl_transfer_policy_tests \
        duckdb_api_curl_link_metadata_tests \
        duckdb_api_curl_http_pagination_tests \
        duckdb_api_curl_tls_security_tests; do
        python3 -I -B "${REPOSITORY_ROOT}/scripts/verify-native-dependencies.py" \
            linkage "${PINS_FILE}" transport "${NATIVE_TEST_ROOT}/${target}" >/dev/null
    done
    if ! strings "${NATIVE_TEST_ROOT}/duckdb_api_curl_http_transport_tests" |
        grep -F 'duckdb_api_private_curl_option_observer_v1' >/dev/null; then
        echo "private curl option observer canary is missing from its focused target" >&2
        exit 1
    fi
    for target in "${ARTIFACT}" "${CONTROLLED_ARTIFACT}" "${STATIC_TEST_CLI}"; do
        if strings "${target}" |
            grep -F 'duckdb_api_private_curl_option_observer_v1' >/dev/null; then
            echo "private curl option observer canary entered a product artifact" >&2
            exit 1
        fi
    done
}

print_paths() {
    printf 'profile=%s\n' "${PROFILE}"
    printf 'dev_root=%s\n' "${DEV_ROOT}"
    printf 'source_root=%s\n' "${TEMPLATE_ROOT}"
    printf 'build_root=%s\n' "${BUILD_ROOT}"
    printf 'pinned_python=%s\n' "${PINNED_PYTHON}"
    printf 'static_test_cli=%s\n' "${STATIC_TEST_CLI}"
    printf 'artifact=%s\n' "${ARTIFACT}"
    printf 'controlled_artifact=%s\n' "${CONTROLLED_ARTIFACT}"
    echo "developer_evidence=non-release"
}

run_tests() {
    local contract="${REPOSITORY_ROOT}/test/python/source_demo_contract.py"
    python3 -I -B "${REPOSITORY_ROOT}/scripts/verify-public-surface-inventory.py"
    python3 -I -B "${REPOSITORY_ROOT}/test/python/public_surface_inventory_tests.py"
    python3 -I -B "${REPOSITORY_ROOT}/scripts/test-native-dependencies.py"
    "${NATIVE_TEST_ROOT}/duckdb_api_connector_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_connector_catalog_fixture_tests"
    (
        cd "${REPOSITORY_ROOT}"
        "${NATIVE_TEST_ROOT}/duckdb_api_failsafe_yaml_tests"
        "${NATIVE_TEST_ROOT}/duckdb_api_package_digest_tests"
        "${NATIVE_TEST_ROOT}/duckdb_api_package_source_tests"
        "${NATIVE_TEST_ROOT}/duckdb_api_local_package_compiler_tests"
        "${NATIVE_TEST_ROOT}/duckdb_api_local_package_reload_fixture_tests" "${REPOSITORY_ROOT}"
        "${NATIVE_TEST_ROOT}/duckdb_api_compiled_package_generation_tests"
        "${NATIVE_TEST_ROOT}/duckdb_api_package_compatibility_tests"
        "${NATIVE_TEST_ROOT}/duckdb_api_package_compiler_contract_tests"
        "${NATIVE_TEST_ROOT}/duckdb_api_package_graphql_renderer_tests"
        "${NATIVE_TEST_ROOT}/duckdb_api_package_predicate_compiler_tests"
        "${NATIVE_TEST_ROOT}/duckdb_api_package_schema_contract_tests"
        "${NATIVE_TEST_ROOT}/duckdb_api_package_compiler_fixture_tests" "${REPOSITORY_ROOT}"
        "${NATIVE_TEST_ROOT}/duckdb_api_package_fixture_candidate_tests" "${REPOSITORY_ROOT}"
        "${NATIVE_TEST_ROOT}/duckdb_api_package_fixture_coverage_tests" "${REPOSITORY_ROOT}"
        "${NATIVE_TEST_ROOT}/duckdb_api_rickandmorty_package_compiler_tests" "${REPOSITORY_ROOT}"
    )
    "${NATIVE_TEST_ROOT}/duckdb_api_scan_request_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_typed_value_adapter_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_scan_planner_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_scan_plan_contract_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_scan_plan_pagination_contract_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_scan_plan_fixture_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_semantics_input_resolution_observation_service_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_graphql_semantics_tests" "${REPOSITORY_ROOT}"
    "${NATIVE_TEST_ROOT}/duckdb_api_package_rest_planning_tests" "${REPOSITORY_ROOT}"
    "${NATIVE_TEST_ROOT}/duckdb_api_repository_graphql_fixture_consumer_tests" "${REPOSITORY_ROOT}"
    "${NATIVE_TEST_ROOT}/duckdb_api_runtime_rest_predicate_fixture_consumer_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_execution_contract_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_runtime_generation_contract_tests" "${REPOSITORY_ROOT}"
    "${NATIVE_TEST_ROOT}/duckdb_api_runtime_generation_lifecycle_tests" "${REPOSITORY_ROOT}"
    "${NATIVE_TEST_ROOT}/duckdb_api_authorization_contract_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_network_policy_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_uri_reference_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_link_pagination_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_scan_resource_accounting_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_request_validation_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_http_transport_contract_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_decoded_page_buffer_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_json_decoder_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_json_root_array_decoder_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_graphql_response_decoder_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_graphql_cursor_pagination_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_controlled_runtime_scenario_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_http_scan_executor_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_http_scan_pagination_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_http_scan_executor_policy_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_rest_plan_admission_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_graphql_paginated_scan_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_graphql_plan_admission_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_package_http_execution_tests" "${REPOSITORY_ROOT}"
    "${NATIVE_TEST_ROOT}/duckdb_api_package_fixture_cancellation_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_package_fixture_column_variant_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_package_fixture_execution_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_package_fixture_failure_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_package_fixture_graphql_body_variant_tests" "${REPOSITORY_ROOT}"
    "${NATIVE_TEST_ROOT}/duckdb_api_package_fixture_pagination_variant_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_package_fixture_resource_variant_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_curl_http_transport_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_curl_http_budget_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_curl_http_lifecycle_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_curl_transfer_policy_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_curl_link_metadata_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_curl_http_pagination_tests"
    "${PINNED_PYTHON}" -I -B \
        "${REPOSITORY_ROOT}/test/python/runtime_curl_tls_tests.py" \
        "${NATIVE_TEST_ROOT}/duckdb_api_curl_tls_security_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_adapter_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_graphql_query_contract_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_graphql_product_contract_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_adapter_stream_contract_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_duckdb_secret_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_package_generation_composition_tests" "${REPOSITORY_ROOT}"
    "${NATIVE_TEST_ROOT}/duckdb_api_query_package_fixture_publication_tests"
    "${NATIVE_TEST_ROOT}/duckdb_api_package_query_surface_tests" "${REPOSITORY_ROOT}"
    "${NATIVE_TEST_ROOT}/duckdb_api_package_product_contract_tests" "${REPOSITORY_ROOT}"
    (
        cd "${TEMPLATE_ROOT}"
        "./build/${PROFILE}/test/unittest" --require duckdb_api 'test/*'
    )
    "${REPOSITORY_ROOT}/scripts/verify-loadable-inventory.sh" \
        "${ARTIFACT}" "${PINS_FILE}" transport
    "${PINNED_PYTHON}" -I -B \
        "${REPOSITORY_ROOT}/test/python/live_rest_product_contract.py" \
        "${CONTROLLED_ARTIFACT}"
    "${PINNED_PYTHON}" -I -B \
        "${REPOSITORY_ROOT}/test/python/authenticated_relation_product_contract.py" \
        "${CONTROLLED_ARTIFACT}"
    "${PINNED_PYTHON}" -I -B \
        "${REPOSITORY_ROOT}/test/python/repository_pagination_product_contract.py" \
        "${CONTROLLED_ARTIFACT}"
    if [[ ! -f "${contract}" ]]; then
        echo "required Query Experience demo contract is missing: ${contract}" >&2
        exit 1
    fi
    python3 -I "${contract}" "${PINNED_PYTHON}" "${ARTIFACT}"
}

run_demo() {
    local demo="${REPOSITORY_ROOT}/examples/first_live_rest_relation.py"
    local isolated
    if [[ ! -f "${demo}" ]]; then
        echo "query-owned live REST demo is not present: ${demo}" >&2
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
