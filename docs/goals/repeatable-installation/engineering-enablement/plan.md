# Engineering Enablement plan: installation custody and reproducibility

## Objective, topology, and authority

Facilitate the bounded repeatable-installation trial by giving Query Experience
a deterministic artifact-custody service, an isolated clean-host process
harness, retained evidence, and primary-source distribution research. Query
Experience remains accountable for the DuckDB user's installation, restart,
name-based load, identity, diagnostic, and first-query narrative.

This interaction is **Facilitation**, not joint product accountability and not
a permanent Enablement approval gate. The lead agent retains technical
decision authority. The product manager retains licensing, ordinary-user
trust, compatibility, support, update, and public-distribution decisions.

The bounded trial may repair evidence retention and produce decision evidence.
It may not publish an artifact, change the PM-selected MIT license, accept RFC
0004, expand the
supported cell, or describe process-wide unsigned loading as a production
posture.

## Disjoint file ownership

Engineering Enablement owns only the custody, isolation, workflow-retention,
and research surfaces below for this goal:

| Owned file or subtree | Responsibility |
| --- | --- |
| `docs/goals/repeatable-installation/engineering-enablement/plan.md` | This facilitation boundary and handoff contract |
| `.github/workflows/linux-amd64-sanitized.yml` | Repair and prove retention of the existing sanitizer evidence |
| `scripts/stage-ci-evidence.py` | Copy an exact allowlist from ignored build state into a visible, new CI staging root and emit a canonical custody inventory |
| `scripts/verify-ci-evidence-roundtrip.py` | Verify staged and downloaded inventories, inner manifests, anchors, and byte digests |
| `scripts/test-ci-evidence-retention.sh` | Forward guard for hidden-path omission, extra-file rejection, and upload/download verification wiring |
| `scripts/run-installability-trial.sh` | Resolve explicit overridable local evidence and host paths, create ignored output, build negative fixtures, and invoke the Query-owned oracle |
| `experiments/repeatable-installation/enablement/` | Trial-only bundle assembly, verification, negative-fixture generation, clean-process orchestration, focused tests, and sourced distribution comparison |

Within the experimental subtree, use responsibility-specific files rather
than a catch-all helper: `trusted-release.json`, `verify_trial_trust.py`,
`verify_bundle.py`, `verify_trial_bundle.py`, `assemble_bundle.py`,
`verify_assembled_bundle.py`,
`make_negative_fixture.py`, `verify_reproduced_artifacts.py`,
the focused test modules mapped below, `distribution-paths.md`, and a short
`README.md` that documents their command contracts. The tracked trust record
is the reviewed trial authority for the exact tag object, peeled commit/tree,
release manifest, and artifact; the caller-supplied manifest and anchor cannot
authorize new bytes. The owned shell runner invokes Query's clean-host oracle
across the process boundary; no Enablement module duplicates that Query
responsibility. Generated bundles and evidence live only under an explicitly
new ignored `.build/repeatable-installation/` root.

The Query-facing `verify_trial_bundle.py` is one self-contained, content-
identified command for the exact trusted manifest and artifact. Its constants
must match `trusted-release.json` under provider tests. Query snapshots those
bytes once and uses the same identity for both verifier calls; the richer
`verify_bundle.py` remains provider-owned and may inspect tagged repository
evidence during assembly and reproduction.

| Enablement test file | One reason to change |
| --- | --- |
| `enablement_test_support.py` | Shared tagged-input construction, subprocess invocation, and retained-evidence lookup used by more than one provider oracle |
| `test_trust_admission.py` | Trusted release identity and manifest-anchor admission |
| `test_negative_fixtures.py` | Deterministic wrong-platform and corrupted-fixture construction |
| `test_bundle_custody.py` | Bundle repeatability, inventory, anchor, and tamper custody |
| `test_reproduction.py` | Independent reproduction-root validation and byte comparison |
| `test_path_boundaries.py` | Symlink-leaf and custody-output path escape rejection |
| `test_enablement.py` | Stable aggregate entry that discovers only the five named oracle modules above |

Query Experience owns its separate plan and exactly the four root experiment
files named there: `experiments/repeatable-installation/README.md`,
`query_host.py`, `install_oracle.py`, and `RESULTS.md`. Those files own the
user-facing demonstration and guidance, the installed-extension process
oracle, and all assertions about SQL rows, DuckDB catalog/install metadata,
diagnostics, restart, name-based load, and absence of registered extension
functions after rejection. Enablement neither creates a `query-experience`
subdirectory nor imports or reinterprets the Query-owned files.

