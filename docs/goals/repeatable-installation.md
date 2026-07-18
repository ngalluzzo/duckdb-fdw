# Goal: Repeatable installation

## PM brief

### Outcome

For a new DuckDB user, enable obtaining or building a reproducible extension
artifact, installing it in a clean environment, and loading it by name so that
the first trustworthy query does not depend on repository-internal build
knowledge.

### Why now

`0.1.0` proved the query behavior from source. The next product uncertainty is
whether that behavior can cross a real installation and trust boundary without
overstating compatibility, provenance, or production readiness.

### Product guardrails

- Must: preserve the exact `0.1.0` SQL, rows, diagnostics, lifecycle behavior,
  extension identity, and supported DuckDB compatibility cell.
- Must: bind every tested artifact to its source commit, DuckDB target,
  platform, extension version, checksum, and provenance evidence.
- Must: make incompatible hosts, platforms, corrupted artifacts, and default
  signature enforcement fail before extension code can run.
- Must not: publish or promise a supported binary channel before its licensing,
  signing, compatibility, update, and support boundaries are approved.
- Must not: present process-wide unsigned-extension loading as an ordinary
  production trust posture.

### Success signals

- In an empty host state, a user installs the exact artifact, restarts DuckDB,
  loads `duckdb_api` by name, identifies version `0.1.0`, and executes the
  established three-row query.
- A repeated install is deterministic, and the installed bytes remain bound to
  the verified input artifact.
- Wrong DuckDB version, wrong platform, corrupted bytes, and the default signed
  extension policy each produce a deterministic and actionable refusal.
- The product manager receives a decision-ready comparison of source-built,
  downloadable unsigned, and DuckDB Community Extension distribution paths.

### Product decisions

- Selected on 2026-07-17: MIT, recorded in the root `LICENSE`.
- Approve the ordinary-user distribution and trust model, the compatibility
  promise, and the support and update boundaries before full `0.2.0` delivery.

## Agent commitment

### Observable interpretation

This activated increment is a bounded installation-and-trust feasibility
trial, not the full `0.2.0` release. It starts with the immutable `v0.1.0`
artifact and an empty DuckDB extension directory. The trial verifies the
artifact bundle before installation, installs through DuckDB's extension
mechanism, closes and reopens the host, loads `duckdb_api` by name, observes
the extension version and install metadata, and runs the unchanged accepted
query.

The same executable narrative rejects a DuckDB 1.5.3 host, a synthetic
wrong-platform artifact, a body-corrupted artifact, and installation without
the explicit unsigned-development setting. The trial compares this controlled
unsigned path with source build and DuckDB Community Extensions, then records
a recommendation in RFC 0004. It does not publish an artifact, choose a
license, accept the RFC, or expand the supported cell.

### Acceptance evidence

- Demonstration: clean-state install, restart, load by name, version and
  installation identity, repeated install, and the exact `0.1.0` query result.
- Automated oracle: manifest and checksum verification followed by supported,
  mismatched-host, wrong-platform, corrupted-artifact, signature-policy, and
  repeated-install probes that leave no extension function registered after a
  rejected input.
- Quality gates: repository asset validation, source-identity verification,
  focused trial checks, applicable fresh product evidence, and Git whitespace
  checks.
- Independent review: supply-chain and trust-boundary review plus clean-host,
  compatibility, diagnostic, lifecycle, and test-oracle review.

### Contract and invariant impact

- The trial may add experimental installation evidence and repair evidence
  retention. It does not change the public SQL, connector, runtime, or native
  execution contracts.
- RFC 0004 will propose distribution, trust, licensing, compatibility, update,
  and support boundaries. Those become product contracts only after required
  team review and product approval.
- Existing invariants remain: exact DuckDB and source identity, immutable
  release artifacts, offline bind and planning, conservative relational
  behavior, strict conversion, bounded execution, cancellation, deterministic
  teardown, redaction, and native-boundary containment.

### Team and RFC routing

- Accountable stream: Query Experience.
- Engineering Enablement — Facilitation: provide the reproducible artifact
  bundle, clean-host harness, provenance checks, evidence retention, and
  candidate distribution research. Exit when Query Experience owns and can
  independently execute the documented installation oracle through a stable
  evidence interface.
- RFC: RFC 0004 is required before full delivery because ordinary-user
  distribution changes compatibility, trust, licensing, update, and support
  commitments. This bounded trial is permitted first to resolve the evidence
  needed for that decision.

### Unknowns and first trial

- Unknown: whether a self-built artifact can support a safe ordinary-user
  install without DuckDB community signing; whether existing release evidence
  survives the CI custody path; and which failure modes DuckDB itself rejects
  before extension initialization.
- Trial: compare source-built, downloadable unsigned, and DuckDB Community
  Extension paths using official policy and executable clean-state probes.

### Delivery path

1. Establish a verified immutable input bundle and a clean-host installation
   oracle with deterministic negative fixtures.
2. Run the oracle against the accepted host, version mismatch, platform
   mismatch, corrupted input, default signature policy, and repeated install.
3. Record the evidence and a bounded recommendation in Draft RFC 0004,
   including the explicit product decisions and external coordination needed
   before full `0.2.0` activation.

## Completion record

### Delivered

- Added a Query-owned clean-host installation oracle for verified install,
  repeat install, restart, load by name, full host identity, and the unchanged
  `0.1.0` query contract.
- Added provider-owned immutable trust, deterministic negative fixtures,
  two-workspace artifact reproducibility, portable bundle assembly, and exact
  CI evidence-custody checks.
- Completed the bounded comparison and recorded DuckDB Community Extensions as
  the recommended ordinary-user target in Draft RFC 0004. No distribution
  channel, license, compatibility expansion, or support promise was activated.

### Evidence

- `experiments/repeatable-installation/RESULTS.md` records the passing matrix,
  immutable identities, deterministic refusals, and trial limitations.
- Two independent retained reproductions produced byte-identical trusted
  artifacts with SHA-256 `4f1a0678fd2a673b433af6248a34966cb8fd41d107d4c0b3b97ca71eb35179ea`.
- Focused Query and Enablement tests, the complete clean-host trial, repository
  asset and source-identity checks, and a fresh native product gate passed.
- The local CI custody guard proves exact staging, workflow structure, inner
  evidence, and staged-versus-downloaded byte comparison. The hosted Actions
  round trip remains external follow-up evidence.

### Tradeoffs and follow-ups

- The successful downloadable path deliberately weakens signature policy for
  one isolated trial host; it is not ordinary production guidance.
- Community build, signing, and installation remain unproved until the product
  manager authorizes external submission. The project-license prerequisite is
  now resolved as MIT.
- Full `0.2.0` delivery remains gated on RFC 0004 acceptance and explicit
  product decisions for compatibility, update, rollback, history, backports,
  and support.
