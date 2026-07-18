#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 2 ]]; then
    echo "usage: run-linux-sanitized.sh NEW_BUILD_ROOT NEW_OUTPUT_ROOT" >&2
    exit 2
fi

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "${REPOSITORY_ROOT}/scripts/lib/release-common.sh"
readonly BUILD_ROOT="$(release_resolve_path "$1")"
readonly OUTPUT_ROOT="$(release_resolve_path "$2")"
readonly PROJECT_SOURCE="${BUILD_ROOT}/project-source"
readonly TEMPLATE_ROOT="${BUILD_ROOT}/extension-template"
readonly TOOL_ROOT="${BUILD_ROOT}/tools"
readonly CMAKE_ROOT="${TOOL_ROOT}/cmake"
readonly NINJA_ROOT="${TOOL_ROOT}/ninja"
readonly CMAKE_BIN="${CMAKE_ROOT}/bin/cmake"
readonly NINJA_BIN="${NINJA_ROOT}/ninja"
readonly ARTIFACT="${TEMPLATE_ROOT}/build/debug/extension/duckdb_api/duckdb_api.duckdb_extension"
readonly TEMPLATE_COMMIT="$(release_pin "${REPOSITORY_ROOT}" dependencies extension_template commit)"
readonly TEMPLATE_TREE="$(release_pin "${REPOSITORY_ROOT}" dependencies extension_template tree)"
readonly DUCKDB_COMMIT="$(release_pin "${REPOSITORY_ROOT}" dependencies duckdb commit)"
readonly DUCKDB_TREE="$(release_pin "${REPOSITORY_ROOT}" dependencies duckdb tree)"
readonly DUCKDB_DESCRIBE="$(release_pin "${REPOSITORY_ROOT}" dependencies duckdb git_describe)"
readonly CI_TOOLS_COMMIT="$(release_pin "${REPOSITORY_ROOT}" dependencies extension_ci_tools commit)"
readonly CI_TOOLS_TREE="$(release_pin "${REPOSITORY_ROOT}" dependencies extension_ci_tools tree)"
readonly CMAKE_URL="$(release_pin "${REPOSITORY_ROOT}" tools cmake_linux_x86_64 url)"
readonly CMAKE_SHA256="$(release_pin "${REPOSITORY_ROOT}" tools cmake_linux_x86_64 sha256)"
readonly NINJA_URL="$(release_pin "${REPOSITORY_ROOT}" tools ninja_linux url)"
readonly NINJA_SHA256="$(release_pin "${REPOSITORY_ROOT}" tools ninja_linux sha256)"

release_require_safe_generated_root "${REPOSITORY_ROOT}" "sanitizer build root" "${BUILD_ROOT}"
release_require_safe_generated_root "${REPOSITORY_ROOT}" "sanitizer output root" "${OUTPUT_ROOT}"
release_require_new_root "sanitizer build root" "${BUILD_ROOT}"
release_require_new_root "sanitizer output root" "${OUTPUT_ROOT}"
release_require_disjoint_roots "sanitizer build root" "${BUILD_ROOT}" \
    "sanitizer output root" "${OUTPUT_ROOT}"
if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
    echo "linux_amd64-sanitized requires a native Linux x86_64 executor" >&2
    exit 1
fi
if ! grep -q '^VERSION_ID="24.04"$' /etc/os-release; then
    echo "linux_amd64-sanitized requires the pinned Ubuntu 24.04 image" >&2
    exit 1
fi
"${REPOSITORY_ROOT}/scripts/sanitizer-source-guard.sh" "${BUILD_ROOT}"
for command in clang clang++ curl git make nm python3 rsync sha256sum strings tar unzip; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "missing required sanitizer-cell command: ${command}" >&2
        exit 1
    fi
done

mkdir -p "${PROJECT_SOURCE}" "${TEMPLATE_ROOT}" "${TOOL_ROOT}" "${CMAKE_ROOT}" "${NINJA_ROOT}"
git -C "${REPOSITORY_ROOT}" archive --format=tar HEAD | tar -x -C "${PROJECT_SOURCE}"
curl -fL --retry 3 -o "${TOOL_ROOT}/cmake.tar.gz" "${CMAKE_URL}"
printf '%s  %s\n' "${CMAKE_SHA256}" "${TOOL_ROOT}/cmake.tar.gz" | sha256sum -c -
curl -fL --retry 3 -o "${TOOL_ROOT}/ninja.zip" "${NINJA_URL}"
printf '%s  %s\n' "${NINJA_SHA256}" "${TOOL_ROOT}/ninja.zip" | sha256sum -c -
tar -xzf "${TOOL_ROOT}/cmake.tar.gz" -C "${CMAKE_ROOT}" --strip-components=1
unzip -oq "${TOOL_ROOT}/ninja.zip" -d "${NINJA_ROOT}"

