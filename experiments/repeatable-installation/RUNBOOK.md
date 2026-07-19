# Repeatable installation experiment runbook

This Query Experience experiment asks one bounded question: can the immutable
`v0.1.0` behavior cross DuckDB's installation boundary without hiding the
artifact, compatibility, or trust assumptions from the user?

It is decision evidence for RFC 0004. It is not a published installation
channel, compatibility expansion, signature policy, or support commitment.
The unsigned setting used by the successful path is appropriate only for this
controlled trial and source development.

## Responsibilities

- `install_oracle.py` preserves the public CLI and composes the admitted inputs,
  clean trial root, scenario matrix, and retained evidence. It contains no
  provider, subprocess, scenario, or redaction implementation.
- `trial_inputs.py` preserves the admitted-path and `VerifiedBundle` interface
  while composing the three independently changing input boundaries below.
- `trial_snapshot.py` freezes every byte-bearing provider input, the
  self-contained verifier, and the Query host harness into private trial
  storage before verification; only those read-only copies reach later
  scenarios.
- `verifier_process.py` owns the bounded, isolated provider verifier exchange
  and bounded path-normalized diagnostics.
- `verified_manifest.py` owns release artifact, embedded behavior, DuckDB
  compatibility, and platform identity validation.
- `negative_fixture_admission.py` owns canonical inventory validation and
  independent reproduction of both recorded mutations.
- `host_process.py` owns minimal environment construction, hard output and time
  bounds, unconditional process-group teardown, and the counted JSON
  subprocess exchange with the DuckDB host edge.
- `installation_scenarios.py` owns supported install/restart/load assertions and
  the signature, DuckDB-version, platform, and corrupted-input refusals.
- `evidence_output.py` owns the versioned retained-evidence shape and recursive
  machine-local path normalization after all real-path assertions finish.
- `query_host.py` performs exactly one DuckDB action per process and emits a
  JSON observation. It is an internal edge used only by the oracle.
- `test_query_oracle.py` is the stable focused-test entry point. It composes
  responsibility-matched `test_*.py` modules; `query_oracle_test_support.py`
  supplies only the shared deterministic miniature bundle.
- Engineering Enablement supplies the two pinned Python hosts, release bundle,
  verifier, negative-fixture inventory, and intentionally invalid artifacts.
- The verified manifest's embedded `public_contract` remains the exact
  query-behavior oracle; Query does not read a mutable worktree copy.

Dependencies point from the thin orchestrator to these Query-owned modules.
`trial_inputs.py` composes verifier, manifest, and fixture admission without
moving their rules into the CLI. `trial_snapshot.py` closes the
verification-to-use race and removes the host processes' dependency on the
repository checkout. Scenarios consume the `HostRunner` process boundary and
verified input records; only `HostRunner` invokes the frozen `query_host.py`.
Evidence formatting receives completed observations and cannot authorize
installation. The Query code consumes provider paths and the verifier command;
it does not import build, release-gate, fixture-generation, or CI-custody
internals.

## Run

Invoke the coordinator with explicit provider outputs:

```sh
python3 -I experiments/repeatable-installation/install_oracle.py \
  --supported-python /absolute/path/to/duckdb-1.5.4/python3 \
  --mismatch-python /absolute/path/to/duckdb-1.5.3/python3 \
  --artifact /absolute/path/to/duckdb_api.duckdb_extension \
  --manifest /absolute/path/to/manifest.json \
  --manifest-anchor /absolute/path/to/manifest.sha256 \
  --verifier /absolute/path/to/verify_trial_bundle.py \
  --negative-fixture-inventory /absolute/path/to/negative-fixtures.json \
  --wrong-platform-artifact /absolute/path/to/wrong-platform.duckdb_extension \
  --corrupted-artifact /absolute/path/to/corrupted/duckdb_api.duckdb_extension
```

The verifier interface is intentionally narrow:

