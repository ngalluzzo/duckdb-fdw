#!/usr/bin/env python3
"""Independent reproduction evidence and byte-comparison tests."""

from __future__ import annotations

import json
import pathlib
import shutil
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from enablement_test_support import EnablementTestCase, REPRODUCTION_ROOT, sha256_bytes


class ReproductionTest(EnablementTestCase):
    def test_two_reproductions_equal_the_trusted_artifact(self) -> None:
        one = REPRODUCTION_ROOT / "evidence-one"
        two = REPRODUCTION_ROOT / "evidence-two"
        if not one.is_dir() or not two.is_dir():
            self.skipTest("retained two-workspace reproduction evidence is unavailable")
        completed = self.run_script("verify_reproduced_artifacts.py", one, two)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        result = json.loads(completed.stdout)
        self.assertTrue(result["artifacts_byte_identical"])
        self.assertTrue(result["all_match_trusted_artifact"])
        self.assertEqual(
            result["trusted_artifact_sha256"],
            "4f1a0678fd2a673b433af6248a34966cb8fd41d107d4c0b3b97ca71eb35179ea",
        )
        self.assertEqual(
            {entry["artifact_sha256"] for entry in result["evidence"]},
            {result["trusted_artifact_sha256"]},
        )

        aliased = self.run_script("verify_reproduced_artifacts.py", one, one)
        self.assertNotEqual(aliased.returncode, 0)
        self.assertIn("roots must be distinct", aliased.stderr)

        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            copied_one = root / "one"
            copied_two = root / "two"
            shutil.copytree(one, copied_one)
            shutil.copytree(two, copied_two)
            changed_artifact = copied_two / "duckdb_api.duckdb_extension"
            changed_artifact.chmod(0o644)
            changed = bytearray(changed_artifact.read_bytes())
            changed[0] ^= 0x01
            changed_artifact.write_bytes(changed)
            changed_manifest_path = copied_two / "manifest/manifest.json"
            changed_manifest_path.chmod(0o644)
            changed_manifest = json.loads(changed_manifest_path.read_text())
            changed_manifest["artifact"]["sha256"] = sha256_bytes(
                changed_artifact.read_bytes()
            )
            changed_manifest_path.write_text(
                json.dumps(changed_manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            changed_anchor_path = copied_two / "manifest/manifest.sha256"
            changed_anchor_path.chmod(0o644)
            changed_anchor_path.write_text(
                f"{sha256_bytes(changed_manifest_path.read_bytes())}  manifest.json\n",
                encoding="utf-8",
            )
            differing = self.run_script(
                "verify_reproduced_artifacts.py", copied_one, copied_two
            )
            self.assertEqual(differing.returncode, 0, differing.stderr)
            differing_result = json.loads(differing.stdout)
            self.assertFalse(differing_result["artifacts_byte_identical"])
            self.assertFalse(differing_result["all_match_trusted_artifact"])


if __name__ == "__main__":
    unittest.main()
