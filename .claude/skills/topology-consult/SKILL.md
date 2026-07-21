---
name: topology-consult
description: Route duckdb-fdw product goals, RFCs, implementation designs, interaction-exit audits, and cross-team reviews through the project topology; select the accountable and affected teams, load their authoritative charters, collect independent evidence-backed perspectives when needed, and produce interaction records or RFC review rows. Use when assigning team ownership, invoking or consulting a team, evaluating topology or source-dependency impact, resolving a cross-team question, checking whether a team interaction has exited, or satisfying affected-team review under docs/RFC_PROCESS.md. Do not summon every team when the decision does not affect them.
---

# Topology Consult

Treat teams as accountability contexts, charters as sources of truth, and
agents as temporary reviewers. Never reproduce a charter inside this skill or
invent a standing team persona.

## Establish the decision boundary

1. Read `AGENTS.md` and `docs/TEAM_TOPOLOGY.md` completely.
2. Identify the artifact and requested mode:
   - **Route:** assign accountability and supporting interactions without
     requesting team review.
   - **Consult:** gather input on a concrete question or proposal.
   - **Implementation exit audit:** determine whether final source, test, and
     dependency boundaries satisfy recorded interaction exits.
   - **RFC review:** produce the required review record under
     `docs/RFC_PROCESS.md`.
3. Read `docs/RFC_PROCESS.md` and the RFC completely for RFC review. Read the
   relevant product and engineering contracts for any consultation whose
   answer depends on behavior or invariants.
4. State the decision, observed facts, unknowns, and authority. Keep product
   manager choices, lead-agent technical authority, and team input distinct.

Do not use team consultation to disguise an unresolved product decision or to
transfer technical decision ownership from the lead agent.

## Route the minimum topology

Choose one accountable team for the outcome or non-product objective:

- Use Connector Experience when acceptance ends with a connector author
  creating, validating, testing, explaining, or maintaining a package.
- Use Package Ecosystem when acceptance ends with a publisher, package
  consumer, or registry operator publishing, discovering, acquiring,
  verifying, composing, reproducing, or governing a package release.
- Use Query Experience when acceptance ends with a DuckDB user querying,
  inspecting, or diagnosing remote data.
- For a genuine non-product objective, use whichever charter owns the operating
  or engineering area, including a stream team, without inventing a product
  outcome.
- Never make Remote Runtime, Relational Semantics, Trust & Provenance, or
  Engineering Enablement accountable for a product goal. Platform or
  subsystem work framed as product delivery retains a consuming stream team.

Add an affected team only when the proposal changes its charter, consumes or
provides its team API, creates operational burden for it, or requires its
specialist evidence. Do not consult every team as ceremony.

Read every selected charter completely and no unselected charter unless
routing remains genuinely ambiguous:

- `docs/teams/CONNECTOR_EXPERIENCE.md`
- `docs/teams/PACKAGE_ECOSYSTEM.md`
- `docs/teams/QUERY_EXPERIENCE.md`
- `docs/teams/REMOTE_RUNTIME.md`
- `docs/teams/RELATIONAL_SEMANTICS.md`
- `docs/teams/TRUST_AND_PROVENANCE.md`
- `docs/teams/ENGINEERING_ENABLEMENT.md`

For each supporting team, record Collaboration, X-as-a-Service, or
Facilitation plus the learning objective or service expectation and an
observable exit condition.

## Evaluate implementation boundaries

For an implementation design or exit audit, inspect the actual declarations,
includes, construction and composition points, build targets, tests, and
adjacent code documentation. Do not accept a semantic type name or a passing
end-to-end test as proof of a low-coupling interface.

- Confirm that consumers depend on the provider's bounded team API without
  constructing, retaining, or reinterpreting provider internals.
- Confirm that provider responsibilities and their oracle families can be
  understood and exercised without unrelated consumer integration machinery.
