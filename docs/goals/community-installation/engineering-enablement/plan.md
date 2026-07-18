# Engineering Enablement plan: Community delivery evidence

## Objective and boundary

Facilitate the `0.2.0` Community installation outcome by providing Query
Experience with a deterministic service for Community toolchain admission,
dependency-license evidence, upstream build provenance, artifact custody, and
release-evidence binding. RFC 0004 is Accepted and the product manager has
authorized the external Community submission proof.

Query Experience remains accountable for the DuckDB user's install, restart,
load-by-name, version, query, compatibility, diagnostic, support-matrix, and
ordinary-user guidance narrative. Engineering Enablement does not define a
supported row, interpret query results, own the stock-host lifecycle oracle, or
approve public acceptance. The lead agent owns integration, Git history, and
external-state changes. Native product fixes remain with the responsibility
that owns the affected source; portability evidence does not transfer product
source ownership to Enablement.

This is a bounded Facilitation interaction. The provider mechanics may remain
only as a low-coordination service after Query demonstrates independent use;
otherwise the goal retires the goal-specific tooling after evidence handoff.

## Provider interfaces

Enablement exposes commands and canonical, anchored records rather than Python
imports into Query code. Every command receives explicit paths, uses a new
caller-owned output root, rejects symlinks and existing output, and emits no
credentials, absolute runner paths, or unrestricted environment data.

| Interface | Provider inputs | Output consumed by Query or the release gate | Boundary |
| --- | --- | --- | --- |
| Candidate admission | Exact source commit, `0.2.0` pins, extension metadata, root MIT license, latest-stable DuckDB identity, and exact Community toolchain layout | `candidate.json` and `candidate.sha256` | Binds source commit/tree, project version, DuckDB identity, descriptor inputs, license digest, the complete pinned gitlink set, and exact `.gitmodules` metadata without consulting `HEAD`, index, or worktree. It contains no supported-platform claim |
| Dependency audit | Admitted source, actual Community build graph, lock/configuration inputs, and primary license texts | `dependency-audit.json`, notices inventory, and anchor | Requires every compiled, linked, bundled, or redistributed dependency to have identity, provenance, license evidence, and an explicit disposition; ambiguity is a hard failure |
| Descriptor admission | Tracked exact `description.yml`, reviewed `descriptor-cycle.json`, anchored candidate and dependency records, pins, and approved maintainer metadata | `descriptor-admission.json` and `descriptor-admission.sha256` in a new caller-owned output root | Requires MIT, `duckdb_api`, version `0.2.0`, C++/cmake, and the exact immutable candidate commit. The reviewed cycle—not co-located custody anchors—authorizes the handoff. It admits only a local proposal and never accepts a branch name, exclusion field, support claim, or self-reported ref as authority |
| Community build evidence | Explicit `duckdb/community-extensions` repository, workflow run, descriptor, job inventory, logs, and downloaded outputs | One `community-build.json` and anchor per row, a complete `community-builds.json` inventory, and explicit artifact paths | Records every job conclusion, DuckDB/toolchain/platform identity, artifact size/digest, and Community origin. A build pass is only a candidate row, not product support |
| Query handoff | Verified candidate, descriptor, Community job inventory, and admitted artifact paths | `query-inputs.json` plus read-only artifacts and manifests | Gives Query only stable identities and explicit paths. Query does not import build, descriptor, workflow, dependency, or custody internals |
| Release binding | Anchored provider records and a Query-owned result inventory supplied as opaque bytes | `release-evidence.json` and anchor | Binds provider and Query evidence without evaluating SQL, diagnostics, or supported-row meaning |
| Hosted custody | Exact release-evidence allowlist, staged root, and freshly downloaded root | `custody.json`, anchor, and staged/downloaded equality result | Verifies names, modes, sizes, digests, inner anchors, and byte equality across upload/download; the hosted artifact is a transfer mechanism, not a historical-availability promise |

The Query result inventory is a consumer-owned contract. Enablement may verify
its anchor and bind its bytes, but must not reconstruct its schema, select rows,
or treat a missing Query result as a build-system decision. A failed or skipped
Community job remains visible in provider evidence and cannot silently vanish
from Query's candidate-row inventory.

## Disjoint ownership

The implementation responsibility map is intentionally more specific than a
directory named for the team. Each planned file has one primary reason to
change.

