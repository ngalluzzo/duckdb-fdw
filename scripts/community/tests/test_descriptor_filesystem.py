#!/usr/bin/env python3
"""Community descriptor filesystem-boundary tests."""

from __future__ import annotations

import pathlib
import sys


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from descriptor_test_support import DescriptorFixture  # noqa: E402


class DescriptorFilesystemTest(DescriptorFixture):
    def test_input_symlink_and_wrong_filename_are_rejected(self) -> None:
        linked = self.root / "linked" / "description.yml"
        linked.parent.mkdir()
        linked.symlink_to(self.proposal)
        result = self.run_descriptor(self.root / "symlink-input", proposal=linked)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must not be a symlink", result.stderr)

        renamed = self.root / "proposal.yml"
        renamed.write_bytes(self.proposal_bytes())
        result = self.run_descriptor(self.root / "renamed-input", proposal=renamed)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("filename is invalid", result.stderr)

    def test_reused_or_symlink_output_root_is_rejected(self) -> None:
        output = self.root / "used-output"
        first = self.run_descriptor(output)
        self.assertEqual(first.returncode, 0, first.stderr)
        second = self.run_descriptor(output)
        self.assertNotEqual(second.returncode, 0)
        self.assertIn("output root must be new", second.stderr)

        target = self.root / "output-target"
        target.mkdir()
        linked = self.root / "linked-output"
        linked.symlink_to(target, target_is_directory=True)
        result = self.run_descriptor(linked)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("output root must be new", result.stderr)


if __name__ == "__main__":
    import unittest

    unittest.main()
