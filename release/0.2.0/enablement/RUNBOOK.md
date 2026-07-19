# Community enablement evidence runbook

This directory holds the tracked, non-secret inputs for the first Engineering
Enablement slice of the `0.2.0` Community installation goal. It does not contain
a release candidate, signed artifact, support matrix, or publication approval.

`pins.json` records one candidate cycle, including exact ref types and the
SHA-256 of the canonical dependency expectations. The extension-ci-tools name
`v1.5-variegata` is a mutable branch label, so provider evidence trusts only its
resolved commit and tree. `descriptor.json` is deliberately
`pending_non_authoritative`: it has no source ref, source commit, maintainers,
publication state, or support claim. That expectation remains the exact input
bound into `candidate.json`. `description.yml` is the separate, tracked
Community proposal for the now-published immutable candidate commit; it names
one approved maintainer and makes no exclusion, platform, or support claim.
`descriptor-cycle.json` is the reviewed authority for that handoff: it binds
the published source commit/tree and exact candidate, dependency, expectation,
pin, and proposal digests. Candidate and dependency `.sha256` files prove byte
custody only and cannot authorize a substituted, jointly re-anchored cycle.
`dependencies.json` describes the inputs that the bounded dependency command
can currently admit; it explicitly defers per-platform delivered dependency
closure until Community artifacts exist.

The offline provider sequence is:

```sh
python3 -I -B scripts/community/audit_dependencies.py \
  --pins "$PWD/release/0.2.0/enablement/pins.json" \
  --expectations "$PWD/release/0.2.0/enablement/dependencies.json" \
  --repository /absolute/candidate/repository \
  --source-commit <exact-40-hex-candidate-commit> \
  --upstreams-root /absolute/upstreams \
  --output-root /absolute/new/dependency-output

python3 -I -B scripts/community/verify_candidate.py \
  --repository /absolute/candidate/repository \
  --source-commit <the-same-exact-candidate-commit> \
  --pins "$PWD/release/0.2.0/enablement/pins.json" \
  --descriptor-expectation "$PWD/release/0.2.0/enablement/descriptor.json" \
  --dependency-audit /absolute/new/dependency-output/dependency-audit.json \
  --dependency-anchor /absolute/new/dependency-output/dependency-audit.sha256 \
  --output-root /absolute/new/candidate-output

python3 -I -B scripts/community/verify_descriptor.py \
  --pins "$PWD/release/0.2.0/enablement/pins.json" \
  --descriptor-expectation "$PWD/release/0.2.0/enablement/descriptor.json" \
  --descriptor-cycle "$PWD/release/0.2.0/enablement/descriptor-cycle.json" \
  --proposal "$PWD/release/0.2.0/enablement/description.yml" \
  --candidate /absolute/new/candidate-output/candidate.json \
  --candidate-anchor /absolute/new/candidate-output/candidate.sha256 \
  --dependency-audit /absolute/new/dependency-output/dependency-audit.json \
  --dependency-anchor /absolute/new/dependency-output/dependency-audit.sha256 \
  --output-root /absolute/new/descriptor-output
```

The upstreams root contains Git repositories at `duckdb/`,
`community-extensions/`, `extension-template/`, and `extension-ci-tools/`.
Commands read exact commit objects and never fetch, checkout, or infer from
`HEAD`; Git replacement objects and lazy object fetching are disabled for every
plumbing call. Each JSON or anchor input is opened and read once through a
bounded regular-file descriptor, and its digest and parsed meaning come from
those same bytes. All three output roots must be new. Candidate admission also
requires the supplied source commit to contain only the single top-level
`duckdb_extension_load(duckdb_api ...)` command, declare extension version
`0.2.0` in that command, and contain the exact pinned root MIT license. That
same immutable commit tree must contain exactly the pinned `duckdb` and
`extension-ci-tools` gitlinks plus a regular `.gitmodules` blob with exactly
their official repositories, local paths, and expected branches. Neither the
candidate repository's `HEAD`, index, worktree, nor ambient Git configuration
participates in those checks. Descriptor admission accepts only the tracked
canonical scalar/list subset of Community YAML. It rejects duplicate keys,
aliases, tags, merge keys, mutable refs, optional exclusion fields, and any
semantic drift from the pinned source, dependency audit, project metadata, or
approved maintainer.

`candidate.sha256` is exactly one lowercase SHA-256, two spaces,
`candidate.json`, and one newline. Candidate JSON is canonical UTF-8 with
sorted keys, two-space indentation, and one trailing newline. Its project tag
and descriptor remain pending. It contains no artifact, custody, platform, or
support field.

`descriptor-admission.json` and `descriptor-admission.sha256` bind the exact
proposal bytes to the anchored candidate, dependency audit, descriptor
expectation, reviewed cycle, and pins. The record grants only local provider
admission of a proposal. It does not claim submission, upstream CI, signing,
deployment, artifact production, platform compatibility, publication, or
support.

## Offline Community build normalization

`build-authorities.json` is the independent trust root for build collection.
Its canonical digest is pinned in `build_evidence_authority.py`, and its
`approved` list is intentionally empty. PR 2256 or any later run therefore
cannot produce admitted build evidence until maintainers review one exact set
of descriptor and export bytes, add its immutable identity row, and update the
pinned registry digest. A caller cannot grant itself that authority by
supplying a consistent replacement registry or by changing co-located export
fields.