| Enablement-owned file or subtree | Responsibility |
| --- | --- |
| `docs/goals/community-installation/engineering-enablement/plan.md` | This facilitation, handoff, and exit contract |
| `release/0.2.0/enablement/pins.json` | Approved latest-stable DuckDB, Community repository/toolchain, action, and evidence-schema identities for one candidate cycle |
| `release/0.2.0/enablement/descriptor.json` | Non-authoritative candidate-bound expectation with source and maintainer authority still unset |
| `release/0.2.0/enablement/description.yml` | Exact Community descriptor proposal for the published immutable candidate; no exclusions, docs, or support claims |
| `release/0.2.0/enablement/descriptor-cycle.json` | Reviewed authority binding the published source and exact proposal/candidate/dependency custody identities |
| `release/0.2.0/enablement/evidence-allowlist.json` | Exact files allowed through hosted custody and final release binding |
| `release/0.2.0/enablement/schemas/` | Versioned provider-record schemas only; no Query result or public support schema |
| `release/0.2.0/enablement/README.md` | Provider command contracts and failure categories for Query and the release gate |
| `scripts/community/verify_candidate.py` | Exact candidate commit/tree, version, license, DuckDB gitlink, and toolchain layout admission |
| `scripts/community/audit_dependencies.py` | Dependency enumeration and canonical license-evidence production |
| `scripts/community/descriptor_expectation.py` | Candidate-bound non-authoritative descriptor expectation validation |
| `scripts/community/candidate_record.py` | Complete provider candidate-record validation at downstream boundaries |
| `scripts/community/descriptor_cycle.py` | Reviewed descriptor-handoff authority validation; self-anchors remain custody only |
| `scripts/community/descriptor_proposal.py` | Dependency-free exact Community YAML grammar and proposal semantics |
| `scripts/community/verify_descriptor.py` | Thin anchored composition boundary for local descriptor admission |
| `scripts/community/collect_build_evidence.py` | Community run/job/output collection and provenance normalization |
| `scripts/community/write_query_inputs.py` | Read-only provider-to-Query artifact/path handoff |
| `scripts/community/bind_release_evidence.py` | Provider records plus opaque Query inventory binding |
| `scripts/community/stage_release_evidence.py` | Exact visible allowlist staging for hosted transfer |
| `scripts/community/verify_hosted_roundtrip.py` | Staged/downloaded inventories, inner anchors, and byte equality |
| `scripts/community/tests/` | Focused provider, descriptor, dependency, provenance, custody, and tamper oracles with deterministic fake upstream records |
| `scripts/test-community-enablement.sh` | Stable focused entry point and structural workflow guard |
| `.github/workflows/community-evidence-custody.yml` | Minimum-permission hosted provider gates, exact staging, pinned upload/download, and post-download verification |
| `docs/releases/0.2.0-supply-chain.md` | Maintainer runbook for descriptor, dependency, provenance, custody, and evidence commands; no ordinary-user installation guidance |

The upstream descriptor lives in `duckdb/community-extensions`; this repository
does not keep a mutable YAML mirror. Enablement prepares and validates the patch
and records its exact bytes and upstream identifiers. The lead agent performs
the authorized external mutation and integrates maintainer-requested changes.

Query owns its stock-DuckDB lifecycle and failure oracle, Query result schema,
support-matrix admission and interpretation, release notes, README installation
text, examples, diagnostics, GitHub Issues support wording, and public
acceptance. Enablement must not edit those files. `CMakeLists.txt`, `Makefile`,
`extension_config.cmake`, native sources, and product tests are integration
surfaces rather than Enablement property: Community failures become bounded
evidence for the lead to route to the owning responsibility.

Provider tests are split by failure authority:

- candidate tests cover dirty/mutable/wrong-version source, stale latest-stable
  DuckDB pins, license drift, and mismatched Community toolchain refs;
- dependency tests cover omitted transitive inputs, unknown or conflicting
  licenses, missing notices, duplicate identities, and generated output drift;
- descriptor tests cover movable refs, wrong repository/version/license/build/
  maintainer metadata, missing or extra fields, ambiguous YAML, candidate and
  dependency custody drift, and descriptor-to-candidate mismatch;
- build-evidence tests cover wrong repository/run/commit, missing or duplicate
  jobs, skipped/failed rows, platform-label collisions, changed artifacts,
  truncated logs, and extra outputs;