git init "${TEMPLATE_ROOT}"
git -C "${TEMPLATE_ROOT}" remote add origin https://github.com/duckdb/extension-template.git
git -C "${TEMPLATE_ROOT}" fetch --depth 1 origin "${TEMPLATE_COMMIT}"
git -C "${TEMPLATE_ROOT}" checkout --detach FETCH_HEAD
git -C "${TEMPLATE_ROOT}" submodule update --init --recursive --depth 1
if [[ "$(git -C "${TEMPLATE_ROOT}" rev-parse HEAD)" != "${TEMPLATE_COMMIT}" ]] ||
   [[ "$(git -C "${TEMPLATE_ROOT}" rev-parse 'HEAD^{tree}')" != "${TEMPLATE_TREE}" ]] ||
   [[ "$(git -C "${TEMPLATE_ROOT}/duckdb" rev-parse HEAD)" != "${DUCKDB_COMMIT}" ]] ||
   [[ "$(git -C "${TEMPLATE_ROOT}/duckdb" rev-parse 'HEAD^{tree}')" != "${DUCKDB_TREE}" ]] ||
   [[ "$(git -C "${TEMPLATE_ROOT}/extension-ci-tools" rev-parse HEAD)" != "${CI_TOOLS_COMMIT}" ]] ||
   [[ "$(git -C "${TEMPLATE_ROOT}/extension-ci-tools" rev-parse 'HEAD^{tree}')" != "${CI_TOOLS_TREE}" ]]; then
    echo "sanitizer-cell source identity mismatch" >&2
    exit 1
fi
python3 -I "${REPOSITORY_ROOT}/scripts/write-observed-dependencies.py" \
    "${REPOSITORY_ROOT}" "${TEMPLATE_ROOT}" "${BUILD_ROOT}/observed-dependencies.json" >/dev/null
rsync -a --delete "${PROJECT_SOURCE}/src/" "${TEMPLATE_ROOT}/src/"
rsync -a --delete "${PROJECT_SOURCE}/test/" "${TEMPLATE_ROOT}/test/"
rsync -a --delete "${PROJECT_SOURCE}/fixtures/" "${TEMPLATE_ROOT}/fixtures/"
cp "${PROJECT_SOURCE}/CMakeLists.txt" "${PROJECT_SOURCE}/Makefile" \
    "${PROJECT_SOURCE}/extension_config.cmake" "${TEMPLATE_ROOT}/"
"${CMAKE_BIN}" -E rm -f "${TEMPLATE_ROOT}/vcpkg.json"
python3 -I "${PROJECT_SOURCE}/scripts/verify-source-identities.py"
"${PROJECT_SOURCE}/scripts/check-native-format.sh"

readonly CLEAN_HOME="${BUILD_ROOT}/home"
readonly CLEAN_TMP="${BUILD_ROOT}/tmp"
readonly CLEAN_CACHE="${BUILD_ROOT}/cache"
mkdir -p "${CLEAN_HOME}" "${CLEAN_TMP}" "${CLEAN_CACHE}"
env -i HOME="${CLEAN_HOME}" TMPDIR="${CLEAN_TMP}" XDG_CACHE_HOME="${CLEAN_CACHE}" \
    PATH="${CMAKE_ROOT}/bin:${NINJA_ROOT}:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
    CC=/usr/bin/clang CXX=/usr/bin/clang++ GEN=ninja \
    DUCKDB_PLATFORM=linux_amd64 OVERRIDE_GIT_DESCRIBE="${DUCKDB_DESCRIBE}" \
    make -C "${TEMPLATE_ROOT}" \
        EXT_DEBUG_FLAGS='-DCMAKE_CXX_STANDARD=11 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DENABLE_SANITIZER=TRUE -DENABLE_UBSAN=TRUE' \
        debug

readonly COMPILE_COMMANDS="${TEMPLATE_ROOT}/build/debug/compile_commands.json"
readonly FLAGS_REPORT="${BUILD_ROOT}/sanitizer-flags.txt"
python3 -I "${PROJECT_SOURCE}/scripts/verify-sanitizer-flags.py" "${COMPILE_COMMANDS}" | tee "${FLAGS_REPORT}"
readonly DISABLED_CANARY="${BUILD_ROOT}/compile-commands-without-sanitizers.json"
python3 -I - "${COMPILE_COMMANDS}" "${DISABLED_CANARY}" <<'PY'
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
source = source.replace("-fsanitize=address", "").replace("-fsanitize=undefined", "")
pathlib.Path(sys.argv[2]).write_text(source)
PY
if python3 -I "${PROJECT_SOURCE}/scripts/verify-sanitizer-flags.py" \
    "${DISABLED_CANARY}" >/dev/null 2>&1; then
    echo "disabled-sanitizer negative canary unexpectedly passed" >&2
    exit 1