```text
PYTHON VERIFIER MANIFEST ARTIFACT MANIFEST_ANCHOR
```

It exits zero only for the original bundle. The coordinator first copies every
data input, the self-contained verifier, and `query_host.py` into private
read-only trial storage. Input
admission verifies only that snapshot, validates the canonical negative-fixture
inventory, and reproduces the exact footer-field and body-byte transformations
from the snapshotted original bytes. The corrupted-input scenario runs the
verifier again against the snapshotted corruption and requires refusal before
the frozen host harness is invoked. Verifier processes use isolated Python, the
same minimal environment policy as hosts, a controlled working directory, a
fixed timeout, a hard output ceiling, and bounded normalized diagnostics.

The command prints one versioned JSON evidence object on success. Retained
observations recursively normalize the clean host, input bundle, and repository
roots as `<trial-root>`, `<input-root>`, and `<repository-root>` after all real
path and byte assertions finish. Save that output outside the repository's
generated-file exclusions, then transcribe the stable identities and outcomes
into `RESULTS.md`; do not commit temporary databases, extension directories,
generated artifacts, or machine-local paths.

Run the focused deterministic Query helper tests without provider artifacts:

```sh
python3 -I experiments/repeatable-installation/test_query_oracle.py
```

## What the oracle proves

The supported path:

1. verifies the manifest, digest anchor, artifact identity, embedded public
   contract, and canonical negative-fixture inventory;
2. starts from a new database and extension directory;
3. installs the local artifact with unsigned development loading explicitly
   enabled;
4. checks the custom-path source and installed file digest, then repeats the
   install in a new process;
5. starts another process, loads `duckdb_api` by name, and compares the observed
   behavior with the verified manifest's embedded contract.

Independent clean states then require:

- default signature policy to reject the unsigned artifact;
- DuckDB 1.5.3 to reject the DuckDB 1.5.4 artifact;
- the accepted host to reject the synthetic `linux_amd64` artifact on the
  `osx_arm64` cell; and
- bundle verification to reject corrupted bytes without starting DuckDB.

Every host observation must match the complete manifest-derived DuckDB identity:
version plus ten-character source commit. Every rejection must leave no
`duckdb_api` function registered and no installed extension artifact. Assertions
use required diagnostic facts rather than entire upstream strings so harmless
wording changes do not erase the compatibility signal.

Each DuckDB action receives only clean HOME, temporary, cache, and configuration
roots plus a fixed PATH and locale. Parent credentials, DuckDB flags, Python
settings, and test variables are not inherited. Fixed time and output bounds
kill the isolated process group and reap the direct child before reporting an
actionable lifecycle failure. Process-group KILL is unconditional after timeout
grace, so an ignored TERM or closed descendant pipe cannot escape cleanup.
The self-contained verifier's digest is required to remain unchanged before and
after each exchange and across the original and corrupted-artifact checks.
This is a lifecycle guard for the verified trial code, not an OS sandbox:
deliberately hostile code can create a new session and escape a POSIX process
group. Ordinary-user distribution must rely on the accepted trust path, not
this harness for malicious-code containment.

## Trust boundary and limitations

A checksum and manifest bind the observed bytes to release evidence; they do
not authenticate an artifact obtained from an untrusted source. DuckDB's
default signature refusal remains the ordinary trust boundary demonstrated by
this trial. The project license is MIT. A public distribution, signing path,
compatibility policy, update policy, and support boundary are now accepted in
RFC 0004. Community build/sign/publish/install, exact matrix, dependency audit,
hosted custody, and user guidance remain `0.2.0` delivery gates.

The wrong-platform file is test evidence only. Its canonical inventory binds its
source and output identities, and Query independently proves that only the
recorded zero-padded platform footer field changed. The corrupted fixture keeps
the release filename so provider verification reaches the byte-identity check;
it is likewise reproduced as exactly one recorded XOR body-byte mutation and
remains paired with the original manifest precisely so verification must fail.
