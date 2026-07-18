# Community candidate provider records

This directory holds the tracked, non-secret inputs for the first Engineering
Enablement slice of the `0.2.0` Community installation goal. It does not contain
a release candidate, signed artifact, support matrix, or publication approval.

`pins.json` records one candidate cycle, including exact ref types and the
SHA-256 of the canonical dependency expectations. The extension-ci-tools name
`v1.5-variegata` is a mutable branch label, so provider evidence trusts only its
resolved commit and tree. `descriptor.json` is deliberately
`pending_non_authoritative`: it has no source ref, source commit, maintainers,
publication state, or support claim. `dependencies.json` describes the inputs
that the bounded dependency command can currently admit; it explicitly defers
per-platform delivered dependency closure until Community artifacts exist.

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
```

The upstreams root contains Git repositories at `duckdb/`,
`community-extensions/`, `extension-template/`, and `extension-ci-tools/`.
Commands read exact commit objects and never fetch, checkout, or infer from
`HEAD`; Git replacement objects and lazy object fetching are disabled for every
plumbing call. Each JSON or anchor input is opened and read once through a
bounded regular-file descriptor, and its digest and parsed meaning come from
those same bytes. Both output roots must be new. Candidate admission also
requires the supplied source commit to contain only the single top-level
`duckdb_extension_load(duckdb_api ...)` command, declare extension version
`0.2.0` in that command, and contain the exact pinned root MIT license.

`candidate.sha256` is exactly one lowercase SHA-256, two spaces,
`candidate.json`, and one newline. Candidate JSON is canonical UTF-8 with
sorted keys, two-space indentation, and one trailing newline. Its project tag
and descriptor remain pending. It contains no artifact, custody, platform, or
support field.

Run the deterministic slice with:

```sh
bash scripts/test-community-enablement.sh
```
