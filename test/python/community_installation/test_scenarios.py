from __future__ import annotations

import unittest

try:
    from .lifecycle import LifecycleError
    from .scenarios import run_scenarios
    from .test_support import (
        FakeInitializationProbe,
        FakeRunner,
        SHA_A,
        incompatible_observation,
        public_contract,
        row,
        supported_observations,
    )
except ImportError:
    from lifecycle import LifecycleError
    from scenarios import run_scenarios
    from test_support import (
        FakeInitializationProbe,
        FakeRunner,
        SHA_A,
        incompatible_observation,
        public_contract,
        row,
        supported_observations,
    )


class ScenarioCompositionTests(unittest.TestCase):
    def test_runs_supported_and_refusal_in_separate_state_machines(self) -> None:
        supported = FakeRunner(supported_observations())
        incompatible = FakeRunner([incompatible_observation()])
        result = run_scenarios(
            supported_runner=supported,
            incompatible_runner=incompatible,
            supported_row=row(),
            incompatible_row=row(),
            artifact_sha256=SHA_A,
            public_contract=public_contract(),
            required_incompatible_facts=("v1.5.4", "v1.5.3"),
            initialization_probe=FakeInitializationProbe(),
        )
        self.assertEqual(len(result.supported.observations), 4)
        self.assertEqual(incompatible.calls, [("incompatible", "incompatible")])
        self.assertTrue(all(state == "supported" for _, state in supported.calls))

    def test_rejects_cross_scenario_process_reuse(self) -> None:
        incompatible = incompatible_observation(process_token="process-4")
        with self.assertRaisesRegex(LifecycleError, "reused a supported"):
            run_scenarios(
                supported_runner=FakeRunner(supported_observations()),
                incompatible_runner=FakeRunner([incompatible]),
                supported_row=row(),
                incompatible_row=row(),
                artifact_sha256=SHA_A,
                public_contract=public_contract(),
                required_incompatible_facts=("v1.5.4", "v1.5.3"),
                initialization_probe=FakeInitializationProbe(),
            )

    def test_requires_actionable_refusal_facts_before_running(self) -> None:
        supported = FakeRunner(supported_observations())
        with self.assertRaisesRegex(LifecycleError, "actionable identity"):
            run_scenarios(
                supported_runner=supported,
                incompatible_runner=FakeRunner([incompatible_observation()]),
                supported_row=row(),
                incompatible_row=row(),
                artifact_sha256=SHA_A,
                public_contract=public_contract(),
                required_incompatible_facts=(),
                initialization_probe=FakeInitializationProbe(),
            )
        self.assertEqual(supported.calls, [])

    def test_requires_independent_pre_initialization_observable(self) -> None:
        probe = FakeInitializationProbe(initialized=True)
        with self.assertRaisesRegex(LifecycleError, "native initialization"):
            run_scenarios(
                supported_runner=FakeRunner(supported_observations()),
                incompatible_runner=FakeRunner([incompatible_observation()]),
                supported_row=row(),
                incompatible_row=row(),
                artifact_sha256=SHA_A,
                public_contract=public_contract(),
                required_incompatible_facts=("v1.5.4", "v1.5.3"),
                initialization_probe=probe,
            )
        self.assertTrue(probe.armed)
        self.assertTrue(probe.checked)


if __name__ == "__main__":
    unittest.main()
