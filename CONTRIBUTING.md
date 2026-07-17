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

## Documentation conventions

- Keep architecture, connector syntax, and runtime contracts in their
  respective documents.
- Describe durable invariants and capability requirements; avoid time-bound
  delivery classifications.
- Update examples, validation rules, compiled IR, and tests together when a
  contract changes.
- Treat network access, secrets, resource budgets, redaction, cancellation, and
  replay safety as part of the functional contract.
