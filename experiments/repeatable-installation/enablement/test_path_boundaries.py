#!/usr/bin/env python3
"""Symlink-leaf and custody-output path boundary tests."""

from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from enablement_test_support import EnablementTestCase, sha256_bytes, synthetic_extension


class PathBoundaryTest(EnablementTestCase):
    def test_symlink_leaf_and_symlinked_output_parent_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            target = root / "manifest-target.json"
            target.write_text("{}\n", encoding="utf-8")
            linked_manifest = root / "manifest.json"
            linked_manifest.symlink_to(target)
            artifact = root / "duckdb_api.duckdb_extension"
            artifact.write_bytes(synthetic_extension())
            anchor = root / "manifest.sha256"
            anchor.write_text(
                f"{sha256_bytes(target.read_bytes())}  manifest.json\n",
                encoding="utf-8",
            )
            refused_leaf = self.run_script(
                "verify_bundle.py", linked_manifest, artifact, anchor
            )
            self.assertNotEqual(refused_leaf.returncode, 0)
            self.assertIn("symlink leaf", refused_leaf.stderr)

            manifest, real_artifact, real_anchor = self.real_triple()
            canonical_parent = root / "canonical-parent"
            canonical_parent.mkdir()
            linked_parent = root / "linked-parent"
            linked_parent.symlink_to(canonical_parent, target_is_directory=True)
            refused_parent = self.run_script(
                "assemble_bundle.py",
                "--artifact",
                real_artifact,
                "--manifest",
                manifest,
                "--manifest-anchor",
                real_anchor,
                "--output",
                linked_parent / "bundle",
            )
            self.assertNotEqual(refused_parent.returncode, 0)
            self.assertIn("output parent must not be a symlink leaf", refused_parent.stderr)


if __name__ == "__main__":
    unittest.main()
