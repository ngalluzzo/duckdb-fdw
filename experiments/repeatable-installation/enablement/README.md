# Repeatable installation provider

This directory contains the artifact-custody side of the completed repeatable
installation experiment. It verifies the exact `v0.1.0` trial bundle, assembles
deterministic copies, derives known-invalid fixtures, and compares two retained
reproductions. It does not import DuckDB or interpret query results.

The narrow consumer command is:

```text
PYTHON verify_trial_bundle.py MANIFEST ARTIFACT MANIFEST_ANCHOR
```

`trusted-release.json` is the reviewed authority for the exact tag, commit,
tree, manifest, and artifact admitted by the trial. A checksum alone does not
authorize substituted bytes.

## Developer map

| Task | Start with |
| --- | --- |
| Verify the trusted tag and release inputs | `verify_trial_trust.py`, `verify_bundle.py` |
| Supply the self-contained Query verifier | `verify_trial_bundle.py` |
| Assemble and check a deterministic bundle | `assemble_bundle.py`, `verify_assembled_bundle.py` |
| Derive the wrong-platform and corrupted fixtures | `make_negative_fixture.py` |
| Compare two retained reproductions | `verify_reproduced_artifacts.py` |
| Understand evaluated distribution paths | `distribution-paths.md` |

Run the provider tests from the repository root:

```sh
python3 -I experiments/repeatable-installation/enablement/test_enablement.py
```

For exact arguments, byte-custody rules, fixture layouts, and the integrated
trial procedure, use [RUNBOOK.md](RUNBOOK.md). The experiment-level outcome is
summarized in the [parent README](../README.md) and [RESULTS.md](../RESULTS.md).
