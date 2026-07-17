---
name: adversarial-review
description: Review duckdb-fdw diffs, commits, or branches with independent fresh-context reviewers whose job is to find correctness, security, concurrency, lifecycle, and test-oracle defects. Use for explicit review requests and before completing substantive changes involving relational semantics, network policy, credentials, replay, caching, providers, pagination, Rust unsafe code, FFI, async execution, or shutdown. This is review-only unless the user or an active delivery workflow separately authorizes fixes.
---

# Adversarial Review

Assume plausible-looking code is wrong until the diff and executable evidence
show otherwise. Separate authorship, review, and fixing responsibilities.

## Freeze the target

1. Identify the exact review target: working-tree diff, staged diff, commit, or
   branch range.
2. Record the base revision and inspect the complete diff.
3. Read only the contracts needed to evaluate that diff.
4. Do not rely on the implementer's rationale, claimed test result, or desired
   conclusion as evidence.

If the target changes during review, review the new delta before concluding.

## Assign independent perspectives

For a substantive review, use at least two fresh subagent contexts when
available. Give them the raw target and relevant source files, not prior
findings. Reviewers do not edit files.

Choose bounded perspectives from:

- **Relational semantics:** predicate implication, `NULL`, operation selection,
  residual ownership, ordering, limit, projection, and cardinality.
- **Transport and policy:** URL construction, SSRF, redirects, DNS, secrets,
  authentication, retries, replay, caching, rate limits, and resource budgets.
- **Rust lifecycle:** ownership, unsafe and FFI boundaries, async cancellation,
  backpressure, races, reload, shutdown, and error paths.
- **Test oracle:** missing negative cases, assertions that do not prove the
  contract, nondeterminism, skipped coverage, and fixture drift.

Use three perspectives when the diff crosses all of these domains. If fresh
subagents are unavailable, perform separate passes and disclose that reduced
independence.

## Require actionable findings

Each finding must include:

- severity: `P0` critical through `P3` minor;
- tight file and line location;
- the violated invariant or observable failure;
- a concrete counterexample or execution path;
- the smallest credible fix direction; and
- confidence plus any evidence still needed.

Reject findings based only on style preference, hypothetical extensibility, or
requirements absent from the repository. Do not dilute real defects with
general praise or summaries.

## Validate and synthesize

1. Deduplicate findings by root cause.
2. Reproduce or inspect each claim independently where practical.
3. Check whether existing tests actually cover the counterexample.
4. Rank findings by user impact and invariant violation, not edit size.
5. Report no findings when none survive validation; do not invent issues to
   justify the review.

For review-only requests, stop after the evidence-backed report. Do not modify
files, commit, or open external changes.

Within an authorized delivery workflow, hand validated findings to a separate
fixer or the lead agent. After fixes, review the fix delta and rerun the
relevant checks. The original implementer must not be the only reviewer of the
repair.

## Review completion

A review is complete only when the whole target has been inspected, independent
perspectives have returned or their absence is disclosed, findings are tied to
evidence, and changed code has not escaped review through a moving diff.
