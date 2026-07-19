#!/usr/bin/env bash

set -euo pipefail

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/duckdb-api-native-dev.XXXXXX")"
trap 'rm -rf "${TEMP_ROOT}"' EXIT

fail() {
    echo "$1" >&2
    exit 1
}

assert_output() {
    local label="$1"
    local expected="$2"
    shift 2
    local observed
    if ! observed="$("$@" 2>&1)"; then
        echo "${label} failed:" >&2
        echo "${observed}" >&2
        exit 1
    fi
    if [[ "${observed}" != "${expected}" ]]; then
        echo "${label} produced unexpected output:" >&2
        echo "expected: ${expected}" >&2
        echo "observed: ${observed}" >&2
        exit 1
    fi
}

create_native_probe() {
    local root="$1"
    mkdir -p "${root}/scripts"
    cp "${REPOSITORY_ROOT}/Makefile" "${root}/Makefile"
    cat >"${root}/scripts/native-dev.sh" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
printf 'native:%s' "$1"
if [[ "$#" -gt 1 ]]; then
    printf ':%s' "$2"
fi
printf '\n'
printf 'native:%s\n' "$1" >>"${ROUTING_LOG}"
SH
    chmod +x "${root}/scripts/native-dev.sh"
}

assert_native_routes() {
    local root="$1"
    local log="$2"
    local target
    assert_output "default native route" "native:help" \
        env ROUTING_LOG="${log}" make -s -C "${root}"
    for target in help bootstrap; do
        assert_output "${target} native route" "native:${target}" \
            env ROUTING_LOG="${log}" make -s -C "${root}" "${target}" PROFILE=release
    done
    for target in build test demo paths verify; do
        assert_output "${target} native route" "native:${target}:release" \
            env ROUTING_LOG="${log}" make -s -C "${root}" "${target}" PROFILE=release
    done
}

help_output="$(make -s -C "${REPOSITORY_ROOT}" help)"
for target in bootstrap build test demo paths verify; do
    if [[ "${help_output}" != *"make ${target}"* ]]; then
        echo "native developer help omitted ${target}" >&2
        exit 1
    fi
done
if [[ "${help_output}" == *"make shell"* ]] || [[ "${help_output}" != *"not release evidence"* ]]; then
    echo "native developer help misstated the supported or evidence surface" >&2
    exit 1
fi

if "${REPOSITORY_ROOT}/scripts/native-dev.sh" build unsupported >"${TEMP_ROOT}/invalid.out" 2>&1; then
    echo "native developer command accepted an unsupported profile" >&2
    exit 1
fi
if ! grep -F "profile must be debug or release" "${TEMP_ROOT}/invalid.out" >/dev/null; then
    echo "native developer profile guard failed for the wrong reason" >&2
    cat "${TEMP_ROOT}/invalid.out" >&2
    exit 1
fi

readonly UNINITIALIZED_PROBE_ROOT="${TEMP_ROOT}/uninitialized layout with spaces"
readonly UNINITIALIZED_LOG="${TEMP_ROOT}/uninitialized.log"
create_native_probe "${UNINITIALIZED_PROBE_ROOT}"
assert_native_routes "${UNINITIALIZED_PROBE_ROOT}" "${UNINITIALIZED_LOG}"
rm -f "${UNINITIALIZED_LOG}"
if env ROUTING_LOG="${UNINITIALIZED_LOG}" \
    make -s -C "${UNINITIALIZED_PROBE_ROOT}" release \
    >"${TEMP_ROOT}/missing-upstream.out" 2>&1; then
    fail "uninitialized source layout accepted an upstream-only goal"
fi
if ! grep -F "Community/upstream goal(s) release require an initialized extension-ci-tools submodule" \
    "${TEMP_ROOT}/missing-upstream.out" >/dev/null; then
    echo "upstream-only goal failed without the expected initialization diagnostic" >&2
    cat "${TEMP_ROOT}/missing-upstream.out" >&2
    exit 1
fi
if [[ -e "${UNINITIALIZED_LOG}" ]]; then
    fail "uninitialized upstream-only failure partly executed native mode"
fi

