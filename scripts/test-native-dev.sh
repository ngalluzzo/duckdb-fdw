#!/usr/bin/env bash

set -euo pipefail

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/duckdb-api-native-dev.XXXXXX")"
trap 'rm -rf "${TEMP_ROOT}"' EXIT

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

mkdir -p "${TEMP_ROOT}/template/extension-ci-tools/makefiles"
cp "${REPOSITORY_ROOT}/Makefile" "${TEMP_ROOT}/template/Makefile"
cat >"${TEMP_ROOT}/template/extension-ci-tools/makefiles/duckdb_extension.Makefile" <<'MAKE'
.PHONY: template-mode-probe
template-mode-probe:
	@echo template-mode-ok
MAKE
if [[ "$(make -s -C "${TEMP_ROOT}/template" template-mode-probe)" != "template-mode-ok" ]]; then
    echo "root Makefile did not delegate in extension-template mode" >&2
    exit 1
fi

python3 - "${REPOSITORY_ROOT}" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
pins = json.loads((root / "release/0.1.0/pins.json").read_text())
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
    raise SystemExit(f"native developer script duplicated release identities: {duplicated!r}")
for path in (
    "dependencies.duckdb.commit",
    "dependencies.extension_ci_tools.commit",
    "dependencies.extension_template.commit",
    "tools.cmake_macos_universal.sha256",
    "tools.ninja_macos.sha256",
):
    if path not in scripts:
        raise SystemExit(f"native developer script does not read required pin: {path}")
PY

bash -n "${REPOSITORY_ROOT}/scripts/native-dev.sh"
bash -n "${REPOSITORY_ROOT}/scripts/lib/native-dev-environment.sh"
bash -n "${REPOSITORY_ROOT}/scripts/lib/native-dev-build.sh"
bash -n "${REPOSITORY_ROOT}/scripts/test-native-dev.sh"
echo "native developer workflow guards passed"
