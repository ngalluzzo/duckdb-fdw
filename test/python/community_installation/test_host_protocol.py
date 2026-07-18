from __future__ import annotations

import json
import pathlib
import tempfile
import unittest

try:
    from .host_protocol import (
        HOST_OBSERVATION_SCHEMA,
        ProtocolError,
        normalize_diagnostic,
        parse_observation,
    )
    from .test_support import GIT_C, row
except ImportError:
    from host_protocol import (
        HOST_OBSERVATION_SCHEMA,
        ProtocolError,
        normalize_diagnostic,
        parse_observation,
    )
    from test_support import GIT_C, row


def frame() -> dict[str, object]:
    return {
        "action": "pre_install",
        "allow_unsigned_extensions": False,
        "behavior": None,
        "diagnostic": None,
        "diagnostic_category": None,
        "duckdb": ["v1.5.4", GIT_C[:10]],
        "extension": None,
        "function_registered": False,
        "ok": True,
        "platform": "osx_arm64",
        "process_token": "123",
        "schema": HOST_OBSERVATION_SCHEMA,
    }


class HostProtocolTests(unittest.TestCase):
    def test_runtime_identity_corroborates_content_bound_launcher(self) -> None:
        observation = parse_observation(json.dumps(frame()), row(), ())
        self.assertEqual(observation.row, row())
        self.assertFalse(observation.allow_unsigned_extensions)

    def test_rejects_extra_fields_and_wrong_host_identity(self) -> None:
        extra = frame()
        extra["ambient_environment"] = {"SECRET": "leaked"}
        with self.assertRaisesRegex(ProtocolError, "invalid shape"):
            parse_observation(json.dumps(extra), row(), ())

        wrong = frame()
        wrong["duckdb"] = ["v1.5.3", GIT_C[:10]]
        with self.assertRaisesRegex(ProtocolError, "wrong DuckDB"):
            parse_observation(json.dumps(wrong), row(), ())

        wrong_commit = frame()
        wrong_commit["duckdb"] = ["v1.5.4", "0" * 10]
        with self.assertRaisesRegex(ProtocolError, "wrong DuckDB"):
            parse_observation(json.dumps(wrong_commit), row(), ())

        wrong_platform = frame()
        wrong_platform["platform"] = "linux_amd64_gcc4"
        with self.assertRaisesRegex(ProtocolError, "wrong DuckDB"):
            parse_observation(json.dumps(wrong_platform), row(), ())

        unsafe_category = frame()
        unsafe_category["diagnostic_category"] = "credential"
        unsafe_category["diagnostic"] = "unexpected detail"
        with self.assertRaisesRegex(ProtocolError, "category is malformed"):
            parse_observation(json.dumps(unsafe_category), row(), ())

    def test_normalizes_canonical_paths_and_bounds_diagnostics(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            canonical = root.resolve()
            diagnostic = f"failed under {canonical}/private/file " + "x" * 100
            normalized = normalize_diagnostic(
                diagnostic, ((root, "<root>"),), limit=32
            )
            self.assertTrue(normalized.startswith("failed under <root>"))
            self.assertTrue(normalized.endswith("<truncated>"))
            self.assertNotIn(str(canonical), normalized)

        unsafe = frame()
        unsafe["ok"] = False
        unsafe["diagnostic_category"] = "version"
        unsafe["diagnostic"] = (
            "failed at /Users/alice/private/token with "
            "https://user:password@example.invalid for duckdb_api: "
            "artifact v1.5.4, host v1.5.3"
        )
        observation = parse_observation(json.dumps(unsafe), row(), ())
        self.assertEqual(
            observation.diagnostic,
            "version refusal: duckdb_api, v1.5.4, v1.5.3",
        )
        self.assertNotIn("alice", observation.diagnostic)
        self.assertNotIn("password", observation.diagnostic)


if __name__ == "__main__":
    unittest.main()
