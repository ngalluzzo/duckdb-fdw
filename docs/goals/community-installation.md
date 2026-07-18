# Goal: Community installation

## PM brief

### Outcome

For a new DuckDB user, enable installing `duckdb_api` from DuckDB Community
Extensions with stock latest-stable DuckDB so that the accepted query works
without repository-internal build knowledge or weakened signature policy.

### Why now

`0.1.0` proved one trustworthy source-built query, and the bounded installation
trial selected the Community channel. MIT and the compatibility, lifecycle,
immutability, rollback/history, and support boundaries are now decided, so the
next value is delivering the ordinary installation journey as `0.2.0`.

### Product guardrails

- Must: support only the latest stable DuckDB release at release time and the
  exact Community CI platform cells that pass the complete release oracle.
- Must: preserve default Community signature enforcement, immutable project
  releases, and the accepted `0.1.0` query behavior.
- Must not: imply support for failed, excluded, untested, older, nightly,
  non-Community, or otherwise absent compatibility cells.
- Must not: promise pre-`1.0` backports, Community rollback, historical-version
  availability, an SLA, or that Community signing is a source-code audit.

### Success signals

- From stock latest-stable DuckDB on every claimed platform row, a user can
  install `duckdb_api` from `community`, restart, load it by name, identify
  version `0.2.0`, and execute the unchanged accepted query.
- The release publishes an exact support matrix and actionable guidance for
  installation failures without instructing ordinary users to enable unsigned
  extensions.
- Every project release identity remains immutable, and a correction uses a
  new SemVer rather than replacing a tag, source ref, or release.
- Users can discover the forward-only pre-`1.0` update policy, lack of
  guaranteed Community rollback/history, and best-effort GitHub Issues support
  at the installation guidance.

## Agent commitment

### Observable interpretation

In a clean host state on each claimed Community CI platform, a user starts the
stock latest-stable DuckDB release with its default extension security policy,
runs `INSTALL duckdb_api FROM community`, exits, starts a separate process,
runs `LOAD duckdb_api`, observes extension version `0.2.0`, and receives the
same three typed rows from the accepted SQL. The release matrix names the exact
DuckDB identity and platform row. A row not present in that matrix is
explicitly unsupported; representative incompatible installation behavior is
captured with an actionable diagnostic and no registered function.

### Acceptance evidence

- Demonstration: clean Community install, separate-process load-by-name,
  version and metadata identification, and the unchanged accepted query on
  every claimed row.
- Automated oracle: DuckDB Community CI build matrix followed by a stock-host
  lifecycle matrix that records complete host, source, signature-channel,
  platform, extension, SQL-contract, and failure identities.
- Quality gates: dependency-license audit, repository asset and source-identity
  validation, focused Query and Enablement oracles, fresh native product gate,
  Community CI, hosted custody upload/download verification, exact release
  evidence validation, and Git whitespace checks.
- Independent review: Query lifecycle, compatibility, diagnostics, and public
  guidance; Engineering Enablement provenance, dependency, CI, and custody;
  adversarial review of supply-chain, native lifecycle, and test-oracle
  boundaries.

### Contract and invariant impact

- RFC 0004, `docs/ARCHITECTURE.md`, and `ROADMAP.md` define the accepted channel
  and support policy. Delivery propagates the exact passing matrix into release
  notes, user guidance, provenance data, examples, diagnostics, and tests.
- The project and extension version advances to `0.2.0`; the connector
  specification and package-version domains do not change.
- The public SQL, rows, schema, offline bind/planning, strict conversion,
  bounded execution, cancellation, teardown, redaction, and native-boundary
  invariants remain unchanged.
- Community acquisition grants no connector runtime network authority, and
  signing proves Community CI provenance rather than source safety.

### Team and RFC routing

- Accountable stream: Query Experience owns the install, restart, load,
  identify, query, compatibility, diagnostic, and support narrative.
- Engineering Enablement — Facilitation: transfer Community build, matrix,
  dependency-audit, provenance, hosted-custody, and release-evidence practice.
  Exit when Query Experience independently maintains and diagnoses the user
  oracle and exact matrix without build or CI internals.
- RFC: RFC 0004 is Accepted. The product manager authorized the external
  Community submission proof; publication remains gated on the complete
  delivery evidence in this goal.

