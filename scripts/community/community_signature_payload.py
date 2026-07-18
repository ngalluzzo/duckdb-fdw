"""Verify the byte-preserving native Community signing transition.

Community CI uploads a ZIP containing one extension whose final 256 bytes are
the unsigned placeholder. DuckDB's deployment script preserves every earlier
byte, replaces that placeholder with its signature, and gzip-compresses the
served native artifact. Wasm uses a different filename and compression path and
is deliberately outside this contract. This module proves only byte derivation;
cryptographic acceptance belongs to Query's stock-DuckDB lifecycle oracle.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import io
import stat
import struct
import zipfile
import zlib

from record_format import AdmissionError, require


SIGNATURE_SIZE = 256
MAX_ARCHIVE_BYTES = 512 * 1024 * 1024
MAX_EXTENSION_BYTES = 512 * 1024 * 1024
MAX_CENTRAL_DIRECTORY_BYTES = 64 * 1024
EXTENSION_FILENAME = "duckdb_api.duckdb_extension"
EOCD = struct.Struct("<4s4H2LH")
EOCD_SIGNATURE = b"PK\x05\x06"


@dataclass(frozen=True)
class SignatureTransition:
    """Transport and content identities spanning one native deployment."""

    build_archive_sha256: str
    unsigned_artifact_sha256: str
    shared_payload_sha256: str
    served_gzip_sha256: str
    signed_artifact_sha256: str
    size_in_bytes: int


def _sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _preflight_archive(archive: bytes) -> None:
    """Bound the uncompressed ZIP directory before ``ZipFile`` materializes it."""

    window_start = max(0, len(archive) - (EOCD.size + 65535))
    offset = archive.rfind(EOCD_SIGNATURE, window_start)
    require(offset >= 0, "build artifact archive has no bounded ZIP directory")
    require(
        offset + EOCD.size <= len(archive),
        "build artifact archive ZIP directory is truncated",
    )
    (
        signature,
        disk_number,
        central_disk,
        entries_on_disk,
        total_entries,
        central_size,
        central_offset,
        comment_size,
    ) = EOCD.unpack_from(archive, offset)
    require(signature == EOCD_SIGNATURE, "build artifact archive ZIP directory drifted")
    require(
        offset + EOCD.size + comment_size == len(archive),
        "build artifact archive ZIP trailer drifted",
    )
    require(
        disk_number == 0 and central_disk == 0,
        "build artifact archive must not span disks",
    )
    require(
        entries_on_disk == 1 and total_entries == 1,
        "build artifact archive inventory drifted",
    )
    require(
        central_size <= MAX_CENTRAL_DIRECTORY_BYTES,
        "build artifact archive ZIP directory is too large",
    )
    require(
        central_offset + central_size <= offset,
        "build artifact archive ZIP directory is invalid",
    )


def extract_unsigned_extension(
    archive: bytes, *, maximum_bytes: int = MAX_EXTENSION_BYTES
) -> bytes:
    """Extract exactly one bounded regular extension from an Actions ZIP."""

    require(maximum_bytes > SIGNATURE_SIZE, "extension size bound is invalid")
    require(len(archive) <= MAX_ARCHIVE_BYTES, "build artifact archive is too large")
    _preflight_archive(archive)
    try:
        with zipfile.ZipFile(io.BytesIO(archive), "r") as bundle:
            members = bundle.infolist()
            require(len(members) == 1, "build artifact archive inventory drifted")
            member = members[0]
            require(
                member.filename == EXTENSION_FILENAME,
                "build artifact archive extension path drifted",
            )
            mode = member.external_attr >> 16
            file_type = stat.S_IFMT(mode)
            require(not member.is_dir(), "build artifact archive contains a directory")
            require(
                file_type in {0, stat.S_IFREG},
                "build artifact archive extension must be a regular file",
            )
            require(
                SIGNATURE_SIZE < member.file_size <= maximum_bytes,
                "build artifact extension exceeds its size authority",
            )
            require(
                not (member.flag_bits & 0x1),
                "build artifact archive extension must not be encrypted",
            )
            with bundle.open(member, "r") as source:
                chunks: list[bytes] = []
                observed = 0
                while True:
                    chunk = source.read(min(64 * 1024, maximum_bytes + 1 - observed))
                    if not chunk:
                        break
                    chunks.append(chunk)
                    observed += len(chunk)
                    require(
                        observed <= maximum_bytes,
                        "build artifact extension exceeds its size authority",
                    )
            result = b"".join(chunks)
            require(
                len(result) == member.file_size,
                "build artifact extension size drifted while it was read",
            )
            return result
    except AdmissionError:
        raise
    except (OSError, zipfile.BadZipFile, RuntimeError) as error:
        raise AdmissionError("build artifact archive is invalid") from error


def decompress_served_extension(
    compressed: bytes, *, maximum_bytes: int = MAX_EXTENSION_BYTES
) -> bytes:
    """Decode one bounded gzip member and reject trailing or concatenated data."""

    require(maximum_bytes > SIGNATURE_SIZE, "extension size bound is invalid")
    require(
        len(compressed) <= MAX_ARCHIVE_BYTES,
        "served Community artifact is too large",
    )
    decoder = zlib.decompressobj(16 + zlib.MAX_WBITS)
    chunks: list[bytes] = []
    observed = 0
    pending = compressed
    try:
        while pending:
            chunk = decoder.decompress(pending, maximum_bytes + 1 - observed)
            chunks.append(chunk)
            observed += len(chunk)
            require(
                observed <= maximum_bytes,
                "served Community extension exceeds its size authority",
            )
            pending = decoder.unconsumed_tail
            if not pending:
                break
        tail = decoder.flush(maximum_bytes + 1 - observed)
        chunks.append(tail)
        observed += len(tail)
    except (OSError, zlib.error) as error:
        raise AdmissionError("served Community artifact is not valid gzip") from error
    require(
        observed <= maximum_bytes,
        "served Community extension exceeds its size authority",
    )
    require(decoder.eof, "served Community artifact gzip member is incomplete")
    require(
        decoder.unused_data == b"" and decoder.unconsumed_tail == b"",
        "served Community artifact has trailing or concatenated data",
    )
    result = b"".join(chunks)
    require(
        len(result) > SIGNATURE_SIZE,
        "served Community extension is smaller than its signature block",
    )
    return result


def verify_native_deployment_transition(
    build_archive: bytes,
    served_gzip: bytes,
    *,
    maximum_bytes: int = MAX_EXTENSION_BYTES,
) -> SignatureTransition:
    """Bind exact downloaded transports and their native extension payloads."""

    unsigned_artifact = extract_unsigned_extension(
        build_archive, maximum_bytes=maximum_bytes
    )
    signed_artifact = decompress_served_extension(
        served_gzip, maximum_bytes=maximum_bytes
    )

    require(
        len(unsigned_artifact) > SIGNATURE_SIZE,
        "unsigned extension is smaller than its signature block",
    )
    require(
        len(signed_artifact) == len(unsigned_artifact),
        "Community signing changed the extension size",
    )
    unsigned_payload = unsigned_artifact[:-SIGNATURE_SIZE]
    signed_payload = signed_artifact[:-SIGNATURE_SIZE]
    require(
        unsigned_payload == signed_payload,
        "served Community extension differs before its signature block",
    )
    require(
        unsigned_artifact[-SIGNATURE_SIZE:] == bytes(SIGNATURE_SIZE),
        "Community build extension lacks the unsigned signature placeholder",
    )
    require(
        signed_artifact[-SIGNATURE_SIZE:] != bytes(SIGNATURE_SIZE),
        "served Community extension still has an unsigned signature placeholder",
    )
    return SignatureTransition(
        build_archive_sha256=_sha256(build_archive),
        unsigned_artifact_sha256=_sha256(unsigned_artifact),
        shared_payload_sha256=_sha256(unsigned_payload),
        served_gzip_sha256=_sha256(served_gzip),
        signed_artifact_sha256=_sha256(signed_artifact),
        size_in_bytes=len(signed_artifact),
    )