- Inspect the target's source inventory as well as its link names. A focused
  consumer target that directly compiles provider production sources or uses a
  provider-private test constructor has not reached X-as-a-Service; a provider
  fixture target may hide its own construction machinery behind a bounded test
  API.
- Require durable ownership to be discoverable from adjacent source/test
  organization and enforceable target dependencies. CMake source-list labels,
  commit scopes, and archived workstream plans alone do not establish it.
- Apply each selected charter's cognitive-load and code-documentation
  expectations, including ownership, lifecycle, errors, resource authority,
  and semantic rationale where relevant.
- Evaluate responsibilities and dependency direction rather than demanding a
  directory or library named after each team.
- Mark an interaction exit `Open` when future changes still require routine
  cross-boundary knowledge or coordinated edits outside the recorded service
  contract.

## Gather team input

For simple routing, remain in the lead context and do not create reviewers.

For an explicit consultation, required RFC review, or consequential cross-team
decision, use one fresh-context subagent for each selected team perspective.
Set `fork_turns` to `none`; do not use the default inherited conversation.
Run independent reviewers in sequential batches when concurrency is limited. A
reviewer receives only:

- the concrete question or artifact;
- its team charter;
- the topology and authoritative contracts needed to judge it;
- the evidence and output format required; and
- a prohibition on editing unless implementation was separately assigned.

Do not reveal the desired conclusion, another team's response, or the
implementer's reasoning. One reviewer may cover multiple teams only when the
environment cannot create enough fresh contexts; preserve a distinct
disposition for each charter and disclose the reduced independence. Never
report a simulated approval as an actual team review.

Require each team perspective to return:

1. `Approved`, `Objected`, or `Needs evidence`;
2. the charter responsibility or interface affected;
3. evidence from contracts, tests, fixtures, or a concrete counterexample;
4. the product, correctness, operational, or cognitive-load consequence;
5. required action or evidence, if any; and
6. the interaction exit condition and whether current implementation evidence
   makes it `Open` or `Satisfied`.

An objection must identify a violated contract or invariant, an unacceptable
product consequence, an operational hazard, or missing decision-critical
evidence. Preference alone is not an objection.

## Integrate dispositions

Validate every response against the source artifact and contracts. Resolve
factual conflicts before presenting the result; do not decide by vote.

Return a routing record:

```markdown
### Topology routing

- Accountable team: <team and outcome or objective>
- Affected teams: <team and reason, or none>
- Interactions: <team, mode, purpose, and exit condition>
- Decision authority: <lead agent, product manager, or delegated owner>
```

For consultation, follow it with one row per actual team perspective:

```markdown
| Team perspective | Result | Evidence or objection | Required action | Exit condition |
| --- | --- | --- | --- | --- |
| <team> | Approved, Objected, or Needs evidence | <specific evidence> | <action or none> | <observable condition> |
```

For an RFC, map each actual reviewer to exactly one row in the RFC template's
review record. The RFC template permits only Pending, Approved, or Objected, so
map `Needs evidence` to `Objected` and state the missing decision-critical
evidence as the objection. Keep the RFC In review while a required perspective
is missing or an objection lacks evidence or a decision-owner disposition.
Decision-critical missing evidence cannot be dispositioned away: obtain it and
repeat the affected review, or return the RFC to Draft with a concrete evidence
requirement. Team approval supplies input; only the recorded decision owner
changes the RFC to Accepted or Rejected.

## Preserve boundaries

- Do not let a stream team prescribe platform internals beyond its consumer
  contract.
- Do not let a platform or subsystem team assume product accountability.
- Do not let Engineering Enablement become a permanent approval gate or take
  quality ownership from another team.
- Do not let Relational Semantics weaken DuckDB meaning to obtain an
  optimization.
- Do not use a charter to override `AGENTS.md`, the topology overview, an
  accepted RFC, or product and engineering contracts.
- Use `$adversarial-review` in addition to this skill when repository rules
  require risk-focused independent review. Team consultation is not a
  substitute for verification.