The collector is offline and does not invoke `gh`, read GitHub authentication,
or inspect ambient environment state. Before invoking it, a maintainer exports
five minimal canonical JSON files from the intended
`duckdb/community-extensions` PR run and downloads the API artifact archives
and complete job logs into explicit roots named `artifacts/` and `logs/`.
Export preparation is a separate external operation; copied web URLs, branch
display state, or a run number alone are never authority.

The canonical export contracts are:

- `pull-request.json`: repository, PR number and state, plus exact head
  repository/ref/commit and base ref/commit;
- `run.json`: repository, workflow id/path, run id/attempt, event, completion
  state, PR linkage, source/base refs, base commit, and exact run head commit.
  GitHub may execute a pull-request merge commit, so the reviewed run head is
  recorded separately from the descriptor-bound PR source commit;
- `matrix.json`: every reviewed matrix combination with either
  `job_expected` or `excluded_unclaimed`. Excluded combinations create no
  GitHub job, runner, log, or artifact, so they remain explicit only in this
  authority-bound inventory and never become support exclusions;
- `jobs.json`: `total_count` and every completed job, including failed,
  cancelled, skipped, and other non-success conclusions. Each job preserves
  its raw matrix scalars, runner name/group/labels, exact DuckDB identity when
  it produces an artifact, declared artifact names, and one log filename,
  byte size, and SHA-256;
- `artifacts.json`: `total_count` and every declared output, binding its API
  id/name, producing job, archive filename, byte size, SHA-256, expiry state,
  run id, and attempt.

All five files use sorted-key, two-space canonical JSON with one trailing
newline and only their versioned fields. `total_count` must equal the supplied
inventory. Every `job_expected` matrix combination must create exactly one
matrix job, while `excluded_unclaimed` combinations must create none.
Non-matrix workflow jobs remain visible with an empty raw matrix. Artifact-
producing jobs may not collide on raw matrix labels, and the job declarations
and artifact export must describe exactly the same output names. Artifact and
log roots contain exactly the named regular files—no directories, symlinks,
missing files, or extras. Each root is opened once as a non-following directory
descriptor; enumeration and child reads remain relative to that stable
descriptor, and any concurrent root replacement, entry replacement, or
inventory change stops collection.

Once a real authority row is approved, run:

```sh
python3 -I -B scripts/community/collect_build_evidence.py \
  --authority-registry "$PWD/release/0.2.0/enablement/build-authorities.json" \
  --pins "$PWD/release/0.2.0/enablement/pins.json" \
  --descriptor-admission /absolute/descriptor-output/descriptor-admission.json \
  --descriptor-anchor /absolute/descriptor-output/descriptor-admission.sha256 \
  --pull-request-export /absolute/export/pull-request.json \
  --run-export /absolute/export/run.json \
  --jobs-export /absolute/export/jobs.json \
  --matrix-export /absolute/export/matrix.json \
  --artifacts-export /absolute/export/artifacts.json \
  --artifacts-root /absolute/downloads/artifacts \
  --logs-root /absolute/downloads/logs \
  --output-root /absolute/new/community-build-output
```

The command emits `community-builds.json` plus its anchor and one
`jobs/job-<id>/community-build.json` plus anchor for every job. Records contain
logical download-relative paths, exact byte identities, raw upstream labels,
raw conclusions, and the separately reviewed excluded/unclaimed combinations.
The Community descriptor PR head repository/commit and workflow execution head
are distinct from the `ngalluzzo/duckdb-fdw` extension source repository and
commit; the registry and output preserve all three rather than conflating
them. They grant only local Community build-evidence authority.
A success conclusion is a candidate build row, not evidence of Community
signing, deployment, stock-host behavior, platform compatibility, or public
support. Those meanings remain with later custody and Query gates.

## Native build bytes are not deployed bytes

The official native deployment path does not preserve the complete
build-artifact digest. The Actions ZIP contains one
`duckdb_api.duckdb_extension` whose final
256 bytes are an all-zero signature placeholder. DuckDB's deployment script
signs the bytes before that block, replaces the placeholder with the 256-byte
Community signature, gzip-compresses the complete result, and serves those
different bytes from the Community endpoint.

Deployment admission must retain five distinct custody identities: the Actions
ZIP, unsigned extension, shared pre-signature payload, served gzip, and signed
extension. It must prove exact extension-byte equality before the signature
block and bind the exact successful main-branch build/deploy run plus freshly
downloaded endpoint bytes. A nonzero signature block is only transition
evidence; it is not local cryptographic validation. Only Query's stock-DuckDB
lifecycle with default extension policy proves that DuckDB accepts the deployed
signature. Query admits the exact anchored deployment record and cannot place
the unsigned digest in the support matrix.

This contract is native-only. The official `wasm_mvp` row produces a `.wasm`
artifact and uses Brotli during deployment. Preserve that provider row as
explicitly unclaimed; do not interpret it with the ZIP/gzip native helper or
include it in the stock-native support matrix.

Run the deterministic slice with:

```sh
bash scripts/test-community-enablement.sh
```
