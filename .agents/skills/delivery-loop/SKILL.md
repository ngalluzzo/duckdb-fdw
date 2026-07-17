---
name: delivery-loop
description: Orchestrate nontrivial implementation, refactoring, bug-fixing, integration, and repository setup in duckdb-fdw from acceptance evidence through bounded implementation, independent review, verification, and a Conventional Commit. Use when a requested change spans multiple steps or files, needs technical orchestration, or must be carried through to a verified result. Do not use for read-only questions, review-only requests, or trivial edits with an obvious single check.
---

# Delivery Loop

Carry a product outcome through engineering completion while keeping the lead
context focused on decisions and evidence.

## Establish the outcome

1. Read `AGENTS.md`, the current worktree, and the relevant source-of-truth
   documents. For product delivery or a durable shared decision, this includes
   `docs/PRODUCT_DELIVERY.md`, `docs/TEAM_TOPOLOGY.md`, and
   `docs/RFC_PROCESS.md`.
2. Restate the request internally as:
   - observable outcome;
   - constraints and invariants;
   - evidence that proves completion;
   - decisions that truly require the product manager.
3. For a product goal, verify one accountable stream team, supporting
   interactions and exit conditions, and the governance route recorded in the
   persistent goal. For non-product work, identify the owning topology team and
   objective without inventing a customer outcome.
4. Apply the mandatory RFC triggers before the diagnostic question or
   exemptions. If an RFC is required, verify that it is Accepted before work
   establishes the disputed public or shared contract. Without acceptance,
   limit work to the RFC itself or a separately bounded evidence trial that
   cannot establish that contract. Follow the recorded emergency-containment
   exception only for urgent security or correctness work.
5. Inspect existing code, tests, history, and configuration before proposing new
   structure.
6. Verify unstable external facts from primary sources. Record durable findings
   in tests, compatibility data, or documentation rather than relying on chat
   memory.

Escalate only the decision classes listed in `AGENTS.md`. Make ordinary
technical decisions autonomously and explain consequential tradeoffs in the
commit or final handoff.

## Plan from evidence

Use the task plan for multi-step work. Put an unresolved RFC gate before the
affected implementation step. Each step must produce evidence toward the full
outcome; do not redefine success around an easy subset.

Identify the cheapest convincing oracle before implementation. Prefer, in
order:

1. an existing failing test or reproducible defect;
2. a deterministic fixture and expected result;
3. a property or invariant test;
4. a narrow executable probe;
5. manual inspection only when automation is disproportionate.

When a boundary is unproven, implement one thin end-to-end trial before scaling
parallel work. Preserve the full requested outcome while using the trial to
validate architecture and testability.

## Graduate trials into production design

Before promoting a greenfield trial or scaling a vertical slice, perform a
responsibility pass. A successful experiment proves feasibility, not production
source shape.

1. Map every affected team interface to its producer, consumers, contract, and
   interaction exit condition.
2. Assign each production module one primary responsibility and reason to
   change. Keep adapters at the edges and dependencies pointed from consumers
   to provider interfaces, never provider internals.
3. Split independently changing connector metadata, relational planning,
   runtime execution, and host integration. Do not create arbitrary files or a
   line-count target; justify intentional co-location by shared invariants and
   ownership.
4. Give each oracle family a home matching the production responsibility. Put
   reusable fixtures and probes in explicit test support and keep end-to-end
   behavior in integration tests.
5. Apply the selected charters' code-documentation expectations to team APIs,
   lifecycle-sensitive state, non-obvious algorithms, policy ordering, and
   compatibility boundaries.
6. Use `$topology-consult` when the design crosses team APIs or claims that an
   interaction has reached its exit. Record any exit that remains open instead
   of inferring independence from a named type or passing integration test.

Keep the responsibility map in the active goal or task plan while delivery is
underway. Put only durable module and dependency intent in shared engineering
documentation.

## Delegate deliberately

Use subagents for independent primary-source research, read-heavy exploration,
test or log triage, and fresh-context review. Delegate implementation only when
file ownership is disjoint or agents have separate worktrees.

For every subagent, provide:

- one bounded question or deliverable;
- authoritative inputs to inspect;
- prohibited mutations, if any;
- the evidence and concise output expected.

The lead agent owns integration and Git history. Do not allow shared-state
coordination through stash, reset, broad checkout, or cleanup commands.

## Implement the behavior

1. Use `$contract-change` when semantics cross documented layers.
2. Add or strengthen the oracle before or with the implementation.
3. Implement complete behavior; reject stubs, silent degradation, speculative
   compatibility shims, and disabled tests.
4. Keep changes cohesive by responsibility, not by accumulating the whole
   outcome in a catch-all module or test suite.
5. Add adjacent code documentation required by `AGENTS.md` and the owning
   charters while the design decisions are fresh.
6. Preserve unrelated worktree changes and run fast, targeted checks during
   iteration.

If implementation exposes a flawed assumption, update the plan and the
authoritative contract rather than hiding the discrepancy in code.

If implementation discovers an RFC trigger, stop at that boundary and preserve
completed in-bounds work. Do not let partially written code make the disputed
decision by default.

## Review independently

Use `$adversarial-review` for substantive or high-risk changes. Reviewers must
receive the target diff and relevant contracts without the implementer's
reasoning or desired conclusion.

Use at least two independent perspectives for changes involving relational
semantics, network access, secrets, replay, caching, concurrency, FFI, or
lifecycle behavior. Validate findings before changing code, then re-run the
focused review after fixes.

## Verify and commit

1. Run targeted tests, then every relevant repository gate from `AGENTS.md`.
2. Inspect the final diff for accidental files, weakened assertions, stale
   terminology, secrets, and unrelated changes.
3. Compare the final source, include, build-target, and test dependencies with
   the responsibility map. Confirm each claimed topology exit against actual
   consumer-provider separation and adjacent interface documentation.
4. Audit each acceptance requirement against authoritative evidence.
5. Audit the governance record: accepted RFCs are propagated to every affected
   contract, or an emergency containment exception records every item required
   by `docs/RFC_PROCESS.md`, including any reserved product decision or
   deferral. A bounded trial must still leave the disputed contract
   unestablished.
6. Commit one coherent change with a Conventional Commit subject and a body for
   non-obvious motivation or tradeoffs.
7. Confirm the expected worktree state after the commit.

Do not claim completion from a narrow green check. If evidence is incomplete,
continue working or report the exact missing authority or external condition.

## Improve the system

When a failure repeats, or one incident demonstrates a systemic process gap
with a concrete recurrence path, update the smallest durable practice that
would prevent it. Use a regression test for deterministic behavior; use
guidance, a skill, or a charter clarification for design-culture failures. Do
not automate subjective judgment merely to make the control look deterministic.
