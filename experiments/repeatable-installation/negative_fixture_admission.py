"""Validate and reproduce the versioned negative-fixture inventory."""

from __future__ import annotations

import json
import pathlib
from dataclasses import dataclass

from verified_manifest import (
    exact_keys,
    file_sha256,
    integer_value,
    object_value,
    require,
    sha256_value,
)


NEGATIVE_FIXTURE_SCHEMA = "duckdb_api/installability-negative-fixtures/v1"
EXTENSION_FOOTER_SIZE = 512
FOOTER_FIELD_SIZE = 32
PLATFORM_OFFSET_IN_FOOTER = 192


@dataclass(frozen=True)
class VerifiedNegativeFixtures:
    """Content identities and platform labels proven from recorded mutations."""

    inventory_sha256: str
    wrong_platform_artifact_sha256: str
    corrupted_artifact_sha256: str
    wrong_platform: str


def canonical_json(value: object) -> bytes:
    """Serialize the provider inventory in its versioned canonical form."""

    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def zero_padded_field(value: object, label: str) -> bytes:
    require(isinstance(value, str) and value, f"{label} is not a string")
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as error:
        raise AssertionError(f"{label} is not ASCII") from error
    require(
        len(encoded) < FOOTER_FIELD_SIZE,
        f"{label} does not fit a zero-padded footer field",
    )
    return encoded + bytes(FOOTER_FIELD_SIZE - len(encoded))


def fixture_record(
    inventory: dict[str, object], path: pathlib.Path, label: str
) -> dict[str, object]:
    fixtures = object_value(inventory.get("fixtures"), "negative fixture inventory")
    record = object_value(fixtures.get(path.name), f"{label} inventory record")
    exact_keys(record, {"mutation", "sha256", "size"}, f"{label} inventory record")
    digest = sha256_value(record.get("sha256"), f"{label} inventory digest")
    size = integer_value(record.get("size"), f"{label} inventory size")
    require(path.stat().st_size == size, f"{label} size differs from its inventory")
    require(file_sha256(path) == digest, f"{label} digest differs from its inventory")
    return record


def verify_wrong_platform_fixture(
    original: bytes,
    path: pathlib.Path,
    record: dict[str, object],
    source_platform: str,
) -> str:
    mutation = object_value(record.get("mutation"), "wrong-platform mutation")
    exact_keys(
        mutation,
        {"after", "before", "offset", "operation", "region"},
        "wrong-platform mutation",
    )
    require(
        mutation.get("operation") == "replace-zero-padded-footer-field"
        and mutation.get("region") == "metadata-platform",
        "wrong-platform mutation operation drifted",
    )
    before = mutation.get("before")
    after = mutation.get("after")
    require(before == source_platform, "wrong-platform source platform drifted")
    require(after != before, "wrong-platform mutation does not change the platform")
    before_field = zero_padded_field(before, "wrong-platform before value")
    after_field = zero_padded_field(after, "wrong-platform after value")
    offset = integer_value(mutation.get("offset"), "wrong-platform mutation offset")
    expected_offset = len(original) - EXTENSION_FOOTER_SIZE + PLATFORM_OFFSET_IN_FOOTER
    require(
        len(original) > EXTENSION_FOOTER_SIZE and offset == expected_offset,
        "wrong-platform mutation does not target the DuckDB footer platform field",
    )
    require(
        original[offset : offset + FOOTER_FIELD_SIZE] == before_field,
        "release artifact platform field differs from the fixture inventory",
    )
    reproduced = bytearray(original)
    reproduced[offset : offset + FOOTER_FIELD_SIZE] = after_field
    require(
        path.read_bytes() == reproduced,
        "wrong-platform fixture contains changes outside its recorded footer field",
    )
    assert isinstance(after, str)
    return after