The lead agent owns `docs/goals/repeatable-installation.md`, RFC 0004,
cross-package integration, and Git history. Existing release builders,
manifests, pins, and verifiers remain read-only dependencies of this package.
In particular, this plan does not authorize edits to
`scripts/run-0.1-release-gate.sh`, `scripts/reproduce-0.1-release.sh`,
`scripts/verify-release-manifest.py`,
`scripts/verify-sanitizer-manifest.py`, or release `0.1.0` inputs.

## Provider interface to Query Experience

The provider boundary is process-based and provisional to the trial:

1. Enablement accepts an existing product evidence root produced from the
   immutable `v0.1.0` tag, verifies its exact tag object, peeled commit/tree,
   release manifest, anchor syntax/target, and artifact against the reviewed
   tracked trust record, and assembles a trial bundle without rebuilding or
   changing the artifact.
2. The bundle contains only `duckdb_api.duckdb_extension`, the existing release
   manifest and anchor, and a canonical bundle inventory plus its SHA-256
   anchor. The inventory records relative names, sizes, hashes, source commit
   and tree, DuckDB target, platform, extension version, and the input-manifest
   digest. Absolute workstation paths, credentials, timestamps, and ambient
   environment values are forbidden.
3. Enablement invokes Query's `install_oracle.py` in a fresh process through
   its fixed CLI: `--supported-python`, `--mismatch-python`, `--artifact`,
   `--manifest`, `--manifest-anchor`, `--verifier`,
   `--wrong-platform-artifact`, `--corrupted-artifact`, and
   `--negative-fixture-inventory`. The inventory is the canonical provider
   record that binds both derived fixture identities and exact mutations. The
   verifier contract is exactly
   `PYTHON VERIFIER MANIFEST ARTIFACT MANIFEST_ANCHOR`. The Query oracle creates
   its own clean host roots; the Enablement runner supplies explicit verified
   paths and does not construct SQL or decide expected query behavior.
4. The Query oracle owns install, close, reopen, load-by-name, identity and
   install-metadata inspection, the unchanged `0.1.0` query, refusal
   diagnostics, and the no-registration proof. It writes canonical JSON under
   a versioned trial schema and exits nonzero on an oracle failure.
5. Enablement validates only the evidence envelope: scenario identity, input
   bundle digest, host identity, process outcome, relative retained-file
   inventory, and the presence and anchor of the Query-owned result. It treats
   the Query result as an opaque consumer record.

The harness must not import the DuckDB Python package in its own process. Each
scenario gets a new process and new host state so a failed load cannot leak a
registered function or configuration into a later probe. Once prepared hosts
and the verified bundle exist, execution is offline.

## Artifact custody and reproducibility workstreams

### Release-artifact admission and bundle custody

- Admit only the exact artifact, manifest, and manifest anchor bound by the
  tracked trial trust record. Verify the local annotated tag object and its
  peeled commit/tree before reading tagged blobs; a mutable local tag ref or a
  newly self-anchored manifest is not authority. Run the trusted verifier
  before the first copy and the exact bundle-root verifier after assembly and
  immediately before Query execution.
- Assemble from an exact allowlist into a new root; reject symlinks, duplicate
  names, unexpected files, writable replacement of the selected artifact, and
  an existing output root.
- Canonicalize only the new JSON inventory. Never rewrite the extension or
  release manifest. After admitting the original release anchor exactly,
  normalize the bundle copy to
  `<trusted-manifest-sha256>  manifest.json\n` so bundle bytes are deterministic
  and contain no workstation path; the original evidence remains unchanged.
- Assemble the same inputs into two empty roots and require identical file
  inventories, bundle-manifest bytes, and anchors. If an archive is useful for
  transport, normalize entry order, ownership, permissions, and timestamps and
  prove two archives byte-identical; the archive format remains trial-only.
- Verify after every custody transition: admission, bundle assembly,
  extraction, optional CI transfer, and immediately before DuckDB invocation.
  A verification failure prevents the Query oracle from running.

### Build reproducibility

Run the existing two-clean-workspace `0.1.0` reproduction path and pass its two
explicit, overridable evidence roots to `verify_reproduced_artifacts.py`. That
oracle independently verifies both anchored manifests against the tracked
release source, records whether each artifact equals the selected trusted
artifact, then compares the two extension files byte-for-byte and by SHA-256.
The two canonical roots must be distinct. Keep semantic-manifest
reproducibility and byte-for-byte artifact reproducibility as separate results.
If valid artifact bytes differ, emit a structured negative reproducibility
result and continue the installation trial with the immutable selected
artifact; do not mask the difference with normalized metadata or claim a
reproducible build.

