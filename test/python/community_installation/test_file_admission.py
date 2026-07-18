from __future__ import annotations

import hashlib
import pathlib
import stat
import tempfile
import unittest

try:
    from .file_admission import FileAdmissionError, stage_content_identified_file
except ImportError:
    from file_admission import FileAdmissionError, stage_content_identified_file


class FileAdmissionTests(unittest.TestCase):
    def test_stages_exact_bounded_bytes_once(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "source.extension"
            destination = root / "staged.extension"
            source.write_bytes(b"signed")
            staged = stage_content_identified_file(
                source,
                destination,
                expected_size=6,
                expected_sha256=hashlib.sha256(b"signed").hexdigest(),
                limit_bytes=6,
            )
            source.write_bytes(b"mutated")
            self.assertEqual(staged.path.read_bytes(), b"signed")
            self.assertEqual(stat.S_IMODE(staged.path.stat().st_mode), 0o400)
            with self.assertRaises(OSError):
                __import__("os").write(staged.descriptor, b"mutate")
            staged.close()

    def test_rejects_size_digest_and_existing_destination(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "source.extension"
            source.write_bytes(b"signed")
            for name, size, digest in (
                ("size", 5, hashlib.sha256(b"signed").hexdigest()),
                ("digest", 6, "0" * 64),
            ):
                with self.assertRaises(FileAdmissionError):
                    stage_content_identified_file(
                        source,
                        root / name,
                        expected_size=size,
                        expected_sha256=digest,
                        limit_bytes=6,
                    )
            existing = root / "existing"
            existing.write_bytes(b"preserve")
            with self.assertRaises(FileAdmissionError):
                stage_content_identified_file(
                    source,
                    existing,
                    expected_size=6,
                    expected_sha256=hashlib.sha256(b"signed").hexdigest(),
                    limit_bytes=6,
                )
            self.assertEqual(existing.read_bytes(), b"preserve")


if __name__ == "__main__":
    unittest.main()
