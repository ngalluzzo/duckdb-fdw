# Repeatable installation provider runbook

This directory owns byte custody for the bounded repeatable-installation
experiment. It does not import DuckDB, construct SQL, or interpret the
Query-owned user oracle.

The stable consumer boundary is:

```text
PYTHON verify_trial_bundle.py MANIFEST ARTIFACT MANIFEST_ANCHOR
```

`trusted-release.json` is the reviewed trial authority for the exact annotated
tag object, peeled commit/tree, release manifest, and artifact. Before reading
tagged blobs, `verify_trial_trust.py` proves that the local tag still names
those identities. `verify_bundle.py` then verifies the selected artifact,
manifest, and exact anchor syntax against that record. A caller-created
manifest plus a new checksum file cannot admit different bytes. Verification
does not require the active worktree to equal the release tag.

`verify_trial_bundle.py` is the self-contained Query-facing authority for the
one exact manifest and artifact. Query snapshots that single file and uses the
same verifier digest for original and corrupted checks without retaining a
repository dependency. Provider-owned `verify_bundle.py` remains the richer
semantic and tagged-source verifier used for assembly and reproduction.

`assemble_bundle.py` copies only the release artifact, manifest, and anchor to
a new output root and adds a deterministic inventory.
`verify_assembled_bundle.py` enforces the exact five-file directory, both
anchors, every recorded size and digest, and the trusted release triple.
The bundle copy of `manifest.sha256` is normalized to the trusted digest plus
the relative `manifest.json` name, so two bundles are byte-identical and carry
no workstation path even when the admitted release anchor used an absolute
source path.
`make_negative_fixture.py` derives a zero-padded wrong-platform metadata
fixture and a one-byte body corruption, recording both exact mutations. The
corrupted copy is written as `corrupted/duckdb_api.duckdb_extension` so its
release filename cannot mask the required byte-identity rejection. The commands
reject symlink leaves and symlinked output parents while resolving ordinary
canonical parents such as macOS system paths.

`verify_reproduced_artifacts.py REPRODUCTION_ONE REPRODUCTION_TWO` verifies the
two distinct 0.1 reproduction evidence roots independently, reports whether
their artifacts are byte-identical and whether each equals the tracked trusted
release digest. A valid byte difference is structured negative evidence, not a
reason to skip installation of the separately trusted artifact.
`scripts/run-installability-trial.sh` defaults these inputs to
`.build/reproduction-v0.1.0-f855dfb/evidence-{one,two}`; use
`--reproduction-one` and `--reproduction-two` to supply other retained roots.

Run the focused provider tests with:

```sh
python3 -I experiments/repeatable-installation/enablement/test_enablement.py
```

The aggregate entry contains no provider assertions. Test ownership is split
by the responsibility that changes:

| Test module | Responsibility |
| --- | --- |
| `enablement_test_support.py` | Explicit shared tagged fixtures, retained-evidence paths, and subprocess probes |
| `test_trust_admission.py` | Trusted release and anchor admission |
| `test_negative_fixtures.py` | Negative-fixture mutation and inventory determinism |
| `test_bundle_custody.py` | Bundle assembly, repeatability, inventory, anchors, and tamper rejection |
| `test_reproduction.py` | Two-workspace reproduction identity and byte comparison |
| `test_path_boundaries.py` | Input and output symlink boundaries |
| `test_enablement.py` | Stable aggregate runner for the five focused suites |

Each focused `test_*.py` module is also directly executable with `python3 -I`
when isolating one responsibility.

Run the integrated local trial through `scripts/run-installability-trial.sh`.
Its default inputs are the retained local `v0.1.0` evidence and pinned Python
hosts; every path has an explicit override. Generated outputs remain under the
ignored `.build/repeatable-installation/` directory unless another new output
root is supplied.

The runner passes `negative-fixtures.json` to Query alongside both derived
artifacts through `--negative-fixture-inventory`; the inventory is the fixed
process boundary for validating exact fixture identities and mutation recipes.
