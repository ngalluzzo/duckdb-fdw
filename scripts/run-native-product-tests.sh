#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -lt 1 || "$#" -gt 2 ]]; then
    echo "usage: run-native-product-tests.sh NEW_BUILD_ROOT [debug|release]" >&2
    exit 2
fi

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BUILD_ROOT="$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$1")"
readonly BUILD_PROFILE="${2:-debug}"
readonly TEMPLATE_COMMIT="cfaf3e236008e782d27f4341b0ee036002d0a449"
readonly DUCKDB_COMMIT="08e34c447bae34eaee3723cac61f2878b6bdf787"
readonly CI_TOOLS_COMMIT="b777c70d30942cca5bef62d6d4fa23a13362f398"
readonly TEMPLATE_ROOT="${BUILD_ROOT}/extension-template"
readonly TOOL_ROOT="${BUILD_ROOT}/tools"
readonly CMAKE_ARCHIVE="${TOOL_ROOT}/cmake.tar.gz"
readonly NINJA_ARCHIVE="${TOOL_ROOT}/ninja.zip"
readonly CMAKE_ROOT="${TOOL_ROOT}/cmake"
readonly NINJA_ROOT="${TOOL_ROOT}/ninja"
readonly CMAKE_BIN="${CMAKE_ROOT}/CMake.app/Contents/bin/cmake"
readonly NINJA_BIN="${NINJA_ROOT}/ninja"
readonly ARTIFACT="${TEMPLATE_ROOT}/build/${BUILD_PROFILE}/extension/duckdb_api/duckdb_api.duckdb_extension"
readonly PYTHON_ENV="${BUILD_ROOT}/python-1.5.4"

if [[ -e "${BUILD_ROOT}" ]]; then
    echo "build root must not already exist: ${BUILD_ROOT}" >&2
    exit 1
fi
if [[ "${BUILD_PROFILE}" != "debug" && "${BUILD_PROFILE}" != "release" ]]; then
    echo "build profile must be debug or release" >&2
    exit 2
fi
if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    echo "native product test records only the Darwin arm64 product cell" >&2
    exit 1
fi
if [[ "$(sw_vers -productVersion)" != "26.5.1" ]]; then
    echo "native product test requires macOS 26.5.1" >&2
    exit 1
fi
if [[ "$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')" != "3.14" ]]; then
    echo "native product test requires Python 3.14 for its pinned DuckDB wheel" >&2
    exit 1
fi
for command in c++ curl git make nm python3 rsync shasum strings sw_vers tar unzip; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "missing required command: ${command}" >&2
        exit 1
    fi
done
if [[ "$(c++ --version | head -n 1)" != Apple\ clang\ version\ 17.0.0* ]]; then
    echo "native product test requires Apple clang 17.0.0" >&2
    exit 1
fi

mkdir -p "${TOOL_ROOT}" "${CMAKE_ROOT}" "${NINJA_ROOT}" "${TEMPLATE_ROOT}"
curl -fL --retry 3 -o "${CMAKE_ARCHIVE}" \
    https://github.com/Kitware/CMake/releases/download/v4.1.2/cmake-4.1.2-macos-universal.tar.gz
printf '%s  %s\n' '3be85f5b999e327b1ac7d804cbc9acd767059e9f603c42ec2765f6ab68fbd367' \
    "${CMAKE_ARCHIVE}" | shasum -a 256 -c -
curl -fL --retry 3 -o "${NINJA_ARCHIVE}" \
    https://github.com/ninja-build/ninja/releases/download/v1.13.0/ninja-mac.zip
printf '%s  %s\n' '229314c7ef65e9c11d19f84e5f4bb374105a4f21f64ed55e8f403df765ab52a7' \
    "${NINJA_ARCHIVE}" | shasum -a 256 -c -
tar -xzf "${CMAKE_ARCHIVE}" -C "${CMAKE_ROOT}" --strip-components=1
unzip -oq "${NINJA_ARCHIVE}" -d "${NINJA_ROOT}"
if [[ "$("${CMAKE_BIN}" --version | head -n 1)" != "cmake version 4.1.2" ]]; then
    echo "bootstrapped CMake identity mismatch" >&2
    exit 1
fi
if [[ "$("${NINJA_BIN}" --version)" != "1.13.0" ]]; then
    echo "bootstrapped Ninja identity mismatch" >&2
    exit 1
fi

