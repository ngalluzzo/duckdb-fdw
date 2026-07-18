"""Stable focused entry point for the first Community Query slice."""

from __future__ import annotations

import pathlib
import unittest


def main() -> int:
    loader = unittest.defaultTestLoader
    suite = loader.discover(
        str(pathlib.Path(__file__).resolve().parent), pattern="test_[ilm]*.py"
    )
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
