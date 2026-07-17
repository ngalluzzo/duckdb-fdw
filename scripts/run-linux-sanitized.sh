#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 2 ]]; then
    echo "usage: run-linux-sanitized.sh NEW_BUILD_ROOT NEW_OUTPUT_ROOT" >&2
    exit 2
fi

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BUILD_ROOT="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$1")"
readonly OUTPUT_ROOT="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$2")"
readonly TEMPLATE_ROOT="${BUILD_ROOT}/extension-template"
readonly TOOL_ROOT="${BUILD_ROOT}/tools"
readonly CMAKE_ROOT="${TOOL_ROOT}/cmake"
readonly NINJA_ROOT="${TOOL_ROOT}/ninja"
readonly CMAKE_BIN="${CMAKE_ROOT}/bin/cmake"
readonly NINJA_BIN="${NINJA_ROOT}/ninja"
readonly ARTIFACT="${TEMPLATE_ROOT}/build/debug/extension/duckdb_api/duckdb_api.duckdb_extension"

if [[ -e "${BUILD_ROOT}" || -e "${OUTPUT_ROOT}" ]]; then
    echo "sanitizer build and output roots must not already exist" >&2
    exit 1
fi
if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
    echo "linux_amd64-sanitized requires a native Linux x86_64 executor" >&2
    exit 1
fi
if ! grep -q '^VERSION_ID="24.04"$' /etc/os-release; then
    echo "linux_amd64-sanitized requires the pinned Ubuntu 24.04 image" >&2
    exit 1
fi
"${REPOSITORY_ROOT}/scripts/sanitizer-source-guard.sh" \
    "${DUCKDB_API_SANITIZER_SOURCE_COMMIT:-}" "${DUCKDB_API_VERIFIED_BASE_IMAGE:-}"
for command in clang clang++ curl git make nm python3 rsync sha256sum strings tar unzip; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "missing required sanitizer-cell command: ${command}" >&2
        exit 1
    fi
done

mkdir -p "${TEMPLATE_ROOT}" "${TOOL_ROOT}" "${CMAKE_ROOT}" "${NINJA_ROOT}"
curl -fL --retry 3 -o "${TOOL_ROOT}/cmake.tar.gz" \
    https://github.com/Kitware/CMake/releases/download/v4.1.2/cmake-4.1.2-linux-x86_64.tar.gz
printf '%s  %s\n' '773cc679c3a7395413bd096523f8e5d6c39f8718af4e12eb4e4195f72f35e4ab' \
    "${TOOL_ROOT}/cmake.tar.gz" | sha256sum -c -
curl -fL --retry 3 -o "${TOOL_ROOT}/ninja.zip" \
    https://github.com/ninja-build/ninja/releases/download/v1.13.0/ninja-linux.zip
printf '%s  %s\n' '46aa8ad0a431e9b6e39f6ca0abc47bf8b13be094e3ac7d0f6d39e94bbdc746f9' \
    "${TOOL_ROOT}/ninja.zip" | sha256sum -c -
tar -xzf "${TOOL_ROOT}/cmake.tar.gz" -C "${CMAKE_ROOT}" --strip-components=1
unzip -oq "${TOOL_ROOT}/ninja.zip" -d "${NINJA_ROOT}"

git init "${TEMPLATE_ROOT}"
git -C "${TEMPLATE_ROOT}" remote add origin https://github.com/duckdb/extension-template.git
git -C "${TEMPLATE_ROOT}" fetch --depth 1 origin cfaf3e236008e782d27f4341b0ee036002d0a449
git -C "${TEMPLATE_ROOT}" checkout --detach FETCH_HEAD
git -C "${TEMPLATE_ROOT}" submodule update --init --recursive --depth 1
if [[ "$(git -C "${TEMPLATE_ROOT}" rev-parse HEAD)" != "cfaf3e236008e782d27f4341b0ee036002d0a449" ]] ||
   [[ "$(git -C "${TEMPLATE_ROOT}" rev-parse 'HEAD^{tree}')" != "e9d306f3e8b0eed85e3cfc132066769baab9f6d2" ]] ||
   [[ "$(git -C "${TEMPLATE_ROOT}/duckdb" rev-parse HEAD)" != "08e34c447bae34eaee3723cac61f2878b6bdf787" ]] ||
   [[ "$(git -C "${TEMPLATE_ROOT}/duckdb" rev-parse 'HEAD^{tree}')" != "33c1f40f6421fd8f79912a6ce96722feca538c61" ]] ||
   [[ "$(git -C "${TEMPLATE_ROOT}/extension-ci-tools" rev-parse HEAD)" != "b777c70d30942cca5bef62d6d4fa23a13362f398" ]] ||
   [[ "$(git -C "${TEMPLATE_ROOT}/extension-ci-tools" rev-parse 'HEAD^{tree}')" != "d3295344af82907cad620374596c44479f35f410" ]]; then
    echo "sanitizer-cell source identity mismatch" >&2
    exit 1