git init "${TEMPLATE_ROOT}"
git -C "${TEMPLATE_ROOT}" remote add origin https://github.com/duckdb/extension-template.git
git -C "${TEMPLATE_ROOT}" fetch --depth 1 origin "${TEMPLATE_COMMIT}"
git -C "${TEMPLATE_ROOT}" checkout --detach FETCH_HEAD
git -C "${TEMPLATE_ROOT}" submodule update --init --recursive --depth 1
if [[ "$(git -C "${TEMPLATE_ROOT}" rev-parse HEAD)" != "${TEMPLATE_COMMIT}" ]] ||
   [[ "$(git -C "${TEMPLATE_ROOT}/duckdb" rev-parse HEAD)" != "${DUCKDB_COMMIT}" ]] ||
   [[ "$(git -C "${TEMPLATE_ROOT}/extension-ci-tools" rev-parse HEAD)" != "${CI_TOOLS_COMMIT}" ]]; then
    echo "pinned source identity mismatch" >&2
    exit 1
fi
if [[ "$(git -C "${TEMPLATE_ROOT}" rev-parse 'HEAD^{tree}')" != "e9d306f3e8b0eed85e3cfc132066769baab9f6d2" ]] ||
   [[ "$(git -C "${TEMPLATE_ROOT}/duckdb" rev-parse 'HEAD^{tree}')" != "33c1f40f6421fd8f79912a6ce96722feca538c61" ]] ||
   [[ "$(git -C "${TEMPLATE_ROOT}/extension-ci-tools" rev-parse 'HEAD^{tree}')" != "d3295344af82907cad620374596c44479f35f410" ]]; then
    echo "pinned source tree identity mismatch" >&2
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

if [[ "${BUILD_PROFILE}" == "debug" ]]; then
    readonly EXTRA_FLAGS_NAME="EXT_DEBUG_FLAGS"
else
    readonly EXTRA_FLAGS_NAME="EXT_RELEASE_FLAGS"
fi
readonly CLEAN_HOME="${BUILD_ROOT}/home"
readonly CLEAN_TMP="${BUILD_ROOT}/tmp"
readonly CLEAN_CACHE="${BUILD_ROOT}/cache"
readonly PYTHON_DIR="$(dirname "$(command -v python3)")"
mkdir -p "${CLEAN_HOME}" "${CLEAN_TMP}" "${CLEAN_CACHE}"
env -i HOME="${CLEAN_HOME}" TMPDIR="${CLEAN_TMP}" XDG_CACHE_HOME="${CLEAN_CACHE}" \
    PATH="${CMAKE_ROOT}/CMake.app/Contents/bin:${NINJA_ROOT}:${PYTHON_DIR}:/usr/bin:/bin:/usr/sbin:/sbin" \
    GEN=ninja DISABLE_SANITIZER=1 DUCKDB_PLATFORM=osx_arm64 \
    OVERRIDE_GIT_DESCRIBE=v1.5.4-0-g08e34c447b \
    make -C "${TEMPLATE_ROOT}" "${EXTRA_FLAGS_NAME}=-DCMAKE_CXX_STANDARD=11 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" \
        "${BUILD_PROFILE}"

readonly NATIVE_TEST_ROOT="${TEMPLATE_ROOT}/build/${BUILD_PROFILE}/extension/duckdb_api"
"${NATIVE_TEST_ROOT}/duckdb_api_connector_tests"
"${NATIVE_TEST_ROOT}/duckdb_api_scan_planner_tests"
"${NATIVE_TEST_ROOT}/duckdb_api_fixture_decoder_tests"
"${NATIVE_TEST_ROOT}/duckdb_api_fixture_stream_tests"
"${NATIVE_TEST_ROOT}/duckdb_api_adapter_tests"
(
    cd "${TEMPLATE_ROOT}"
    "./build/${BUILD_PROFILE}/test/unittest" --require duckdb_api 'test/*'
)
"${REPOSITORY_ROOT}/scripts/verify-loadable-inventory.sh" "${ARTIFACT}"

python3 -m venv "${PYTHON_ENV}"
"${PYTHON_ENV}/bin/python3" -m pip install --disable-pip-version-check --no-deps \
    --require-hashes -r "${REPOSITORY_ROOT}/test/python/requirements-macos-py314.txt"
"${PYTHON_ENV}/bin/python3" "${REPOSITORY_ROOT}/test/python/artifact_contract.py" "${ARTIFACT}"

echo "native product tests passed"
echo "artifact=${ARTIFACT}"
echo "artifact_sha256=$(shasum -a 256 "${ARTIFACT}" | awk '{print $1}')"
