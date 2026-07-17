---
name: delivery-loop
description: Orchestrate nontrivial implementation, refactoring, bug-fixing, integration, and repository setup in duckdb-fdw from acceptance evidence through bounded implementation, independent review, verification, and a Conventional Commit. Use when a requested change spans multiple steps or files, needs technical orchestration, or must be carried through to a verified result. Do not use for read-only questions, review-only requests, or trivial edits with an obvious single check.
---

# Delivery Loop

Carry a product outcome through engineering completion while keeping the lead
context focused on decisions and evidence.

## Establish the outcome

1. Read `AGENTS.md`, the current worktree, and the relevant source-of-truth
   documents.
2. Restate the request internally as:
   - observable outcome;
   - constraints and invariants;
   - evidence that proves completion;
   - decisions that truly require the product manager.
3. Inspect existing code, tests, history, and configuration before proposing new
   structure.
4. Verify unstable external facts from primary sources. Record durable findings
   in tests, compatibility data, or documentation rather than relying on chat
   memory.

Escalate only the decision classes listed in `AGENTS.md`. Make ordinary
technical decisions autonomously and explain consequential tradeoffs in the
commit or final handoff.

## Plan from evidence

Use the task plan for multi-step work. Each step must produce evidence toward
the full outcome; do not redefine success around an easy subset.

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
4. Keep changes cohesive and preserve unrelated worktree changes.
5. Run fast, targeted checks during iteration.

If implementation exposes a flawed assumption, update the plan and the
authoritative contract rather than hiding the discrepancy in code.

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
3. Audit each acceptance requirement against authoritative evidence.
4. Commit one coherent change with a Conventional Commit subject and a body for
   non-obvious motivation or tradeoffs.
5. Confirm the expected worktree state after the commit.

Do not claim completion from a narrow green check. If evidence is incomplete,
continue working or report the exact missing authority or external condition.

## Improve the system

When the same failure mode appears twice, add a regression test and update the
smallest durable control that would prevent it: `AGENTS.md`, a skill, a hook, a
validator, or CI. Keep workflow changes evidence-based and narrowly scoped.
