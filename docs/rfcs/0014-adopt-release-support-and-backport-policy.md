# RFC 0014: Adopt a release, support, and backport policy

```yaml
rfc: "0014"
title: "Adopt a release, support, and backport policy"
status: "Accepted"
rfc_type: "Non-product"
sponsor_team: "Engineering Enablement"
technical_decision_owner: "Lead agent"
product_approver: "Nic Galluzzo"
authors:
  - "Lead agent"
required_reviewers:
  - "Connector Experience"
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
affected_teams:
  - "Connector Experience"
  - "Query Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
linked_outcome_or_objective: "0.9.0 public connector authoring and API candidate (docs/goals/stable-local-connector-packages.md's successor goal)"
supersedes: "Not applicable"
```

## Summary

Adopts a best-effort, latest-release-only support policy: only the most
recent `1.x` release is supported, breaking changes ship with release notes
and migration guidance but no formal deprecation window or LTS branch, there
is no backport policy, and security issues are handled best-effort through
the same public GitHub Issues channel as ordinary defects. This closes the
release-and-support-policy gate `ROADMAP.md` requires before `0.9.0`.

## Sponsorship and context

- **RFC type:** Non-product.
- **Sponsoring team:** Engineering Enablement. This RFC decides a release-gate
  policy applied uniformly across every team's public surface; it does not
  belong to one stream team's product outcome.
- **Linked outcome or objective:** Unblocks the `0.9.0` "public connector
  authoring and API candidate" goal. `ROADMAP.md`'s "Release-governance
  decision and remaining gates" section states: "Before `0.9.0`, an
  Engineering Enablement release-and-support-policy RFC with product-manager
  approval decides support windows and removals, deprecation, migration,
  security response, maintenance, and backport policy."
- **Why now:** `0.9.0` freezes the `1.0.0` public contract for compatibility
  testing. That freeze is meaningless without a stated policy for what happens
  when the contract needs to change later — otherwise every future breaking
  change is an ungoverned, ad hoc decision.

## Problem

`ROADMAP.md` and RFC 0009 already establish durable choices this RFC builds on
rather than re-decides: the project is MIT licensed, DuckDB Community
Extensions is the ordinary-user distribution and trust path, source build is
the contributor path, releases are immutable, and "support remains
best-effort through GitHub Issues." What is missing is the specific mechanics
`ROADMAP.md` names as a `0.9.0` prerequisite: support windows and removals,
deprecation, migration, security response, maintenance, and backport policy.

Concrete scenario this RFC must answer: a connector author has a package
loaded against `1.2.0`. `2.0.0` ships a breaking change to a diagnostic code
they depend on, per RFC 0009's project-SemVer commitment that a breaking
change to governed public behavior carries "the SemVer consequence dictated
by the then-current public contract" — i.e., a MAJOR version increment, not a
minor or patch one. Today there is no documented answer to how much notice
they get before that major release ships, whether `1.2.0` keeps receiving
fixes, how to report a security issue privately if they find one, or whether
any release beyond the latest is ever patched.

Observed facts: single-maintainer project, MIT licensed, no existing
`SECURITY.md`, no existing LTS or backport practice, best-effort GitHub Issues
support already established by RFC 0009. Assumption carried into the decision
below: project scale and maintainer capacity do not currently support a
multi-branch maintenance commitment; this RFC does not preclude adopting one
later if that changes.

## Decision drivers and invariants

- **Must preserve:** RFC 0009's existing durable choices (MIT license,
  Community Extensions distribution, best-effort GitHub Issues support,
  immutable releases).
- **Must enable:** A stated, testable policy the `0.9.0` release gate can
  point to when freezing the `1.0.0` public contract's migration and
  deprecation rules.
- **Must not introduce:** A support commitment the project cannot sustain
  (e.g., a multi-version LTS or backport promise with no maintainer capacity
  behind it), or an undocumented security-reporting path that leaves
  reporters guessing.

## Proposed decision

Adopt best-effort, latest-release-only support:

1. **Support window:** Only the latest published release is supported,
   whatever its version number. There is no LTS release and no committed
   support window measured in time; "supported" means the maintainer will
   consider issues against it on a best-effort basis, consistent with RFC
   0009's existing GitHub Issues support channel.
