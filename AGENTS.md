# Agent Guidance

## Mission and ownership

Build a DuckDB-native relational adapter for well-structured HTTP and GraphQL
APIs. The product manager owns product outcomes, priorities, and public-policy
choices. The lead agent owns technical planning, implementation orchestration,
verification, review, and repository hygiene.

Proceed autonomously on implementation choices that preserve the documented
contracts. Ask the product manager only when a decision would materially alter:

- public SQL or connector-package behavior;
- compatibility promises or explicit exclusions;
- security, privacy, credential, or data-loss risk;
- licensing;
- ongoing external-service cost; or
- a previously approved product outcome.

Do not ask for routine language, library, file-layout, testing, or refactoring
choices when evidence in the repository can resolve them.

## Sources of truth

Read the relevant documents before changing behavior:

- `docs/PRODUCT_DELIVERY.md` defines product intake, goal shaping, acceptance
  evidence, and handoff.
- `docs/TEAM_TOPOLOGY.md` defines value streams, team accountability, and
  cross-team interaction rules.
- `docs/RFC_PROCESS.md` defines when durable shared decisions require an RFC,
  who decides them, and what acceptance produces.
- `docs/ARCHITECTURE.md` defines product and relational invariants.
- `docs/CONNECTOR_SPECIFICATIONS.md` defines author-facing package syntax and
  validation.
- `docs/RUNTIME_CONTRACTS.md` defines compiled IR, planning, execution, and
  lifecycle contracts.
- `CONTRIBUTING.md` defines Git and documentation conventions.

The three system-design documents are one contract at different layers. A
semantic change is not complete until every affected layer, example,
diagnostic, and test agrees. The operating documents govern how that work is
shaped, assigned, and delivered.

## Required skills

- Use `$draft-product-goal` when turning product-manager direction into a new,
  split, clarified, or materially revised product goal before activation.
- Use `$delivery-loop` for nontrivial implementation, refactoring, bug-fixing,
  integration, or repository setup.
- Use `$contract-change` whenever behavior crosses architecture, connector
  syntax, compiled IR, planning, execution, validation, or diagnostics.
- Use `$adversarial-review` for substantive code review and before completing
  changes involving relational correctness, network policy, credentials,
  retries, caching, concurrency, FFI, or lifecycle behavior.

## Engineering invariants

- Ordinary bind and planning are deterministic and perform no network I/O.
- Remote predicate `R` is safe only when the DuckDB predicate `D` implies `R`.
  Exact pushdown additionally requires `R` to imply `D`.
- Every residual predicate has exactly one owner. Limit and offset occur only
  after required filtering and ordering.
- Missing DuckDB capabilities use conservative behavior; unavailable query
  structure is never reconstructed from SQL text.
- Providers preserve base-row cardinality.
- Pagination is sequential unless independence and consistency are proven.
- A retry requires both declared replay safety and an uncommitted replay unit.
- Connector network and resource policies may narrow host policy, never widen
  it. Secrets remain bound to approved authenticators, placements, and hosts.
- Plans and compiled snapshots are immutable during execution. Work is bounded,
  cancelable, and subject to backpressure.
- Numeric and schema conversion is strict and lossless where the declared
  DuckDB type requires it.

Do not weaken an invariant to make an implementation or test easier.

## Delivery workflow

1. For a product goal, use `$draft-product-goal` and follow
   `docs/PRODUCT_DELIVERY.md`; make the persistent goal reference that document
   and state its outcome, evidence, and guardrails.
2. For a product goal, assign exactly one accountable stream-aligned team using
   `docs/TEAM_TOPOLOGY.md`; record supporting teams, interaction modes, and
   exit conditions in the task plan.
3. Apply the trigger rules in `docs/RFC_PROCESS.md` to any durable shared
   decision; for a product goal, do so after team assignment. Decide a required
   RFC before implementation commits to the affected shared or public contract.
   A bounded evidence trial may precede the decision.
4. Inspect the current worktree and authoritative contracts.
5. Translate the request into observable acceptance evidence.
6. Identify uncertain external facts and verify them from primary sources.
7. Prefer one thin end-to-end trial when an interface or architecture is still
   unproven.
8. Implement the complete behavior without placeholders, silent fallbacks, or
   disabled tests.
9. Run independent review and apply only findings supported by evidence.
10. Run the narrow checks first, then the complete relevant gate.
11. Review the final diff and commit a coherent change using Conventional
   Commits.

When a failure pattern repeats, improve the test, skill, hook, or guidance that
allowed it. Do not rely on future agents remembering a conversation.

## Multi-agent rules

Use subagents when independent contexts materially improve speed or quality,
especially for primary-source research, read-heavy exploration, test triage,
and adversarial review.

- Give each subagent a bounded task, evidence target, and output format.
- Keep reviewers independent from the implementer's reasoning.
- Parallel writers require disjoint ownership or separate worktrees.
- The lead agent owns integration and Git history unless a subagent is
  explicitly assigned a branch or worktree.
- Never let multiple agents use stash, reset, or broad cleanup commands to
  coordinate shared state.
- Return distilled findings rather than raw logs to the lead context.

## Implementation quality

- Do not add stubs merely to compile.
- Do not skip, delete, weaken, or broadly rewrite tests to obtain a green run.
- Add regression tests for defects and property tests for semantic laws.
- Prefer deterministic fixtures over live services. Live tests confirm upstream
  compatibility; they are not the primary correctness oracle.
- Keep unsafe Rust and FFI surfaces small, documented by invariants, and covered
  by lifecycle and failure-path tests.
- Treat warnings as actionable. Do not suppress them without a documented
  reason.
- Preserve unrelated user changes and never use destructive Git commands.

## Current verification

Until the code scaffold defines stronger commands, documentation and agent
changes must pass:

```sh
ruby scripts/validate-agent-assets.rb
git diff --check
git diff --cached --check
```

Run the cached-diff check after staging the intended commit so new files are
included in the whitespace gate.

Update this section in the same commit that introduces authoritative build,
format, lint, test, sanitizer, fuzz, or compatibility-matrix commands.

## Git discipline

- Keep `main` releasable and commits focused.
- Use Conventional Commit subjects and explanatory bodies for non-obvious
  decisions.
- Do not rewrite shared history or use `git reset --hard`, `git stash`, or
  destructive checkout commands.
- Do not commit credentials, local databases, generated build output, or test
  fixtures containing personal data.
- Do not choose or infer a project license; licensing is a product-manager
  decision.

## Definition of done

A change is done only when the requested behavior exists, acceptance evidence
covers the full request, affected contracts agree, relevant checks pass,
required RFC decisions and propagation are complete or, for an urgent
containment commit only, the scoped exception required by
`docs/RFC_PROCESS.md` is recorded, adversarial findings are resolved or
explicitly rejected with evidence, and the final diff contains no unrelated or
unexplained changes.
