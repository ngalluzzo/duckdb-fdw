#!/usr/bin/env python3
"""Bundle assembly, repeatability, and directory-custody tests."""

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from enablement_test_support import EnablementTestCase, sha256_bytes


class BundleCustodyTest(EnablementTestCase):
    def test_two_bundle_assemblies_are_byte_identical(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            outputs = [self.assemble_real_bundle(root / name) for name in ("one", "two")]

            names = sorted(path.name for path in outputs[0].iterdir())
            self.assertEqual(
                names,
                [
                    "bundle.json",
                    "bundle.sha256",
                    "duckdb_api.duckdb_extension",
                    "manifest.json",
                    "manifest.sha256",
                ],
            )
            for name in names:
                self.assertEqual(
                    (outputs[0] / name).read_bytes(), (outputs[1] / name).read_bytes()
                )
            normalized_anchor = (outputs[0] / "manifest.sha256").read_text(
                encoding="utf-8"
            )
            self.assertEqual(
                normalized_anchor,
                "764be0f79b373c53f61926e96dd5b56ca51d1c775cbdd949d85a30ac58b8a4f9"
                "  manifest.json\n",
            )
            self.assertNotIn("/Users/", normalized_anchor)

    def test_bundle_root_rejects_inventory_tamper_and_extra_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            bundle = self.assemble_real_bundle(root / "bundle")
            accepted = self.run_script("verify_assembled_bundle.py", bundle)
            self.assertEqual(accepted.returncode, 0, accepted.stderr)

            extra = bundle / "unexpected"
            extra.write_text("not allowlisted\n", encoding="utf-8")
            refused_extra = self.run_script("verify_assembled_bundle.py", bundle)
            self.assertNotEqual(refused_extra.returncode, 0)
            self.assertIn("directory inventory drifted", refused_extra.stderr)
            extra.unlink()

            inventory = bundle / "bundle.json"
            inventory.chmod(0o644)
            inventory.write_bytes(inventory.read_bytes() + b" ")
            refused_tamper = self.run_script("verify_assembled_bundle.py", bundle)
            self.assertNotEqual(refused_tamper.returncode, 0)
            self.assertIn("bundle inventory anchor mismatch", refused_tamper.stderr)

            absolute_bundle = self.assemble_real_bundle(root / "absolute-anchor")
            manifest_anchor = absolute_bundle / "manifest.sha256"
            manifest_anchor.chmod(0o644)
            manifest_anchor.write_text(
                "764be0f79b373c53f61926e96dd5b56ca51d1c775cbdd949d85a30ac58b8a4f9"
                "  /Users/alice/build/manifest.json\n",
                encoding="utf-8",
            )
            bundle_inventory_path = absolute_bundle / "bundle.json"
            bundle_inventory_path.chmod(0o644)
            bundle_inventory = json.loads(bundle_inventory_path.read_text())
            bundle_inventory["files"]["manifest.sha256"] = {
                "sha256": sha256_bytes(manifest_anchor.read_bytes()),
                "size": manifest_anchor.stat().st_size,
            }
            bundle_inventory_path.write_text(
                json.dumps(bundle_inventory, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            bundle_anchor = absolute_bundle / "bundle.sha256"
            bundle_anchor.chmod(0o644)
            bundle_anchor.write_text(
                f"{sha256_bytes(bundle_inventory_path.read_bytes())}  bundle.json\n",
                encoding="utf-8",
            )
            refused_absolute = self.run_script(
                "verify_assembled_bundle.py", absolute_bundle
            )
            self.assertNotEqual(refused_absolute.returncode, 0)
            self.assertIn("not the normalized relative record", refused_absolute.stderr)


if __name__ == "__main__":
    unittest.main()