def verify_corrupted_fixture(
    original: bytes, path: pathlib.Path, record: dict[str, object]
) -> None:
    mutation = object_value(record.get("mutation"), "corrupted fixture mutation")
    exact_keys(
        mutation,
        {"after", "before", "offset", "operation", "region"},
        "corrupted fixture mutation",
    )
    require(
        mutation.get("operation") == "xor-0x01"
        and mutation.get("region") == "body",
        "corrupted fixture mutation operation drifted",
    )
    offset = integer_value(mutation.get("offset"), "corrupted fixture mutation offset")
    before = integer_value(mutation.get("before"), "corrupted fixture before byte")
    after = integer_value(mutation.get("after"), "corrupted fixture after byte")
    footer_start = len(original) - EXTENSION_FOOTER_SIZE
    require(0 <= offset < footer_start, "corrupted fixture mutation is not in the body")
    require(original[offset] == before, "corrupted fixture before byte drifted")
    require(before ^ 0x01 == after, "corrupted fixture is not an xor-0x01 mutation")
    reproduced = bytearray(original)
    reproduced[offset] ^= 0x01
    require(
        path.read_bytes() == reproduced,
        "corrupted fixture contains changes outside its recorded body byte",
    )


def verify_negative_fixtures(
    *,
    artifact: pathlib.Path,
    inventory_path: pathlib.Path,
    wrong_platform_artifact: pathlib.Path,
    corrupted_artifact: pathlib.Path,
    expected_digest: str,
    expected_size: int,
    source_platform: str,
) -> VerifiedNegativeFixtures:
    """Verify inventory custody and independently reproduce both outputs."""

    raw_inventory = inventory_path.read_bytes()
    try:
        inventory_value = json.loads(raw_inventory)
    except json.JSONDecodeError as error:
        raise AssertionError("negative fixture inventory is not valid JSON") from error
    inventory = object_value(inventory_value, "negative fixture inventory")
    require(
        raw_inventory == canonical_json(inventory),
        "negative fixture inventory is not canonical JSON",
    )
    exact_keys(
        inventory,
        {"fixtures", "schema", "source"},
        "negative fixture inventory",
    )
    require(
        inventory.get("schema") == NEGATIVE_FIXTURE_SCHEMA,
        "negative fixture inventory schema drifted",
    )
    source = object_value(inventory.get("source"), "negative fixture source")
    exact_keys(source, {"filename", "sha256", "size"}, "negative fixture source")
    require(
        source
        == {
            "filename": artifact.name,
            "sha256": expected_digest,
            "size": expected_size,
        },
        "negative fixture source identity differs from the verified artifact",
    )
    fixtures = object_value(inventory.get("fixtures"), "negative fixture inventory")
    require(
        corrupted_artifact.name == artifact.name,
        "corrupted fixture must preserve the release artifact filename",
    )
    expected_filenames = {
        wrong_platform_artifact.name,
        corrupted_artifact.name,
    }
    require(
        set(fixtures) == expected_filenames and len(expected_filenames) == 2,
        f"negative fixture filenames drifted: {sorted(fixtures)!r}",
    )

    wrong_record = fixture_record(
        inventory,
        wrong_platform_artifact,
        "wrong-platform fixture",
    )
    corrupted_record = fixture_record(
        inventory,
        corrupted_artifact,
        "corrupted fixture",
    )
    original = artifact.read_bytes()
    wrong_platform = verify_wrong_platform_fixture(
        original,
        wrong_platform_artifact,
        wrong_record,
        source_platform,
    )
    verify_corrupted_fixture(original, corrupted_artifact, corrupted_record)
    wrong_digest = file_sha256(wrong_platform_artifact)
    corrupted_digest = file_sha256(corrupted_artifact)
    require(
        wrong_digest != expected_digest and corrupted_digest != expected_digest,
        "negative fixture equals the release artifact",
    )
    return VerifiedNegativeFixtures(
        inventory_sha256=file_sha256(inventory_path),
        wrong_platform_artifact_sha256=wrong_digest,
        corrupted_artifact_sha256=corrupted_digest,
        wrong_platform=wrong_platform,
    )
