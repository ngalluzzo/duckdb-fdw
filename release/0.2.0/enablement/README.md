# Community candidate provider records

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

Run the deterministic slice with:

```sh
bash scripts/test-community-enablement.sh
```