### Clean-host and negative-fixture isolation

- The supported scenario verifies the bundle, installs with the explicitly
  scoped unsigned-development startup setting, closes the host, starts a new
  host, loads `duckdb_api` by name, and delegates the accepted query assertions
  to Query Experience.
- The repeated-install scenario uses the same verified input twice without
  force replacement and proves the installed artifact digest remains equal to
  the admitted input before and after restart.
- The corrupted-input fixture is derived by a deterministic fixed mutation
  and must fail bundle verification before DuckDB or the Query oracle runs.
- Wrong-host and wrong-platform fixtures retain valid, recorded construction
  recipes and fixed digests. The canonical `negative-fixtures.json` travels
  with both fixture paths so Query can reject counterfeit same-size inputs
  before DuckDB. DuckDB must reject admitted fixtures before extension
  initialization; the Query oracle proves that no extension function is
  registered afterward.
- The signature-policy scenario uses the unmodified artifact but omits the
  unsigned-development startup setting. DuckDB owns the refusal; the harness
  records the exact host identity and process outcome without weakening the
  default.

### CI evidence retention and the hidden `.build` omission

The sanitizer workflow currently passes `.build/linux-amd64-sanitized` and a
`.build` log directly to `actions/upload-artifact` at commit
`ea165f8d65b6e75b540449e92b4886f43607fa02`. That action's
`include-hidden-files` input defaults to `false`; the workflow therefore cannot
claim that its hidden evidence survived upload, and `if-no-files-found: error`
is not a downloaded-content oracle.

Repair this as a custody boundary rather than broadly enabling all hidden
files:

1. Verify the sanitizer evidence with the existing manifest and envelope
   verifiers.
2. Copy only the required artifact, manifests, anchors, compile commands,
   flags report, envelope, and log into a newly created, non-hidden staging
   root under `${{ runner.temp }}`.
3. Emit and verify a canonical custody inventory; fail on missing, extra,
   hidden, symlinked, or changed inputs.
4. Upload only that visible staging root using the pinned action.
5. Download the artifact into a second new root in the same workflow job,
   re-run the inventory and inner-evidence verifiers, and compare every
   downloaded file byte-for-byte with the still-present staged root.

The custody verifier validates the sanitizer manifest anchor as one exact
lowercase SHA-256 record with two spaces, a target whose basename is
`manifest.json`, and one newline before delegating to the release verifier. It
derives the expected sanitizer source from the tracked trial trust record and
pinned commit, not from a self-authorizing mutable local tag.

The focused workflow guard parses the YAML structure and exact executable
step arguments. It must fail if a future edit uploads `.build` directly,
comments out or conditionally disables custody work, removes the allowlisted
staging step, removes the staged/downloaded comparison, weakens
`if-no-files-found`, or changes an action from its full commit pin.

### Distribution-path evidence

Record a dated, primary-source comparison of source-built direct loading,
downloadable unsigned artifacts/custom repositories, DuckDB Community
Extensions, and static custom-host distribution. For each path, state who
builds, signs, distributes, and supports it; exact DuckDB/platform coupling;
historical-version behavior; licensing and upstream coordination; and the
security consequence. This workstream supplies evidence to the Query-sponsored
RFC 0004 and makes no recommendation on a reserved product choice.

## Deterministic evidence matrix

| Property | Oracle | Required result |
| --- | --- | --- |
| Admission | Existing release-manifest verifier plus bundle verifier | Source/tag/tree, DuckDB 1.5.4, `osx_arm64`, version `0.1.0`, size, and artifact digest agree before any install process starts |
| Bundle repeatability | Two empty output roots from the same admitted evidence | Identical allowlisted files, canonical inventory bytes, and anchors |
| Build reproducibility | Two cache-empty tagged workspaces | Exact artifact digest comparison reported separately from semantic-manifest comparison |
| Clean installation | Query oracle in a new isolated host state | Install, close, reopen, load by name, accepted identity and rows |
| Repeated installation | Same bundle and a controlled extension directory | Installed bytes remain equal to the admitted artifact; no replacement or drift |
| Corrupted input | Fixed byte mutation | Custody verifier rejects before the DuckDB process and consumer oracle |
| DuckDB 1.5.3 | Exact pinned mismatch host | DuckDB refuses before registration; Query result reports no registered function |
| Wrong platform | Deterministically generated metadata fixture | DuckDB refuses before initialization; fixture recipe and digest are retained |
| Default signature policy | Unmodified unsigned artifact, no opt-in | DuckDB refuses; no fallback enabling unsigned loading occurs |
| CI retention | Visible stage, pinned upload, fresh download | Downloaded inventory and all inner hashes equal staged inputs |
| Isolation | One new process and host root per scenario | No state, imports, function registration, or writable extension bytes cross scenarios |

