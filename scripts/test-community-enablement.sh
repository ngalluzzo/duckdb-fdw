#!/usr/bin/env bash

set -euo pipefail

readonly REPOSITORY_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 -I -B \
  "${REPOSITORY_ROOT}/scripts/community/tests/test_all.py"
