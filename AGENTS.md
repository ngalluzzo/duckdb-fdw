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
- `docs/TEAM_TOPOLOGY.md` and its linked charters define value streams, team
  accountability, review lenses, and cross-team interaction rules.
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
- Use `$topology-consult` when routing accountability, invoking a team
  perspective, evaluating topology impact, or collecting affected-team input
  for an RFC.
- Use `$delivery-loop` for nontrivial implementation, refactoring, bug-fixing,
  integration, or repository setup.
- Use `$contract-change` whenever behavior crosses architecture, connector
  syntax, compiled IR, planning, execution, validation, or diagnostics.
- Use `$adversarial-review` for substantive code review and before completing
  changes involving relational correctness, network policy, credentials,
  retries, caching, concurrency, FFI, or lifecycle behavior.

`.agents/skills/` is the source of truth for these skills and is what
`scripts/validate-agent-assets.rb` validates against `agents/openai.yaml`.
`.claude/skills/` is a generated mirror so Claude Code can discover the same
skills in this repository; regenerate it with
`python3 scripts/write-claude-skills.py` after adding, removing, or editing a
skill, and do not hand-edit files under `.claude/skills/` directly.

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
2. For a product goal, use `$topology-consult` in Route mode to assign exactly
   one accountable stream-aligned team from `docs/TEAM_TOPOLOGY.md`; record
   supporting teams, interaction modes, and exit conditions in the task plan.
3. Apply the trigger rules in `docs/RFC_PROCESS.md` to any durable shared
   decision; for a product goal, do so after team assignment. Decide a required
   RFC before implementation commits to the affected shared or public contract.
   A bounded evidence trial may precede the decision.
4. Inspect the current worktree and authoritative contracts.
5. Translate the request into observable acceptance evidence.
6. Identify uncertain external facts and verify them from primary sources.
7. Prefer one thin end-to-end trial when an interface or architecture is still
   unproven.
8. Before promoting a trial into product code, map the implementation and test
   responsibilities to the accepted team interfaces, dependency direction, and
   code-documentation obligations. A trial proves feasibility; it is not a
   production source-layout template.
9. Implement the complete behavior without placeholders, silent fallbacks, or
   disabled tests.
10. Run independent review and apply only findings supported by evidence.
11. Audit every interaction exit condition against the final source and test
    dependencies, not merely the presence of a named type or passing end-to-end
    test.
12. Run the narrow checks first, then the complete relevant gate.
13. Review the final diff and commit a coherent change using Conventional
   Commits.

When a failure pattern repeats, or one incident demonstrates a systemic gap
with a concrete recurrence path, improve the smallest durable practice that
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
- A commit offered for cross-worktree integration must be verified from that
  exact committed tree in a clean snapshot. Uncommitted files, later commits,
  or another stream's projected sources invalidate the evidence. When a
  provider interface changes, the snapshot check must build the affected
  committed consumers as well as the provider's focused tests.
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

## Implementation design and code documentation

Team topology defines accountability and durable interfaces, not a directory
per team. Source and test structure must nevertheless preserve those
interfaces as independently understandable responsibilities:

- Map each production module to one primary reason to change. Split connector
  metadata, relational planning, runtime execution, and DuckDB integration when
  they evolve under different contracts or charter responsibilities.
- Keep dependency direction visible. An adapter may consume a provider's team
  API; it must not construct, retain, or reinterpret provider internals merely
  because all code ships in one artifact.
- Avoid catch-all `core`, `common`, or `utils` modules that accumulate unrelated
  responsibilities. Do not replace them with arbitrary fragmentation: justify
  co-location by shared invariants and change ownership, not line count.
- Organize tests along the same responsibility boundaries. Put shared fixtures
  and probes in explicit test support; reserve cross-layer suites for behavior
  that genuinely requires integration.
- Make the durable ownership map discoverable from adjacent source and test
  organization plus enforceable build-target dependencies. Root build source
  lists, commit scopes, and archived delivery plans are not sufficient.
- A focused consumer test target must consume a provider's bounded API or
  fixture service. If it directly lists provider production sources or imports
  provider-private test construction, the interaction remains open; whole-graph
  composition belongs only in an explicitly named integration target.
- Keep experiments under `experiments/` free to optimize for learning. Before
  production promotion, perform the responsibility pass above and remove any
  trial-only coupling that would obscure the intended design.

Code documentation serves maintainers and technically literate product readers.
Document cross-team and lifecycle-sensitive APIs beside their declarations,
including purpose, ownership, inputs and outputs, invariants, lifetime,
concurrency, cancellation and close behavior, error ownership, resource
authority, and compatibility status where applicable. Explain non-obvious
algorithms, ordering constraints, and upstream workarounds beside the code that
depends on them. Do not comment obvious mechanics or optimize for a comment
quota.

## Current verification

Documentation and agent changes must pass:

```sh
ruby scripts/validate-agent-assets.rb
python3 -I -B scripts/verify-public-surface-inventory.py
python3 -I -B test/python/public_surface_inventory_tests.py
python3 -I -B scripts/verify-contract-freeze.py
python3 -I -B test/python/contract_freeze_tests.py
git diff --check
git diff --cached --check
```

For the ordinary source-development loop on the supported product cell, use
the pinned reusable developer environment:

```sh
make build
make test
make demo
```

These commands may reuse `.build/dev` and are not release evidence. Use
`PROFILE=release` to select the release profile or
`DUCKDB_API_DEV_ROOT=/absolute/path` to isolate another developer cell. Run
`make help` for the complete interface and `make paths` for the exact pinned
Python host, loadable artifact, and statically linked test-CLI paths.

Changes to product source, fixtures, build configuration, or release evidence
must additionally pass:

```sh
scripts/verify-source-identities.py
python3 -I -B scripts/test-native-dependencies.py
scripts/run-native-product-tests.sh /absolute/new/build-root debug
```

The first command is the fast content and version identity gate. The second
uses deterministic fake SDK records to prove that native dependency drift
fails closed. The third derives the SDK through `xcrun`, verifies the current
platform libcurl inputs, and performs a fresh pinned DuckDB build, private C++
contract tests, SQLLogicTests, loadable-artifact inventory, and the same
clean-host direct-load contract used by `make demo` on the supported product
cell. Use a new build root on every run. `make verify` is the convenience
wrapper that allocates that new root; it does not reuse developer state.

The authoritative `0.2.0` source-identity, Community Query, and Community
Enablement gates are:

```sh
scripts/verify-source-identities.py
python3 -I -B test/python/community_installation/test_community_installation.py
scripts/test-community-enablement.sh
```

The authoritative `0.1.0` release and sanitizer commands are intentionally
stricter:

```sh
scripts/run-0.1-release-gate.sh /absolute/new/build-root /absolute/new/evidence-root
scripts/run-linux-sanitized-cell.sh "$PWD/.build/linux-amd64-sanitized"
```

The release command requires a clean worktree with `v0.1.0` equal to `HEAD`.
The sanitizer launcher must run on native Linux amd64 and verifies the pinned
container digest defined by `release/0.1.0/pins.json`; a direct inner-script or
emulated run is not release evidence.

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
explicitly rejected with evidence, topology interaction exits are supported by
the actual module and test dependencies, code-level design intent is documented
where required above, and the final diff contains no unrelated or unexplained
changes.
