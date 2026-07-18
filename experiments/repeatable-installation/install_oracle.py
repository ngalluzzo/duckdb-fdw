#!/usr/bin/env python3
"""Orchestrate the bounded 0.2 installation-and-trust trial.

The public CLI accepts only the explicit Engineering Enablement path contract.
Input admission, host subprocess mechanics, installation assertions, and
retained-evidence normalization live in cohesive sibling modules. Passing this
oracle remains decision evidence; it does not make the unsigned artifact a
supported or ordinarily trusted distribution.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import tempfile
from collections.abc import Sequence


# ``scripts/run-installability-trial.sh`` invokes this file with Python's
# isolated mode. Admit only this fixed Query-owned module directory so sibling
# imports work without exposing the working directory or user site packages.
MODULE_ROOT = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(MODULE_ROOT))

from evidence_output import build_retained_evidence
from host_process import HostRunner, isolated_environment
from installation_scenarios import run_installation_scenarios
from trial_inputs import TrialInputs, verify_trial_inputs
from trial_snapshot import create_trial_snapshot


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    """Preserve the experiment's explicit provider-path CLI."""

    parser = argparse.ArgumentParser(
        description="Run the bounded repeatable-installation trust oracle."
    )
    parser.add_argument("--supported-python", required=True)
    parser.add_argument("--mismatch-python", required=True)
    parser.add_argument("--artifact", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--manifest-anchor", required=True)
    parser.add_argument("--verifier", required=True)
    parser.add_argument("--negative-fixture-inventory", required=True)
    parser.add_argument("--wrong-platform-artifact", required=True)
    parser.add_argument("--corrupted-artifact", required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    inputs = TrialInputs.admit(
        supported_python=args.supported_python,
        mismatch_python=args.mismatch_python,
        artifact=args.artifact,
        manifest=args.manifest,
        manifest_anchor=args.manifest_anchor,
        verifier=args.verifier,
        negative_fixture_inventory=args.negative_fixture_inventory,
        wrong_platform_artifact=args.wrong_platform_artifact,
        corrupted_artifact=args.corrupted_artifact,
    )
    query_host = MODULE_ROOT / "query_host.py"
    query_host = query_host.resolve(strict=True)
    repository = MODULE_ROOT.parents[1]
    with tempfile.TemporaryDirectory(prefix="duckdb-api-installation-") as directory:
        # Canonicalize macOS's /var -> /private/var alias before snapshots or
        # DuckDB metadata are created, so retained redaction replaces a whole
        # path instead of leaving a machine-local `/private` prefix.
        trial_root = pathlib.Path(directory).resolve(strict=True)
        snapshot = create_trial_snapshot(inputs, query_host, trial_root / "snapshot")
        verified = verify_trial_inputs(snapshot.inputs)
        runner = HostRunner(
            snapshot.query_host,
            isolated_environment(trial_root / "environment"),
        )
        scenarios = run_installation_scenarios(
            runner,
            snapshot.inputs,
            verified,
            trial_root,
        )
        retained_result = build_retained_evidence(
            snapshot.inputs,
            verified,
            scenarios,
            trial_root,
            repository,
        )

    print(json.dumps(retained_result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
