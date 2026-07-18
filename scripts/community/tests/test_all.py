#!/usr/bin/env python3
"""Run the complete bounded Community Enablement provider test slice."""

from __future__ import annotations

import importlib
import pathlib
import sys
import unittest


HERE = pathlib.Path(__file__).resolve().parent
MODULES = (
    "test_record_boundaries",
    "test_tracked_records",
    "test_descriptor_proposal",
    "test_descriptor_authority",
    "test_descriptor_filesystem",
    "test_dependency_admission",
    "test_candidate_admission",
)


def suite() -> unittest.TestSuite:
    sys.path.insert(0, str(HERE))
    loader = unittest.defaultTestLoader
    result = unittest.TestSuite()
    for name in MODULES:
        result.addTests(loader.loadTestsFromModule(importlib.import_module(name)))
    return result


def main() -> int:
    result = unittest.TextTestRunner(verbosity=2).run(suite())
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
