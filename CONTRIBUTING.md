# Contributing

## Git conventions

- Branch from `main` and keep each commit focused on one coherent change.
- Use Conventional Commit subjects: `<type>(optional-scope): <imperative summary>`.
- Use lower-case types such as `feat`, `fix`, `docs`, `test`, `refactor`,
  `build`, `ci`, and `chore`.
- Keep the subject concise, omit a trailing period, and explain motivation or
  non-obvious tradeoffs in the commit body.
- Mark incompatible public-contract changes with `!` and a `BREAKING CHANGE:`
  footer.
- Do not commit credentials, local databases, generated build output, or
  captured fixtures containing personal data.
- Do not rewrite shared branch history.

Examples:

```text
docs: formalize predicate composition rules
feat(runtime): add bounded cursor pagination
fix(auth)!: reject cross-host token redirects
```

## Native developer workflow

On the exact supported cell documented in the
[README](README.md#quick-start), the repository root is the public
source-development boundary:

```sh
make demo
make test
```

The first command performs an incremental source build as needed and directly
loads the artifact in the pinned clean Python host. The second runs the focused
native, SQL, artifact-inventory, and direct-load oracles. Use `make help` to
discover `bootstrap`, `build`, `paths`, and profile overrides.

Developer state is reusable and therefore is not release evidence. Before
handoff, run `make verify`; it allocates a new root and executes the complete
fresh product cell. The tagged release and sanitizer procedure remains the
[0.1.0 evidence runbook](docs/releases/0.1.0.md).

## Documentation conventions

- Keep architecture, connector syntax, and runtime contracts in their
  respective documents.
- Describe durable invariants and capability requirements; avoid time-bound
  delivery classifications.
- Update examples, validation rules, compiled IR, and tests together when a
  contract changes.
- Treat network access, secrets, resource budgets, redaction, cancellation, and
  replay safety as part of the functional contract.

### READMEs

A README is an entry point for the person using or changing the directory, not
a history of the work that created it.

- Write the root README for a prospective user or first-time contributor. Lead
  with what the product does, its current usable surface, the shortest working
  example, prerequisites, and the supported build and test commands.
- Write a source-package README for a developer about to change that package.
  Map common changes to code, public interfaces, focused tests, and relevant
  invariants. Link to the owning charter; do not restate team-process prose.
- Write an experiment README for someone deciding whether to reproduce, inspect,
  or retire the trial. State its status, question, outcome, one reproduction
  command, and links to results and any detailed runbook.
- Write a release-directory README for the maintainer inspecting those records.
  Explain what the files are and where the exact operational procedure lives.
- Move long exact procedures, evidence formats, and operator failure handling
  to a named `RUNBOOK.md`. Keep durable behavior in the authoritative contract,
  rationale in an RFC, and product history in release notes or the changelog.
- Verify every command and local link. Prefer a concise route to one
  authoritative source over copying material that can drift.

## Code organization

- Give each module one primary reason to change and keep dependencies pointed
  through the team APIs in `docs/TEAM_TOPOLOGY.md`.
- Use production boundaries for independently changing connector, planning,
  runtime, and adapter responsibilities. Team topology does not require folders
  named after teams, but a consumer must not depend on provider internals.
- Mirror production responsibilities in tests. Keep reusable doubles and
  fixtures in explicit test-support modules and integration-only assertions in
  integration suites.
- Keep ownership visible beside the code and in build-target dependencies. A
  root source list or historical plan is an assembly record, not a package
  boundary. Focused consumer tests link provider fixture services instead of
  compiling provider implementations or importing their private constructors.
- Treat a catch-all module or test suite as a design smell, not an automatic
  violation. Split when it combines different invariants, owners, consumers,
  or reasons to change; do not split merely to satisfy a line threshold.

## Code documentation

- Document cross-team interfaces and lifecycle-sensitive abstractions beside
  their declarations: purpose, ownership, invariants, lifetime, concurrency,
  cancellation and close behavior, error ownership, resource authority, and
  compatibility status as applicable.
- Add rationale comments for non-obvious correctness algorithms, safety
  ordering, policy enforcement, and upstream workarounds.
- Prefer names and small cohesive functions for ordinary mechanics. Do not
  restate the code or pursue a comment-density target.
- A technically literate reader should be able to trace an end-to-end path and
  understand why each module boundary exists without reconstructing design
  intent from tests or chat history.