readonly INITIALIZED_PROBE_ROOT="${TEMP_ROOT}/initialized layout with spaces"
readonly INITIALIZED_LOG="${TEMP_ROOT}/initialized.log"
create_native_probe "${INITIALIZED_PROBE_ROOT}"
mkdir -p "${INITIALIZED_PROBE_ROOT}/extension-ci-tools/makefiles"
cat >"${INITIALIZED_PROBE_ROOT}/extension-ci-tools/makefiles/duckdb_extension.Makefile" <<'MAKE'
.PHONY: release debug test_release template-mode-probe
release debug test_release template-mode-probe:
	@test "$(PROJ_DIR)" = "$(CURDIR)/"
	@test "$(EXT_CONFIG)" = "$(CURDIR)/extension_config.cmake"
	@printf 'upstream:%s\n' "$@"
	@printf 'upstream:%s\n' "$@" >>"$(ROUTING_LOG)"
MAKE
assert_native_routes "${INITIALIZED_PROBE_ROOT}" "${INITIALIZED_LOG}"
for target in release debug test_release template-mode-probe; do
    assert_output "${target} upstream route" "upstream:${target}" \
        env ROUTING_LOG="${INITIALIZED_LOG}" \
        make -s -C "${INITIALIZED_PROBE_ROOT}" "${target}"
done

rm -f "${INITIALIZED_LOG}"
if env ROUTING_LOG="${INITIALIZED_LOG}" \
    make -s -C "${INITIALIZED_PROBE_ROOT}" help release \
    >"${TEMP_ROOT}/mixed-goals.out" 2>&1; then
    fail "Makefile accepted mixed native and Community/upstream goals"
fi
if ! grep -F "native goal(s) help cannot be combined with Community/upstream goal(s) release" \
    "${TEMP_ROOT}/mixed-goals.out" >/dev/null; then
    echo "mixed goals failed without the expected routing diagnostic" >&2
    cat "${TEMP_ROOT}/mixed-goals.out" >&2
    exit 1
fi
if [[ -e "${INITIALIZED_LOG}" ]]; then
    fail "mixed-goal rejection partly executed a native or upstream recipe"
fi

python3 -I - "${REPOSITORY_ROOT}" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
pins = json.loads((root / "release/0.6.0/pins.json").read_text())
scripts = "\n".join(
    path.read_text()
    for path in (
        root / "scripts/native-dev.sh",
        root / "scripts/lib/native-dev-environment.sh",
        root / "scripts/lib/native-dev-build.sh",
    )
)
identities = [
    pins["dependencies"][name][field]
    for name, fields in {
        "duckdb": ("commit", "tree"),
        "extension_ci_tools": ("commit", "tree"),
        "extension_template": ("commit", "tree"),
    }.items()
    for field in fields
]
identities.extend(tool[field] for tool in pins["tools"].values() for field in ("url", "sha256"))
duplicated = [value for value in identities if value in scripts]
if duplicated:
    raise SystemExit(f"native developer script duplicated current identities: {duplicated!r}")
for path in (
    "dependencies.duckdb.commit",
    "dependencies.extension_ci_tools.commit",
    "dependencies.extension_template.commit",
    "tools.cmake_macos_universal.sha256",
    "tools.ninja_macos.sha256",
    "system_dependencies.macos_sdk.version",
    "system_dependencies.macos_sdk.build_version",
):
    if path not in scripts:
        raise SystemExit(f"native developer script does not read required pin: {path}")
if "release/0.6.0/pins.json" not in scripts:
    raise SystemExit("native developer workflow does not select current 0.6 pins")
if "release/0.1.0/pins.json" in scripts:
    raise SystemExit("native developer workflow reads historical 0.1 pins")
if "release/0.4.0/pins.json" in scripts:
    raise SystemExit("native developer workflow reads historical 0.4 pins")
if "verify-native-dependencies.py" not in scripts:
    raise SystemExit("native developer workflow omits dependency verification")
for required in (
    "src test cmake CMakeLists.txt",
    '"${stage}/cmake/" "${TEMPLATE_ROOT}/cmake/"',
):
    if required not in scripts:
        raise SystemExit("native developer workflow omits the CMake authority module")
PY

bash -n "${REPOSITORY_ROOT}/scripts/native-dev.sh"
bash -n "${REPOSITORY_ROOT}/scripts/lib/native-dev-environment.sh"
bash -n "${REPOSITORY_ROOT}/scripts/lib/native-dev-build.sh"
bash -n "${REPOSITORY_ROOT}/scripts/test-native-dev.sh"
echo "native developer workflow guards passed"