- custody tests cover hidden paths, symlinks, path escape, missing/extra files,
  malformed anchors, post-stage mutation, and regenerated downloaded content;
- release-binding tests prove Query records remain opaque and that a missing,
  changed, or unanchored consumer inventory cannot enter release evidence.

## Parallel workstreams

### Candidate and Community toolchain admission

Resolve the release-time latest stable DuckDB commit and the exact
`duckdb/community-extensions`, extension-template, extension-ci-tools, and
workflow-action refs from primary sources. Pin one coherent candidate cycle.
Verify that the clean `0.2.0` source commit and `extension_config.cmake` agree,
then reproduce the Community build locally where the official toolchain
supports it. A DuckDB stable release or toolchain-ref change invalidates the
candidate rather than being absorbed by a compatibility shim.

The first thin slice must establish the external publication sequence before
the descriptor can merge: determine whether Community pull-request CI exposes
an installable Community-signed artifact or staging repository that stock
DuckDB can exercise before ordinary publication. If it does not, stop at that
gate and return the atomicity constraint to the lead. Do not publish `0.2.0`
merely to obtain test bytes, replace a published candidate, or weaken signature
policy to simulate Community trust.

### Dependency-license evidence

Enumerate dependencies from the actual Community configure/build inputs and
link outputs, not only a hand-maintained manifest. Bind each dependency to its
source/version and primary license text, record whether it is built, linked,
bundled, or used only as a tool, and emit the notices required for
redistribution. A missing transitive dependency or unclear licensing result
blocks descriptor merge and release; tooling must not infer permission from the
project's MIT license or silently allow an unknown identifier.

This stream can run with candidate-toolchain work after the pins are frozen.
It reruns against the exact upstream build outputs before final release because
the Community environment, rather than a local guess, determines the delivered
dependency set.

### Upstream descriptor and build provenance

Produce one minimal descriptor patch bound to the admitted commit, project
version, MIT license, build language, repository, and maintainers. Capture the
external PR, workflow, attempt, job, runner/toolchain, and descriptor identities
without treating GitHub display state or a branch head as authority. Maintainer
revisions require a new descriptor digest and revalidation.

Collect the complete upstream job inventory, including failed, skipped,
excluded, and successful rows. Verify every downloaded artifact immediately,
retain the raw upstream platform label and exact DuckDB identity, and emit
candidate rows for Query. Enablement reports portability failures with their
source/build evidence but never converts them into public diagnostics or
support exclusions.

### Hosted custody and release evidence

Stage only the tracked allowlist into a new visible runner-temp root. Use
commit-pinned upload/download actions, `contents: read`, no ambient write
permission, no `pull_request_target`, no credentials in jobs that execute
untrusted source, bounded retention, and a second new download root. Re-run all
inner verifiers and compare every downloaded byte with the staged input.

The final evidence envelope binds the candidate, dependency audit, exact
descriptor, upstream job inventory, per-row artifacts, Query-owned inventory,
hosted custody result, source/tag relationship, and gate logs. It records
Community custody truthfully: project identities are immutable, while
Community endpoint rollback, cache retention, and historical availability are
not project guarantees.

### Transfer and upstream maintenance

Document the provider commands and rehearse them with Query using only the
anchored records and explicit paths. Preserve a small maintainer procedure for
future latest-stable transitions and Community `ref`/`ref_next` coordination,
but do not promise backports. A forward fix selects a new SemVer and immutable
source ref; no procedure may move a project tag or replace a released identity.

## Sequencing gates

1. **Interface gate:** Query and Enablement freeze the provider input/output
   names, authority, redaction, and ownership. Provider modules cannot import
   the Query oracle, and Query cannot import `scripts/community/` internals.
2. **Candidate gate:** one clean source commit, version `0.2.0`, latest-stable
   DuckDB identity, Community toolchain refs, MIT digest, and dependency audit
   pass together. Any pin change restarts this gate.
3. **Publication-atomicity gate:** a primary-source or executable probe proves
   how the exact Community-trusted candidate can pass the stock-host oracle
   without making an unproved `0.2.0` artifact ordinary-user-visible. If no
   safe route exists, external merge/publication stops for lead disposition.
4. **Descriptor gate:** the authorized upstream patch names only the admitted
   immutable commit and passes local plus upstream descriptor checks. A
   maintainer edit reopens admission.
