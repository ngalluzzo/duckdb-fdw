#!/usr/bin/env python3
"""Single-read filesystem and offline Git plumbing boundary tests."""

from __future__ import annotations

import hashlib
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

import git_snapshot  # noqa: E402
import record_format  # noqa: E402


class RecordBoundaryTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_canonical_input_is_opened_and_read_once(self) -> None:
        path = self.root / "record.json"
        payload = b'{\n  "value": "stable"\n}\n'
        path.write_bytes(payload)
        real_open = os.open
        opened: list[pathlib.Path] = []

        def observe_open(target: object, flags: int, mode: int = 0o777) -> int:
            opened.append(pathlib.Path(target))
            return real_open(target, flags, mode)

        with mock.patch.object(record_format.os, "open", side_effect=observe_open):
            value, digest = record_format.load_canonical_object(path, "test record")
        self.assertEqual(value, {"value": "stable"})
        self.assertEqual(digest, hashlib.sha256(payload).hexdigest())
        self.assertEqual(opened, [path])

    def test_anchor_verification_reads_each_input_once(self) -> None:
        record = self.root / "candidate.json"
        anchor = self.root / "candidate.sha256"
        payload = b'{\n  "status": "stable"\n}\n'
        record.write_bytes(payload)
        anchor.write_text(
            f"{hashlib.sha256(payload).hexdigest()}  candidate.json\n",
            encoding="ascii",
        )
        original = record_format.read_regular_bytes
        observed: list[pathlib.Path] = []

        def observe(path: pathlib.Path, label: str, maximum_bytes: int = record_format.MAX_JSON_BYTES) -> bytes:
            observed.append(path)
            return original(path, label, maximum_bytes)

        with mock.patch.object(record_format, "read_regular_bytes", side_effect=observe):
            value, _digest, _anchor_digest = record_format.verify_anchored_object(
                record, anchor, "candidate.json", "candidate"
            )
        self.assertEqual(value, {"status": "stable"})
        self.assertEqual(observed, [record, anchor])

    def test_git_plumbing_disables_replace_objects_and_lazy_fetch(self) -> None:
        completed = subprocess.CompletedProcess(
            args=[], returncode=0, stdout="", stderr=""
        )
        with mock.patch.object(git_snapshot.subprocess, "run", return_value=completed) as run:
            git_snapshot._git(self.root, "rev-parse", "HEAD")
        arguments = run.call_args.args[0]
        environment = run.call_args.kwargs["env"]
        self.assertIn("--no-replace-objects", arguments)
        self.assertEqual(environment["GIT_NO_REPLACE_OBJECTS"], "1")
        self.assertEqual(environment["GIT_NO_LAZY_FETCH"], "1")

    def test_filesystem_diagnostic_does_not_disclose_caller_path(self) -> None:
        missing = self.root / "sensitive" / "record.json"
        with self.assertRaises(record_format.AdmissionError) as context:
            record_format.load_canonical_object(missing, "test record")
        self.assertNotIn(str(self.root), str(context.exception))


if __name__ == "__main__":
    unittest.main()