fi
echo "disabled-sanitizer negative canary passed"

readonly NATIVE_TEST_ROOT="${TEMPLATE_ROOT}/build/debug/extension/duckdb_api"
for test_binary in \
    duckdb_api_connector_tests \
    duckdb_api_scan_planner_tests \
    duckdb_api_fixture_decoder_tests \
    duckdb_api_fixture_stream_tests \
    duckdb_api_adapter_tests; do
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
        "${NATIVE_TEST_ROOT}/${test_binary}"
done
(
    cd "${TEMPLATE_ROOT}"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
        ./build/debug/test/unittest --require duckdb_api 'test/*'
)
"${PROJECT_SOURCE}/scripts/verify-loadable-inventory.sh" "${ARTIFACT}"
"${REPOSITORY_ROOT}/scripts/sanitizer-source-guard.sh" "${BUILD_ROOT}/final-source-guard"

mkdir -p "${OUTPUT_ROOT}"
cp "${ARTIFACT}" "${OUTPUT_ROOT}/duckdb_api.duckdb_extension"
cp "${COMPILE_COMMANDS}" "${OUTPUT_ROOT}/compile_commands.json"
cp "${FLAGS_REPORT}" "${OUTPUT_ROOT}/sanitizer-flags.txt"
python3 -I - "${REPOSITORY_ROOT}" "${BUILD_ROOT}" "${OUTPUT_ROOT}" <<'PY'
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

repo, build, output = map(pathlib.Path, sys.argv[1:])
artifact = output / "duckdb_api.duckdb_extension"
compile_commands = output / "compile_commands.json"
flags_report = output / "sanitizer-flags.txt"
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
pins = json.loads((repo / "release/0.1.0/pins.json").read_text())
dependencies = json.loads((build / "observed-dependencies.json").read_text())
tag = pins["project"]["tag"]
command = lambda *args: subprocess.check_output(args, text=True).strip()
manifest = {
    "schema": "duckdb_api/release-evidence/v1",
    "cell": pins["sanitizer_cell"]["name"],
    "source": {
        "commit": command("git", "-C", str(repo), "rev-parse", "HEAD"),
        "tree": command("git", "-C", str(repo), "rev-parse", "HEAD^{tree}"),
        "tag": tag,
        "clean": True,
    },
    "dependencies": dependencies,
    "toolchain": {
        "architecture": platform.machine(),
        "compiler": command("clang++", "--version").splitlines()[0],
        "cmake": command(str(build / "tools/cmake/bin/cmake"), "--version").splitlines()[0],
        "ninja": command(str(build / "tools/ninja/ninja"), "--version"),
        "cxx_standard": "11",
        "sanitizers": {"address": True, "undefined": True},
    },
    "content": {
        "compiled_connector_sha256": digest(repo / "fixtures/example/compiled_connector.snapshot"),
        "fixture_sha256": digest(repo / "fixtures/example/items.json"),
    },
    "sanitizer_compile_commands": {
        "filename": compile_commands.name,
        "sha256": digest(compile_commands),
    },
    "sanitizer_flags_report": {
        "filename": flags_report.name,
        "sha256": digest(flags_report),
    },
    "artifact": {
        "filename": artifact.name,
        "sha256": digest(artifact),
        "size": artifact.stat().st_size,
    },
}
(output / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
PY
sha256sum "${OUTPUT_ROOT}/manifest.json" >"${OUTPUT_ROOT}/manifest.sha256"
python3 -I "${REPOSITORY_ROOT}/scripts/verify-sanitizer-manifest.py" \
    "${REPOSITORY_ROOT}" "${OUTPUT_ROOT}/manifest.json" "${OUTPUT_ROOT}/manifest.sha256" \
    "${OUTPUT_ROOT}/duckdb_api.duckdb_extension" "${OUTPUT_ROOT}/compile_commands.json" \
    "${OUTPUT_ROOT}/sanitizer-flags.txt"
chmod 0444 "${OUTPUT_ROOT}/duckdb_api.duckdb_extension" "${OUTPUT_ROOT}/compile_commands.json" \
    "${OUTPUT_ROOT}/sanitizer-flags.txt" "${OUTPUT_ROOT}/manifest.json" "${OUTPUT_ROOT}/manifest.sha256"
echo "linux_amd64-sanitized inner measurements passed"