5. **Build-evidence gate:** the complete Community job inventory and every
   candidate artifact are anchored. Missing, skipped, failed, or ambiguous rows
   remain visible and are never inferred as passing.
6. **Query gate:** Query consumes `query-inputs.json` and owns the stock-host
   lifecycle result for every candidate row. Only Query may turn build plus
   lifecycle evidence into the exact public support matrix.
7. **Custody gate:** the provider records, opaque Query inventory, artifacts,
   logs, and anchors survive a real hosted upload/download round trip with exact
   byte equality.
8. **Release gate:** descriptor source, final immutable source/tag, extension
   version, dependency audit, upstream artifacts, Query matrix, custody, and
   release evidence all agree. The source tag must resolve to the candidate
   commit and must never be moved. Independent supply-chain and test-oracle
   review precede publication and guidance.
9. **Transfer gate:** the final dependency direction and the measurable exit
   evidence below are audited before Facilitation closes.

## Hazards and required containment

- **Upstream release drift:** a new latest stable DuckDB during delivery makes
  the old matrix stale. Freeze a candidate cycle or restart; never claim two
  DuckDB releases under the latest-only policy.
- **Publish-to-test circularity:** Community signing or stock installation may
  exist only after merge. Resolve that fact before publication; do not replace
  a bad public `0.2.0` or use unsigned loading as acceptance evidence.
- **Mutable external identity:** bind repository, PR, workflow, attempt, job,
  descriptor, commit, and artifact digests. A run number or URL alone is not
  provenance.
- **Partial matrix:** failed, cancelled, skipped, excluded, duplicated, or
  uncollected jobs cannot disappear from evidence. Absence is unclaimed, not a
  pass.
- **Platform-name ambiguity:** preserve Community's raw row plus runner and
  toolchain facts. Query owns the mapping to published platform names.
- **Dependency undercount:** compare configured inputs, compiler/linker
  observations, and bundled files. Unknown or conflicting licenses fail closed.
- **Signature overclaim:** record Community origin and verify through stock
  default policy; never describe signing as a source audit.
- **Untrusted CI input:** external PR code receives no project or publication
  secrets. Workflows use minimum permissions, pinned actions, explicit
  repositories/commits, timeouts, and bounded output.
- **Custody substitution:** verify before staging, after download, and before
  release binding. Reject hidden, linked, extra, regenerated, or changed bytes.
- **Retention overclaim:** hosted artifacts are finite-lived transfer evidence;
  release guidance promises no Community rollback or historical availability.
- **Cross-team coupling:** provider failures expose typed evidence categories,
  not Query SQL or diagnostic rules. Routine Query maintenance must not require
  Enablement edits.

## Transfer evidence and Facilitation exit

Facilitation remains **Open** until all of the following are observable:

- Query runs its documented oracle across `query-inputs.json`, identifies the
  exact passing rows, and diagnoses one deliberately failed provider row
  without importing or editing `scripts/community/`, the workflow, or custody
  code.
- A fresh-context handoff receives only the provider README, pinned inputs, and
  explicit external run identifiers, then reproduces the candidate-row handoff
  and Query-owned matrix without Enablement explaining build internals.
- Candidate, dependency, descriptor, build, tamper, and custody test families
  pass; a real hosted upload/download round trip proves exact evidence
  survival; and negative fixtures show source, license, job, artifact, and
  consumer-inventory drift fail at the owning boundary.
- Query changes a lifecycle or support-matrix assertion without changing the
  provider service, while a provider provenance/custody fixture changes without
  changing Query SQL or diagnostics. The final source and test dependencies
  demonstrate that separation.
- Query owns ongoing user-oracle, matrix, diagnostic, guidance, and GitHub
  Issues support maintenance. Any retained generic descriptor/provenance/
  custody tooling has a named maintainer, documented compatibility rule, and
  deterministic invocation that requires no Enablement approval.
- The `0.2.0` release evidence records the immutable project identity, exact
  claimed rows, dependency result, Community provenance, hosted custody, and
  known external-retention limits without transferring public acceptance to
  Enablement.

If any later latest-stable transition routinely requires Enablement to inspect
Query SQL, catalog state, diagnostics, or support prose, the interaction exit
is still Open and the provider boundary must be corrected rather than hidden by
a passing end-to-end workflow.
