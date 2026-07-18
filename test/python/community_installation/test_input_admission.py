from __future__ import annotations

import hashlib
import json
import pathlib
import tempfile
import unittest
from unittest import mock

try:
    from . import input_admission as admission
    from .input_admission import AdmissionError, admit_candidate
    from .test_support import candidate_document, canonical_bytes, write_candidate
except ImportError:
    import input_admission as admission
    from input_admission import AdmissionError, admit_candidate
    from test_support import candidate_document, canonical_bytes, write_candidate


class CandidateAdmissionTests(unittest.TestCase):
    def test_accepts_exact_anchored_candidate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            candidate, anchor = write_candidate(pathlib.Path(directory))
            admitted = admit_candidate(candidate, anchor)
            self.assertEqual(admitted.source_tag, "v0.2.0")
            self.assertEqual(admitted.duckdb.version, "1.5.4")
            self.assertEqual(
                admitted.sha256, hashlib.sha256(candidate.read_bytes()).hexdigest()
            )

    def test_rejects_tampered_bytes_and_malformed_anchor(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            candidate, anchor = write_candidate(root)
            candidate.write_bytes(candidate.read_bytes() + b" ")
            with self.assertRaisesRegex(AdmissionError, "does not match"):
                admit_candidate(candidate, anchor)
            candidate, anchor = write_candidate(root)
            anchor.write_text("a" * 64 + " candidate.json\n", encoding="ascii")
            with self.assertRaisesRegex(AdmissionError, "invalid syntax"):
                admit_candidate(candidate, anchor)

    def test_rejects_noncanonical_or_ambiguous_json(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            document = candidate_document()
            candidate = root / "candidate.json"
            candidate.write_text(json.dumps(document), encoding="utf-8")
            digest = hashlib.sha256(candidate.read_bytes()).hexdigest()
            anchor = root / "candidate.sha256"
            anchor.write_text(f"{digest}  candidate.json\n", encoding="ascii")
            with self.assertRaisesRegex(AdmissionError, "not canonical"):
                admit_candidate(candidate, anchor)

            duplicate = canonical_bytes(document).replace(
                b'{\n  "community"',
                b'{\n  "schema": "duckdb_api/community-candidate/v1",\n  "community"',
                1,
            )
            candidate.write_bytes(duplicate)
            digest = hashlib.sha256(duplicate).hexdigest()
            anchor.write_text(f"{digest}  candidate.json\n", encoding="ascii")
            with self.assertRaisesRegex(AdmissionError, "duplicate key"):
                admit_candidate(candidate, anchor)

    def test_rejects_unknown_fields_and_reserved_identity_drift(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            document = candidate_document()
            document["unexpected"] = True
            candidate, anchor = write_candidate(root, document)
            with self.assertRaisesRegex(AdmissionError, "fields differ"):
                admit_candidate(candidate, anchor)

            document = candidate_document()
            project = document["project"]
            assert isinstance(project, dict)
            project["version"] = "0.1.0"
            candidate, anchor = write_candidate(root, document)
            with self.assertRaisesRegex(AdmissionError, "must be '0.2.0'"):
                admit_candidate(candidate, anchor)

    def test_rejects_credentialed_repository_and_symlink_leaf(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            document = candidate_document()
            project = document["project"]
            assert isinstance(project, dict)
            project["repository"] = "https://secret@example.invalid/repository"
            candidate, anchor = write_candidate(root, document)
            with self.assertRaisesRegex(AdmissionError, "uncredentialed HTTPS"):
                admit_candidate(candidate, anchor)

            candidate, anchor = write_candidate(root)
            link_root = root / "link"
            link_root.mkdir()
            link = link_root / "candidate.json"
            link.symlink_to(candidate)
            with self.assertRaisesRegex(AdmissionError, "must not be a symlink"):
                admit_candidate(link, anchor)

    def test_rejects_renamed_candidate_or_anchor(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            candidate, anchor = write_candidate(root)
            renamed_candidate = root / "other.json"
            candidate.rename(renamed_candidate)
            with self.assertRaisesRegex(AdmissionError, "named candidate.json"):
                admit_candidate(renamed_candidate, anchor)

            candidate, anchor = write_candidate(root)
            renamed_anchor = root / "other.sha256"
            anchor.rename(renamed_anchor)
            with self.assertRaisesRegex(AdmissionError, "named candidate.sha256"):
                admit_candidate(candidate, renamed_anchor)

    def test_rejects_regular_file_replacement_before_open(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            candidate, anchor = write_candidate(root)
            replacement = root / "replacement.json"
            replacement.write_bytes(candidate.read_bytes())
            original_lstat = pathlib.Path.lstat
            replaced = False

            def replace_after_lstat(path: pathlib.Path):
                nonlocal replaced
                metadata = original_lstat(path)
                if path == candidate and not replaced:
                    replaced = True
                    replacement.replace(candidate)
                return metadata

            with mock.patch.object(pathlib.Path, "lstat", replace_after_lstat):
                with self.assertRaisesRegex(AdmissionError, "changed before"):
                    admit_candidate(candidate, anchor)

    def test_rejects_symlink_replacement_without_nofollow(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            candidate, anchor = write_candidate(root)
            replacement = root / "replacement.json"
            replacement.write_bytes(candidate.read_bytes())
            original_lstat = pathlib.Path.lstat
            replaced = False

            def replace_after_lstat(path: pathlib.Path):
                nonlocal replaced
                metadata = original_lstat(path)
                if path == candidate and not replaced:
                    replaced = True
                    candidate.unlink()
                    candidate.symlink_to(replacement)
                return metadata

            with (
                mock.patch.object(pathlib.Path, "lstat", replace_after_lstat),
                mock.patch.object(admission.os, "O_NOFOLLOW", 0),
            ):
                with self.assertRaisesRegex(AdmissionError, "changed before"):
                    admit_candidate(candidate, anchor)


if __name__ == "__main__":
    unittest.main()