fi
rsync -a --delete "${REPOSITORY_ROOT}/src/" "${TEMPLATE_ROOT}/src/"
rsync -a --delete "${REPOSITORY_ROOT}/test/" "${TEMPLATE_ROOT}/test/"
rsync -a --delete "${REPOSITORY_ROOT}/fixtures/" "${TEMPLATE_ROOT}/fixtures/"
cp "${REPOSITORY_ROOT}/CMakeLists.txt" "${REPOSITORY_ROOT}/Makefile" \
    "${REPOSITORY_ROOT}/extension_config.cmake" "${TEMPLATE_ROOT}/"
"${CMAKE_BIN}" -E rm -f "${TEMPLATE_ROOT}/vcpkg.json"
"${REPOSITORY_ROOT}/scripts/verify-source-identities.py"
"${REPOSITORY_ROOT}/scripts/check-native-format.sh"

readonly CLEAN_HOME="${BUILD_ROOT}/home"
readonly CLEAN_TMP="${BUILD_ROOT}/tmp"
readonly CLEAN_CACHE="${BUILD_ROOT}/cache"
mkdir -p "${CLEAN_HOME}" "${CLEAN_TMP}" "${CLEAN_CACHE}"
env -i HOME="${CLEAN_HOME}" TMPDIR="${CLEAN_TMP}" XDG_CACHE_HOME="${CLEAN_CACHE}" \
    PATH="${CMAKE_ROOT}/bin:${NINJA_ROOT}:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
    CC=/usr/bin/clang CXX=/usr/bin/clang++ GEN=ninja \
    DUCKDB_PLATFORM=linux_amd64 OVERRIDE_GIT_DESCRIBE=v1.5.4-0-g08e34c447b \
    make -C "${TEMPLATE_ROOT}" \
        EXT_DEBUG_FLAGS='-DCMAKE_CXX_STANDARD=11 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DENABLE_SANITIZER=TRUE -DENABLE_UBSAN=TRUE' \
        debug

readonly COMPILE_COMMANDS="${TEMPLATE_ROOT}/build/debug/compile_commands.json"
"${REPOSITORY_ROOT}/scripts/verify-sanitizer-flags.py" "${COMPILE_COMMANDS}"
readonly DISABLED_CANARY="${BUILD_ROOT}/compile-commands-without-sanitizers.json"
python3 - "${COMPILE_COMMANDS}" "${DISABLED_CANARY}" <<'PY'
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
source = source.replace("-fsanitize=address", "").replace("-fsanitize=undefined", "")
pathlib.Path(sys.argv[2]).write_text(source)
PY
if "${REPOSITORY_ROOT}/scripts/verify-sanitizer-flags.py" "${DISABLED_CANARY}" >/dev/null 2>&1; then
    echo "disabled-sanitizer negative canary unexpectedly passed" >&2
    exit 1
fi
echo "disabled-sanitizer negative canary passed"

ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    "${TEMPLATE_ROOT}/build/debug/extension/duckdb_api/duckdb_api_contract_tests"
(
    cd "${TEMPLATE_ROOT}"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
        ./build/debug/test/unittest --require duckdb_api 'test/*'
)
"${REPOSITORY_ROOT}/scripts/verify-loadable-inventory.sh" "${ARTIFACT}"

mkdir -p "${OUTPUT_ROOT}"
cp "${ARTIFACT}" "${OUTPUT_ROOT}/duckdb_api.duckdb_extension"
python3 - "${REPOSITORY_ROOT}" "${TEMPLATE_ROOT}" "${OUTPUT_ROOT}" <<'PY'
import hashlib
import json
import pathlib
import platform
import subprocess
import sys

repo, template, output = map(pathlib.Path, sys.argv[1:])
artifact = output / "duckdb_api.duckdb_extension"
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
pins = json.loads((repo / "release/0.1.0/pins.json").read_text())
compile_commands = template / "build/debug/compile_commands.json"
manifest = {
    "schema": "duckdb_api/release-evidence/v1",
    "cell": "linux_amd64-sanitized",
    "source": {
        "commit": subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip(),
        "tree": subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD^{tree}"], text=True).strip(),
        "clean": True,
    },
    "base_image": pins["sanitizer_cell"]["base_image"],
    "dependencies": pins["dependencies"],
    "toolchain": {
        "architecture": platform.machine(),
        "compiler": subprocess.check_output(["clang++", "--version"], text=True).splitlines()[0],
        "cmake": subprocess.check_output([str(template.parent / "tools/cmake/bin/cmake"), "--version"], text=True).splitlines()[0],
        "ninja": subprocess.check_output([str(template.parent / "tools/ninja/ninja"), "--version"], text=True).strip(),
        "cxx_standard": "11",
        "sanitizers": {"address": True, "undefined": True},
    },
    "content": {
        "compiled_connector_sha256": digest(repo / "fixtures/example/compiled_connector.snapshot"),
        "fixture_sha256": digest(repo / "fixtures/example/items.json"),
    },
    "production_compile_commands_sha256": digest(compile_commands),
    "artifact": {"filename": artifact.name, "sha256": digest(artifact), "size": artifact.stat().st_size},
}
(output / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
PY
sha256sum "${OUTPUT_ROOT}/manifest.json" >"${OUTPUT_ROOT}/manifest.sha256"
chmod 0444 "${OUTPUT_ROOT}/duckdb_api.duckdb_extension" "${OUTPUT_ROOT}/manifest.json" \
    "${OUTPUT_ROOT}/manifest.sha256"
echo "linux_amd64-sanitized evidence passed"