Focused tests use fixed inputs and exact categories rather than matching broad
substrings or accepting any nonzero exit. The retained evidence records command
arguments in redacted, relative form and hashes stdout/stderr records; it does
not retain credentials, unrestricted environment dumps, or absolute local
paths.

## Dependencies and overlap risks

- The release gate requires a clean repository at `v0.1.0`. Because the active
  goal and plans are newer work, evidence must come from a detached clean
  `v0.1.0` checkout or existing verified evidence, never from a modified trial
  worktree pretending to be the tag.
- The product evidence root, `release/0.1.0/pins.json`, existing release and
  sanitizer verifiers, pinned DuckDB 1.5.4 and mismatch 1.5.3 hosts, and the
  Query-owned process oracle are prerequisites. Missing inputs are hard
  failures, not rebuild or network fallbacks inside the oracle.
- `.github/workflows/linux-amd64-sanitized.yml` is a release-evidence surface.
  The lead agent must serialize that edit with any concurrent workflow change
  and integrate the retention repair before relying on CI custody evidence.
- Query Experience and Enablement must not co-edit an oracle. The process
  argument/result schema is the only integration seam; rows and diagnostics
  stay Query-owned, while roots, byte custody, process isolation, and transfer
  envelopes stay Enablement-owned.
- Generated `.build` state is never Git coordination state and must not be
  shared between concurrent worktrees. Every root is caller-supplied, new, and
  ownership-marked where reuse is explicitly supported.
- RFC 0004 and the goal completion record consume distilled results. They do
  not duplicate harness internals or become alternate manifest authorities.
- A need to edit public SQL, native extension source, connector/runtime
  contracts, release `0.1.0` inputs, or Query-owned files is an integration
  request to the lead, not an expansion of this package.

## Facilitation exit

The interaction exits when all of the following are observable:

- Query Experience can assemble or obtain a verified bundle, run the supported
  and negative scenario matrix from documented commands, and interpret its own
  result record without Enablement assistance or repository-internal build
  knowledge.
- Query Experience owns and can change the installation/query oracle while the
  Enablement harness continues treating it as an opaque process consumer.
- Two bundle assemblies, repeated installation, and the CI upload/download
  round trip pass their byte-level custody checks; the guard test demonstrates
  that the original hidden-`.build` retention pattern fails.
- The evidence delivered to RFC 0004 distinguishes verified facts, failed or
  missing evidence, and product decisions without implying approval.
- Ongoing domain-oracle maintenance belongs to Query Experience. Any retained
  generic custody and CI-retention gate is documented as a low-coordination
  Engineering Enablement service, or the trial-only tooling is retired after
  the RFC decision.

If routine installation work still requires Enablement to inspect Query SQL,
catalog assertions, or user diagnostics, the exit remains open because the
provider boundary is not yet low-coupling.

## RFC boundary

No new RFC is required to run this bounded experiment or to repair the existing
workflow so it retains exactly the evidence it already claims to retain. Both
are reversible evidence/process work and establish no new user commitment.

During the bounded trial, RFC 0004 remained mandatory, Query-sponsored, and
product-manager-gated before any of the following crossed the trial boundary:

- choosing or publishing an ordinary-user download or repository channel;
- changing the MIT project license or submitting to DuckDB Community Extensions;
- accepting a signing authority, custom trust root, or routine unsigned
  posture;
- promising compatibility beyond the existing DuckDB 1.5.4 `osx_arm64` cell;
- defining update, rollback, historical-version, retention, or support policy;
- treating the experimental bundle/archive or provider schema as a public or
  durable cross-team contract; or
- changing SQL, diagnostics, extension lifecycle, initialization, reload, or
  native compatibility behavior.

The trial populated the evidence section and alternatives of RFC 0004 without
approving the proposal. On 2026-07-17, the product manager resolved the policy,
both required teams approved, and RFC 0004 was Accepted. Community delivery and
the facilitation exit proceed under the new full `0.2.0` goal.
