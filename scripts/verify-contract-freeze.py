#!/usr/bin/env python3
"""Verify the frozen 1.0.0 public contract against its authoritative sources."""

from __future__ import annotations

import pathlib
import sys

SCRIPT_ROOT = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_ROOT))

from contract_freeze import FreezeError, FreezePaths, verify_paths  # noqa: E402


def main() -> int:
    repository = SCRIPT_ROOT.parent
    try:
        verify_paths(FreezePaths(repository))
    except FreezeError as error:
        print(f"contract freeze failed: {error}", file=sys.stderr)
        return 1
    print("contract freeze passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