### Unknowns and first trial

- Unknown: whether the current native source, tests, compiler flags, and
  extension metadata pass the current Community toolchain and which platform
  rows survive the complete lifecycle oracle.
- Trial: build one immutable `0.2.0` source candidate with the same pinned
  Community toolchain, then open an authorized descriptor submission so the
  upstream matrix supplies the exact compatibility evidence before any support
  row or ordinary-user instruction is published.

### Publication-atomicity finding

Community pull-request CI can prove the build and test matrix, but it cannot
prove the final default-signature installation path. The pinned reusable
[distribution workflow](https://github.com/duckdb/extension-ci-tools/blob/72e76e99cd7fee45a99739cd118ec2db64e034ec/.github/workflows/_extension_distribution.yml#L1-L9)
uploads build artifacts without deploying them. The pinned Community
[build workflow](https://github.com/duckdb/community-extensions/blob/3a9af37016c909a7fb01510d1b63eda1e177da8a/.github/workflows/build.yml#L144-L160)
runs deployment only from upstream `main`, where the
[deployment job](https://github.com/duckdb/community-extensions/blob/3a9af37016c909a7fb01510d1b63eda1e177da8a/.github/workflows/_extension_deploy.yml#L121-L147)
receives the Community signing key.

Delivery therefore exhausts candidate, dependency, build, query, provenance,
and custody evidence before the upstream merge, then performs the exact stock
Community install immediately after deployment. The candidate commit is never
replaced. If that irreversible final proof fails, `0.2.0` remains a failed
public candidate and the correction advances to a new SemVer; no project tag,
source ref, or published identity moves.

### Delivery path

1. Produce a Community-compatible `0.2.0` source candidate, exact version and
   provenance identities, dependency audit, and separate Query and Enablement
   oracles.
2. Publish the immutable candidate, run the authorized Community descriptor
   and build matrix, and resolve only evidence-backed portability failures or
   exclusions.
3. After Community signing and publication, run the stock-DuckDB lifecycle
   oracle on every claimed row and the hosted custody round trip.
4. Record the exact matrix and policies in release guidance, complete
   independent review and topology-exit audit, tag the immutable source, and
   publish `0.2.0`.

## Completion record

### Delivered

An immutable, locally verified `0.2.0` source candidate and exact Community
descriptor proposal now exist. The repository also contains the dependency,
candidate, descriptor, upstream-build, native-signature-transition,
stock-DuckDB lifecycle, support-matrix-admission, and evidence-custody
boundaries needed to finish the Community path without reconstructing release
authority from chat or mutable branch state.

This is an **unreleased delivery checkpoint**, not completion of the product
outcome. DuckDB Community maintainers have not run, signed, deployed, or
accepted the candidate; no `v0.2.0` tag, public support matrix, ordinary-user
Community installation guidance, or `0.2.0` release exists.

### Evidence

- The immutable candidate is source commit
  `47dc6169ae820f70beb0c2722b8a8f5288cd1469`; the Community proposal is
  [duckdb/community-extensions#2256](https://github.com/duckdb/community-extensions/pull/2256).
- Candidate, dependency, descriptor, build-evidence, deployment-transition,
  Query lifecycle, and matrix-admission oracles pass locally and in the
  repository's pinned Linux container cell.
- Independent Query and supply-chain review found no remaining local defect in
  the pre-publication evidence boundaries.
- The upstream workflows remain subject to DuckDB Community maintainer
  authority, so none of the local evidence is represented as Community build,
  signature, deployment, or stock-install proof.

### Material decisions and deviations

- The product manager decided that real Community publication is not a
  prerequisite for continuing development of later product outcomes. The
  external publication gate is deferred rather than weakened or simulated.
- RFC 0004 remains the accepted ordinary-user distribution direction. Later
  product work may reuse the source-build developer path, but it cannot claim
  Community installation, a passing Community platform row, or a released
  `0.2.0` identity without completing this goal's remaining external evidence.

### Product options discovered

- Resume the pinned Community proposal when upstream maintainer capacity is
  available; its immutable source identity allows that work to proceed even
  after `main` advances.
- Revisit the ordinary-user distribution decision in a superseding RFC only if
  product evidence—not schedule pressure—shows that Community is the wrong
  long-term channel.
