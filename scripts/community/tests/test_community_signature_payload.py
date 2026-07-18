#!/usr/bin/env python3
"""Community archive, gzip, and signature-transition boundary tests."""

from __future__ import annotations

import gzip
import io
import pathlib
import stat
import sys
import unittest
from unittest import mock
import zipfile


HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from community_signature_payload import (  # noqa: E402
    EXTENSION_FILENAME,
    SIGNATURE_SIZE,
    decompress_served_extension,
    extract_unsigned_extension,
    verify_native_deployment_transition,
)
from record_format import AdmissionError  # noqa: E402


def archive(
    entries: list[tuple[str, bytes]],
    *,
    symlink: bool = False,
    compression: int = zipfile.ZIP_DEFLATED,
) -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        for name, payload in entries:
            info = zipfile.ZipInfo(name)
            info.compress_type = compression
            if symlink:
                info.create_system = 3
                info.external_attr = (stat.S_IFLNK | 0o777) << 16
            bundle.writestr(info, payload)
    return output.getvalue()


class CommunitySignaturePayloadTests(unittest.TestCase):
    def setUp(self) -> None:
        self.payload = b"content-identified extension payload\n"
        self.unsigned = self.payload + bytes(SIGNATURE_SIZE)
        self.signed = self.payload + (b"s" * SIGNATURE_SIZE)

    def test_extracts_one_exact_regular_extension(self) -> None:
        value = archive([(EXTENSION_FILENAME, self.unsigned)])
        self.assertEqual(extract_unsigned_extension(value), self.unsigned)

    def test_rejects_archive_inventory_paths_links_and_bounds(self) -> None:
        cases = (
            (archive([]), "inventory"),
            (
                archive(
                    [
                        (EXTENSION_FILENAME, self.unsigned),
                        ("extra.txt", b"extra"),
                    ]
                ),
                "inventory",
            ),
            (archive([(f"nested/{EXTENSION_FILENAME}", self.unsigned)]), "path"),
            (
                archive([(f"{EXTENSION_FILENAME}.wasm", self.unsigned)]),
                "path",
            ),
            (archive([(EXTENSION_FILENAME, self.unsigned)], symlink=True), "regular"),
        )
        for value, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(AdmissionError, message):
                    extract_unsigned_extension(value)
        with self.assertRaisesRegex(AdmissionError, "size authority"):
            extract_unsigned_extension(
                archive([(EXTENSION_FILENAME, self.unsigned)]),
                maximum_bytes=len(self.unsigned) - 1,
            )

    def test_rejects_multiple_entries_before_zipfile_materializes_them(self) -> None:
        value = archive(
            [(EXTENSION_FILENAME, self.unsigned), ("extra", b"extra")]
        )
        with mock.patch(
            "community_signature_payload.zipfile.ZipFile",
            side_effect=AssertionError("ZipFile must not run"),
        ):
            with self.assertRaisesRegex(AdmissionError, "inventory"):
                extract_unsigned_extension(value)

    def test_decodes_one_bounded_gzip_member(self) -> None:
        compressed = gzip.compress(self.signed, mtime=0)
        self.assertEqual(decompress_served_extension(compressed), self.signed)
        with self.assertRaisesRegex(AdmissionError, "trailing or concatenated"):
            decompress_served_extension(compressed + gzip.compress(b"extra", mtime=0))
        with self.assertRaisesRegex(AdmissionError, "size authority"):
            decompress_served_extension(
                compressed, maximum_bytes=len(self.signed) - 1
            )
        with self.assertRaisesRegex(AdmissionError, "valid gzip"):
            decompress_served_extension(b"not gzip")
        with self.assertRaisesRegex(AdmissionError, "incomplete"):
            decompress_served_extension(compressed[:-1])

    def test_binds_only_the_signature_block_transition(self) -> None:
        build_archive = archive([(EXTENSION_FILENAME, self.unsigned)])
        served_gzip = gzip.compress(self.signed, mtime=0)
        transition = verify_native_deployment_transition(
            build_archive, served_gzip
        )
        self.assertEqual(transition.size_in_bytes, len(self.signed))
        self.assertNotEqual(
            transition.unsigned_artifact_sha256,
            transition.signed_artifact_sha256,
        )
        self.assertEqual(
            transition.shared_payload_sha256,
            __import__("hashlib").sha256(self.payload).hexdigest(),
        )
        self.assertEqual(
            transition.build_archive_sha256,
            __import__("hashlib").sha256(build_archive).hexdigest(),
        )
        self.assertEqual(
            transition.served_gzip_sha256,
            __import__("hashlib").sha256(served_gzip).hexdigest(),
        )

    def test_transport_encodings_remain_distinct_custody_identities(self) -> None:
        stored = archive(
            [(EXTENSION_FILENAME, self.unsigned)],
            compression=zipfile.ZIP_STORED,
        )
        deflated = archive(
            [(EXTENSION_FILENAME, self.unsigned)],
            compression=zipfile.ZIP_DEFLATED,
        )
        first = verify_native_deployment_transition(
            stored, gzip.compress(self.signed, mtime=0)
        )
        second = verify_native_deployment_transition(
            deflated, gzip.compress(self.signed, mtime=1)
        )
        self.assertNotEqual(first.build_archive_sha256, second.build_archive_sha256)
        self.assertNotEqual(first.served_gzip_sha256, second.served_gzip_sha256)
        self.assertEqual(
            first.unsigned_artifact_sha256,
            second.unsigned_artifact_sha256,
        )
        self.assertEqual(first.signed_artifact_sha256, second.signed_artifact_sha256)

    def test_rejects_payload_size_and_signature_drift(self) -> None:
        cases = (
            (self.unsigned, self.signed + b"x", "size"),
            (
                b"changed" + self.unsigned[7:],
                self.signed,
                "differs before",
            ),
            (
                self.payload + (b"u" * SIGNATURE_SIZE),
                self.signed,
                "unsigned signature placeholder",
            ),
            (self.unsigned, self.unsigned, "still has an unsigned"),
        )
        for unsigned, signed, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(AdmissionError, message):
                    verify_native_deployment_transition(
                        archive([(EXTENSION_FILENAME, unsigned)]),
                        gzip.compress(signed, mtime=0),
                    )


if __name__ == "__main__":
    unittest.main()
