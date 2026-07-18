#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    echo "usage: test-build-pin-binding.sh TEMPLATE_ROOT" >&2
    exit 2
fi

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "${REPOSITORY_ROOT}/scripts/lib/release-common.sh"
readonly TEMPLATE_ROOT="$(release_resolve_path "$1")"
readonly TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/duckdb-api-pin-binding.XXXXXX")"
trap 'rm -rf "${TEMP_ROOT}"' EXIT

git clone --quiet --no-hardlinks "${REPOSITORY_ROOT}" "${TEMP_ROOT}/mutated-source"
python3 -I - "${TEMP_ROOT}/mutated-source/release/0.1.0/pins.json" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
pins = json.loads(path.read_text())
pins["dependencies"]["duckdb"]["commit"] = "0" * 40
path.write_text(json.dumps(pins, indent=2, sort_keys=True) + "\n")
PY
if output="$(python3 -I "${TEMP_ROOT}/mutated-source/scripts/write-observed-dependencies.py" \
    "${TEMP_ROOT}/mutated-source" "${TEMPLATE_ROOT}" "${TEMP_ROOT}/observed.json" 2>&1)"; then
    echo "dependency-pin drift canary unexpectedly passed" >&2
    exit 1
fi
if [[ "${output}" != *"observed build dependencies do not match release pins"* ]]; then
    echo "dependency-pin drift canary failed for the wrong reason" >&2
    echo "${output}" >&2
    exit 1
fi
echo "dependency-pin drift canary passed"
