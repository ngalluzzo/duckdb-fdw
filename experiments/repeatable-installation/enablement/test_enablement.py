#!/usr/bin/env python3
"""Run every focused Engineering Enablement provider test."""

from __future__ import annotations

import importlib
import pathlib
import sys
import unittest


HERE = pathlib.Path(__file__).resolve().parent
MODULES = (
    "test_trust_admission",
    "test_negative_fixtures",
    "test_bundle_custody",
    "test_reproduction",
    "test_path_boundaries",
)


def aggregate_suite() -> unittest.TestSuite:
    sys.path.insert(0, str(HERE))
    loader = unittest.defaultTestLoader
    suite = unittest.TestSuite()
    for name in MODULES:
        suite.addTests(loader.loadTestsFromModule(importlib.import_module(name)))
    return suite


def main() -> int:
    result = unittest.TextTestRunner(verbosity=1).run(aggregate_suite())
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
