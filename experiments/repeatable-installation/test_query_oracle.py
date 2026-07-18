#!/usr/bin/env python3
"""Run every responsibility-focused Query installation-oracle test module."""

from __future__ import annotations

import pathlib
import sys
import unittest


MODULE_ROOT = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(MODULE_ROOT))

TEST_MODULES = (
    "test_trial_inputs",
    "test_trial_snapshot",
    "test_negative_fixture_admission",
    "test_verifier_process",
    "test_host_process",
    "test_installation_scenarios",
    "test_evidence_output",
    "test_install_oracle",
)


def main() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromNames(TEST_MODULES)
    result = unittest.TextTestRunner().run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