2. **Removals and deprecation:** This RFC does not change RFC 0009's project-
   SemVer commitment: a breaking change to governed public behavior (SQL,
   connector-package syntax, diagnostic behavior, or any other category in
   RFC 0009's "Public behavior and SemVer inventory") still requires a MAJOR
   version increment, never a minor or patch one. What this RFC removes is
   an *advance-notice* window ahead of that major release (e.g., no "must
   ship a warning one minor version before removal" rule) — the major bump
   itself remains the compatibility signal. It does require the release
   gate's existing "curated release notes and migration guidance for every
   incompatible change" (`ROADMAP.md`'s Release gate, item 6) in the same
   release that introduces the break. This keeps the commitment honest about
   maintainer capacity while still giving connector authors a documented
   upgrade path and an unambiguous, SemVer-encoded signal that one exists.
3. **Migration:** Migration guidance is release-note-driven, not a separate
   migration-tool or compatibility-shim commitment. `duckdb_api/v1`'s own
   migration and reload behavior (RFC 0012, RFC 0013) is unaffected.
4. **Security response:** Ordinary security issues are reported and tracked
   through the same public GitHub Issues channel as ordinary defects — there
   is no committed response SLA at this project scale. This is a real
   tradeoff (see Drawbacks) accepted explicitly rather than left
   undocumented. Because Remote Runtime's surface (transport, authentication,
   network/host policy, credentials) is where a defect report is most likely
   to require reproducing with a live hostname, token, or full request/
   response body, `SECURITY.md` instructs reporters to redact hostnames,
   tokens, and full request/response bodies from a public issue and describe
   the flaw abstractly, with GitHub's private vulnerability reporting feature
   (repository Security tab → "Report a vulnerability") as the fallback for
   reports that cannot be abstracted without losing reproducibility. This
   adds no email or other personal contact channel and no response-time
   commitment beyond the same best-effort basis as ordinary defects.
5. **Maintenance and backport policy:** No backports. A fix ships only in the
   next release built from the latest source; there is no maintained branch
   for a prior release.

### Public behavior

Adds one durable policy statement (this RFC) that `0.9.0`'s frozen `1.0.0`
public contract must reference for its migration/deprecation rules. Does not
change any current SQL, connector-package, or diagnostic behavior.

### Shared interfaces

Not affected. This RFC decides operating policy, not a team API or compiled
IR shape.

### Operational behavior

Establishes that release-note-driven migration guidance (already a release-
gate requirement) is the sole deprecation mechanism, and that security
reports flow through the existing public issue tracker rather than a new
private channel.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Engineering Enablement | Sponsoring team; owns the release-gate policy artifact | Adds a policy reference the release gate checks | Facilitation | Every domain team can apply the policy to its own public-surface changes without an Enablement approval queue |
| Connector Experience | Affected: connector-package compatibility/exclusion changes follow this policy | Its public-surface changes must ship release notes and migration guidance per this policy, no new approval step | X-as-a-Service | Team applies the policy independently when it changes public connector-package behavior |
| Query Experience | Affected: SQL/diagnostic breaking changes follow this policy | Same as above, for SQL/diagnostic surface | X-as-a-Service | Team applies the policy independently |
| Remote Runtime | Affected: no public surface of its own, but its behavior changes can force Query/Connector-owned breaking changes that follow this policy | No interface change | X-as-a-Service | Not applicable beyond Query/Connector's own exit |
| Relational Semantics | Affected: same as Remote Runtime | No interface change | X-as-a-Service | Not applicable beyond Query/Connector's own exit |

No accountability or interface boundary moves. No charter update is required.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** Not affected.
- **Authentication, credentials, network policy, and privacy:** Not affected.
- **Resource budgets, backpressure, and cancellation:** Not affected.
- **Replay units, retries, caching, and duplicate prevention:** Not affected.
- **Concurrency, immutability, and state ownership:** Not affected.
- **FFI, initialization, reload, shutdown, and failure containment:** Not
  affected.
- **Diagnostics, redaction, metrics, and progress reporting:** Affected only
  in that every future breaking diagnostic change must ship release notes
  under this policy; no new diagnostic behavior is introduced.

## Compatibility and migration

- No existing users, connector packages, or stored data are affected by
  adopting this policy itself.
- Compatibility guarantee: none beyond "latest release only," explicitly
  weaker than a versioned-support promise. This is the central tradeoff of
  the decision (see Drawbacks).
- No migration or coexistence behavior changes.
- No rollback conditions apply; this is a policy adoption, not a code change.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Does RFC 0009 already commit to best-effort GitHub Issues support? | Primary-source citation | Read `ROADMAP.md`'s "Release-governance decision and remaining gates" section | Confirmed: "support remains best-effort through GitHub Issues." This RFC extends, not replaces, that choice. |
| Is there an existing security-reporting channel or `SECURITY.md`? | Repository search | `find`/`ls` for `SECURITY.md`; grep `CONTRIBUTING.md` for security process | Confirmed: none exists. This RFC is the first record of a security-reporting answer. |
| Is a multi-version LTS or backport commitment sustainable today? | Maintainer capacity | Product-manager decision (single-maintainer project, no evidence of dedicated release-engineering capacity) | Decided against for this RFC; revisit if maintainer capacity changes. |

No decision-critical uncertainty remains pending.

## Alternatives considered

- **Formal deprecation window (e.g., one minor version's notice before
  removal):** More author-friendly, but adds process overhead disproportionate
  to current project scale and is not required by any existing evidence of
  author harm. Rejected for `1.0.0`'s initial policy; revisit if adoption
  grows enough to justify the overhead.
- **LTS branch with backports:** Gives long-lived deployments a stable patch
  target, but requires maintaining multiple branches and back-porting fixes,
  which no current evidence supports as sustainable at this project's scale.
  Rejected.
- **Private security disclosure channel (e.g., a security email alias):**
  More conventional for OSS security practice, but introduces an operational
  commitment (monitoring a private channel, coordinating disclosure timing)
  the project has no established process for today. Rejected for now; noted
  as a follow-on option below since it is a real gap.
- **Retain the status quo (no explicit policy):** Leaves `0.9.0` unable to
  freeze `1.0.0`'s migration/deprecation rules, which `ROADMAP.md` requires.
  Not viable given the linked outcome.

## Drawbacks and failure modes

- **Reduced security disclosure privacy:** Ordinary security issues are still
  reported through public GitHub Issues, disclosing them publicly at report
  time. GitHub private vulnerability reporting mitigates this for reports
  that cannot be safely redacted (principally Remote Runtime's transport/
  credential/host surface), but a reporter who does not know about or use
  that path still discloses publicly by default. Engineering Enablement owns
  revisiting this if the project's user base or risk profile changes.
- **No support for pinned older versions:** A connector author who cannot
  upgrade immediately gets no fixes until they do. Connector Experience and
  Query Experience own communicating this clearly in release notes so authors
  can plan upgrades deliberately.
- **No advance deprecation notice ahead of a major release:** A breaking
  change still requires a MAJOR version increment per RFC 0009's project-
  SemVer commitment, but that major release can land with only release-note
  documentation, not advance warning of the coming bump. This trades author
  planning time for lower maintenance overhead; Engineering Enablement owns
  monitoring whether this becomes a recurring pain point worth revisiting.

## Acceptance and verification

- **End-to-end demonstration:** This document, once Accepted, is the
  discoverable policy `0.9.0`'s frozen `1.0.0` public contract cites for its
  migration and deprecation rules.
- **Automated oracle:** Not applicable; this is a policy decision, not
  executable behavior. `0.9.0`'s release gate verifies the contract-freeze
  artifact references this RFC.
- **Quality gates:** `ruby scripts/validate-agent-assets.rb`, `git diff --check`
  (documentation-only change).
- **Independent review:** `$topology-consult` perspectives from Connector
  Experience, Query Experience, Remote Runtime, and Relational Semantics
  (recorded below), since every team's public surface is bound by this
  policy.
- **Interaction exit:** Every domain team can cite this policy when shipping
  its own breaking change without an Engineering Enablement approval step.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Not affected | No product or relational invariant changes | Not applicable |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Not affected | No package-syntax change | Not applicable |
| `docs/RUNTIME_CONTRACTS.md` | Not affected | No compiled IR or runtime behavior change | Not applicable |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | No accountability or interface boundary moves | Not applicable |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | No process change; this RFC uses the existing RFC process as-is | Not applicable |
| `ROADMAP.md` | Affected | Record this RFC as satisfying the `0.9.0` release-and-support-policy prerequisite | Pending: update on acceptance |
| `SECURITY.md` | Affected | Add a `SECURITY.md` documenting the public-issue-tracker reporting path, the redaction guidance for transport/credential/host-surface reports, and GitHub private vulnerability reporting as the fallback | Pending: add on acceptance |

## Unresolved questions

- Should the project later add a private security-disclosure channel as
  adoption grows? Non-blocking; recorded as a follow-on option below.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Connector Experience | Connector Experience | Approved | No `CompiledConnector` or author-workflow impact; RFC 0013 already deferred the deprecation-window question to this RFC's gate; Connector/Query jointly own release-note communication, matching the charter's diagnostics responsibility. | Accepted as evidence; no action required. |
| Query Experience | Query Experience | Objected | The RFC's original worked example (a breaking diagnostic change landing in a MINOR release, `1.2.0` → `1.3.0`) contradicted RFC 0009's project-SemVer commitment that a breaking change carries "the SemVer consequence dictated by the then-current public contract" — i.e., a MAJOR bump. | Confirmed against RFC 0009 (`docs/rfcs/0009-set-native-v1-boundary.md:370-371`) and corrected: the worked example, "Removals and deprecation" item, and a Drawbacks bullet now state explicitly that a breaking change still requires a MAJOR version increment; only the *advance-notice window* ahead of that release is removed, not the version-bump discipline itself. |
| Remote Runtime | Remote Runtime | Objected | Public-only security disclosure with no redaction guidance is an operational hazard specific to Remote Runtime's credential/host/redirect surface: reproducing a host-policy-bypass or credential-binding defect typically requires pasting a live hostname, token, or request/response body, which a public issue with no SLA leaves exposed indefinitely. | Addressed: the "Security response" item and `SECURITY.md` propagation row now require redaction guidance (redact hostnames, tokens, and bodies; describe the flaw abstractly) plus GitHub private vulnerability reporting as a documented fallback, per the product manager's confirmed choice. |
| Relational Semantics | Relational Semantics | Approved | No `ScanRequest`/`ScanPlan` or correctness-proof impact; the RFC governs post-ship support mechanics, not the pre-ship review bar a change must clear, which remains governed by `docs/RFC_PROCESS.md` regardless of deprecation policy. | Accepted as evidence; no action required. |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Approved by Nic Galluzzo — best-effort, latest-release-
  only support policy (as opposed to a formal deprecation-window variant),
  and GitHub private vulnerability reporting (as opposed to a published
  personal email) as the security-report fallback channel.
- **Rationale:** Both material objections identified real gaps rather than
  style preferences: Query Experience caught a genuine inconsistency with
  RFC 0009's already-accepted SemVer commitment, and Remote Runtime
  identified a concrete operational hazard specific to its credential/host
  surface. Both are resolved with targeted, evidence-driven revisions rather
  than overridden — this RFC does not weaken any invariant to obtain
  acceptance. Connector Experience and Relational Semantics found no
  interface or correctness impact within their charters.
- **Material objections:** Query Experience's SemVer inconsistency (resolved:
  worked example and policy text corrected to require a MAJOR version for
  breaking changes). Remote Runtime's disclosure-redaction gap (resolved:
  redaction guidance and GitHub private vulnerability reporting added to the
  security-response policy and `SECURITY.md` propagation requirement).
- **Superseded by:** Not applicable.

**Status: Accepted.**

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Add a private security-disclosure channel if project adoption or risk profile grows | Engineering Enablement | None identified | Evidence of real reporter demand or a security incident that public disclosure made worse |
| 0.9.0 public connector authoring and API candidate | Connector Experience | Query Experience, Remote Runtime, Relational Semantics (X-as-a-Service); Engineering Enablement (Facilitation) | This RFC Accepted |
