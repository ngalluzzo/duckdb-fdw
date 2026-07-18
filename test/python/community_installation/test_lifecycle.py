from __future__ import annotations

from dataclasses import replace
import unittest

try:
    from .lifecycle import (
        HostObservation,
        LifecycleError,
        verify_incompatible_refusal,
        verify_supported_lifecycle,
    )
    from .test_support import (
        FakeRunner,
        SHA_A,
        extension,
        public_contract,
        row,
    )
except ImportError:
    from lifecycle import (
        HostObservation,
        LifecycleError,
        verify_incompatible_refusal,
        verify_supported_lifecycle,
    )
    from test_support import FakeRunner, SHA_A, extension, public_contract, row


class LifecycleTests(unittest.TestCase):
    def supported_observations(self) -> list[HostObservation]:
        target = row()
        return [
            HostObservation(
                action="pre_install",
                process_token="process-1",
                ok=True,
                row=target,
                allow_unsigned_extensions=False,
                extension=None,
                function_registered=False,
            ),
            HostObservation(
                action="install",
                process_token="process-2",
                ok=True,
                row=target,
                allow_unsigned_extensions=False,
                extension=extension(loaded=False),
                function_registered=False,
            ),
            HostObservation(
                action="repeat_install",
                process_token="process-3",
                ok=True,
                row=target,
                allow_unsigned_extensions=False,
                extension=extension(loaded=False),
                function_registered=False,
            ),
            HostObservation(
                action="load_query",
                process_token="process-4",
                ok=True,
                row=target,
                allow_unsigned_extensions=False,
                extension=extension(loaded=True),
                function_registered=True,
                behavior=public_contract(),
            ),
        ]

    def test_proves_distinct_supported_lifecycle_states(self) -> None:
        runner = FakeRunner(self.supported_observations())
        result = verify_supported_lifecycle(
            runner, row(), SHA_A, public_contract()
        )
        self.assertEqual(len(result.observations), 4)
        self.assertEqual(
            runner.calls,
            [
                ("pre_install", "supported"),
                ("install", "supported"),
                ("repeat_install", "supported"),
                ("load_query", "supported"),
            ],
        )

    def test_rejects_install_side_effects_and_weakened_signature_policy(self) -> None:
        observations = self.supported_observations()
        observations[1] = replace(
            observations[1],
            allow_unsigned_extensions=True,
            extension=extension(loaded=True),
            function_registered=True,
        )
        with self.assertRaisesRegex(LifecycleError, "signature policy"):
            verify_supported_lifecycle(
                FakeRunner(observations), row(), SHA_A, public_contract()
            )

        observations = self.supported_observations()
        observations[1] = replace(
            observations[1],
            extension=extension(loaded=True),
            function_registered=True,
        )
        with self.assertRaisesRegex(LifecycleError, "identity drifted"):
            verify_supported_lifecycle(
                FakeRunner(observations), row(), SHA_A, public_contract()
            )

    def test_rejects_reinstall_drift_or_reused_process(self) -> None:
        observations = self.supported_observations()
        observations[2] = replace(
            observations[2], extension=extension(loaded=False, install_path="<other>")
        )
        with self.assertRaisesRegex(LifecycleError, "changed the installed destination"):
            verify_supported_lifecycle(
                FakeRunner(observations), row(), SHA_A, public_contract()
            )

        observations = self.supported_observations()
        observations[2] = replace(observations[2], process_token="process-2")
        with self.assertRaisesRegex(LifecycleError, "distinct fresh processes"):
            verify_supported_lifecycle(
                FakeRunner(observations), row(), SHA_A, public_contract()
            )

    def test_rejects_wrong_public_behavior_or_artifact(self) -> None:
        observations = self.supported_observations()
        observations[3] = replace(observations[3], behavior={"rows": []})
        with self.assertRaisesRegex(LifecycleError, "differs from the public contract"):
            verify_supported_lifecycle(
                FakeRunner(observations), row(), SHA_A, public_contract()
            )

        observations = self.supported_observations()
        observations[3] = replace(
            observations[3], extension=extension(loaded=True, artifact_sha256="b" * 64)
        )
        with self.assertRaisesRegex(LifecycleError, "identity drifted"):
            verify_supported_lifecycle(
                FakeRunner(observations), row(), SHA_A, public_contract()
            )

    def incompatible_observation(self) -> HostObservation:
        return HostObservation(
            action="incompatible",
            process_token="process-negative",
            ok=False,
            row=row(),
            allow_unsigned_extensions=False,
            extension=None,
            function_registered=False,
            diagnostic_category="version",
            diagnostic="artifact targets v1.5.4; current host is v1.5.3",
        )

    def test_requires_actionable_contained_incompatible_refusal(self) -> None:
        observation = verify_incompatible_refusal(
            FakeRunner([self.incompatible_observation()]),
            row(),
            required_facts=("v1.5.4", "v1.5.3"),
            forbidden=("top-secret",),
        )
        self.assertFalse(observation.ok)

        leaked = replace(
            self.incompatible_observation(),
            diagnostic="artifact targets v1.5.4; current host is v1.5.3 top-secret",
        )
        with self.assertRaisesRegex(LifecycleError, "private context"):
            verify_incompatible_refusal(
                FakeRunner([leaked]),
                row(),
                required_facts=("v1.5.4", "v1.5.3"),
                forbidden=("top-secret",),
            )

    def test_refusal_cannot_install_register_or_enable_unsigned(self) -> None:
        registered = replace(
            self.incompatible_observation(),
            extension=extension(loaded=False),
            function_registered=True,
        )
        with self.assertRaisesRegex(LifecycleError, "installation or registration"):
            verify_incompatible_refusal(
                FakeRunner([registered]), row(), required_facts=("v1.5.4",)
            )

        unsigned = replace(
            self.incompatible_observation(), allow_unsigned_extensions=True
        )
        with self.assertRaisesRegex(LifecycleError, "signature policy"):
            verify_incompatible_refusal(
                FakeRunner([unsigned]), row(), required_facts=("v1.5.4",)
            )


if __name__ == "__main__":
    unittest.main()
