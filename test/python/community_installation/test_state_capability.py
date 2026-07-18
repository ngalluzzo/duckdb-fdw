from __future__ import annotations

import os
import pathlib
import tempfile
import unittest
from unittest import mock

try:
    from . import state_capability as state_capability_module
    from .state_capability import StateCapability, StateCapabilityError
except ImportError:
    import state_capability as state_capability_module
    from state_capability import StateCapability, StateCapabilityError


class StateCapabilityTests(unittest.TestCase):
    def state_root(self, root: pathlib.Path) -> pathlib.Path:
        state_root = root / "state"
        state_root.mkdir()
        return state_root.resolve()

    def materialize_database(
        self, capability: StateCapability, payload: bytes = b"safe"
    ) -> None:
        descriptor = os.open(
            capability.child_database,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL,
            0o600,
            dir_fd=capability.descriptor,
        )
        try:
            os.write(descriptor, payload)
        finally:
            os.close(descriptor)

    def test_restores_child_leaves_and_closes_descriptor(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            capability = StateCapability.admit(self.state_root(root), "supported")
            retained_descriptor = capability.descriptor
            self.materialize_database(capability)
            capability.finish()
            self.assertEqual(capability.database.read_bytes(), b"safe")
            self.assertTrue(capability.extension_directory.is_dir())
            with self.assertRaises(OSError):
                os.fstat(retained_descriptor)

    def test_rejects_public_leaf_swaps_without_clobbering_targets(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            state_root = self.state_root(root)
            capability = StateCapability.admit(state_root, "supported")
            outside_database = root / "outside.duckdb"
            outside_extensions = root / "outside-extensions"
            outside_extensions.mkdir()
            capability.database.symlink_to(outside_database)
            capability.extension_directory.symlink_to(
                outside_extensions, target_is_directory=True
            )
            self.materialize_database(capability)
            with self.assertRaisesRegex(
                StateCapabilityError, "persistent host state could not be restored"
            ):
                capability.finish()
            self.assertFalse(outside_database.exists())
            self.assertEqual(tuple(outside_extensions.iterdir()), ())
            self.assertTrue(capability.database.is_symlink())
            self.assertTrue(capability.extension_directory.is_symlink())

    def test_symlink_to_regular_race_preserves_data_and_reusable_state(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            capability = StateCapability.admit(self.state_root(root), "supported")
            outside_database = root / "outside.duckdb"
            capability.database.symlink_to(outside_database)
            self.materialize_database(capability)
            rename_no_replace = state_capability_module._rename_no_replace

            def race(source: str, destination: str, descriptor: int) -> None:
                if destination == "query.duckdb":
                    capability.database.unlink()
                    capability.database.write_bytes(b"racing caller data")
                rename_no_replace(source, destination, descriptor)

            with mock.patch.object(
                state_capability_module, "_rename_no_replace", side_effect=race
            ):
                with self.assertRaisesRegex(
                    StateCapabilityError, "persistent host state could not be restored"
                ):
                    capability.finish()
            self.assertEqual(capability.database.read_bytes(), b"racing caller data")
            self.assertTrue(capability.extension_directory.is_dir())

            readmitted = StateCapability.admit(
                capability.state_root, "supported"
            )
            readmitted.finish()
            self.assertEqual(readmitted.database.read_bytes(), b"racing caller data")

    def test_rejects_whole_public_state_directory_replacement(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            state_root = self.state_root(root)
            capability = StateCapability.admit(state_root, "supported")
            capability.state.rename(state_root / "detached")
            capability.state.mkdir()
            capability.extension_directory.mkdir()
            capability.database.write_bytes(b"decoy")
            self.materialize_database(capability)
            with self.assertRaisesRegex(StateCapabilityError, "directory was replaced"):
                capability.finish()
            self.assertEqual(capability.database.read_bytes(), b"decoy")
            self.assertEqual(
                (state_root / "detached/query.duckdb").read_bytes(), b"safe"
            )

    @unittest.skipUnless(hasattr(os, "symlink"), "requires symbolic links")
    def test_rejects_published_database_and_extension_leaf_swaps(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            state_root = self.state_root(root)
            capability = StateCapability.admit(state_root, "extensions")
            self.materialize_database(capability)
            capability.finish()
            capability.extension_directory.rmdir()
            external = root / "external"
            external.mkdir()
            capability.extension_directory.symlink_to(
                external, target_is_directory=True
            )
            with self.assertRaisesRegex(StateCapabilityError, "persistent host state"):
                StateCapability.admit(state_root, "extensions")

            capability = StateCapability.admit(state_root, "database")
            self.materialize_database(capability)
            capability.finish()
            capability.database.unlink()
            external_database = root / "external.duckdb"
            external_database.write_bytes(b"")
            capability.database.symlink_to(external_database)
            with self.assertRaisesRegex(StateCapabilityError, "persistent host state"):
                StateCapability.admit(state_root, "database")


if __name__ == "__main__":
    unittest.main()
