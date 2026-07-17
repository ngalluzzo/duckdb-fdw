---
name: contract-change
description: Keep duckdb-fdw semantic contracts synchronized across architecture, connector YAML, schema validation, compiled Rust IR, planning, execution, diagnostics, examples, and tests. Use when adding or changing connector fields, SQL behavior, pushdown rules, DuckDB capability handling, providers, pagination, retries, caching, security policy, resource budgets, schemas, FFI, or lifecycle behavior. Do not use for an internal refactor that demonstrably preserves all observable semantics.
---

# Contract Change

Treat architecture, author-facing syntax, runtime types, and executable evidence
as one contract expressed at different layers.

## Establish the semantic delta

1. Read the relevant sections of:
   - `docs/ARCHITECTURE.md` for product and relational invariants;
   - `docs/CONNECTOR_SPECIFICATIONS.md` for package syntax and validation;
   - `docs/RUNTIME_CONTRACTS.md` for compiled IR and runtime behavior.
2. State the old and new observable behavior in one precise sentence each.
3. Identify compatibility, security, and failure-mode consequences.
4. Ask the product manager only if the change crosses an escalation boundary in
   `AGENTS.md`.

Do not start by editing the easiest layer. Determine the whole propagation path
first.

## Build the traceability set

For the semantic delta, mark every applicable layer and its completion evidence:

| Layer | Required evidence |
| --- | --- |
| Architecture | Invariant or decision agrees with the behavior. |
| Connector syntax | Field shape, defaults, examples, and status are explicit. |
| Schema and validation | Invalid or unsafe forms are rejected deterministically. |
| Compiled IR | Runtime types preserve every semantic distinction. |
| Planning | Selection, ownership, and capability behavior are deterministic. |
| Execution | Ordering, bounds, cancellation, and failure behavior are explicit. |
| Diagnostics | Errors and plan explanations reveal the applied decision safely. |
| Tests and fixtures | Positive, negative, boundary, and regression cases execute. |

An unaffected layer needs a reason, not an edit. An affected layer needs direct
evidence, not an intention to update it later.

## Apply correctness gates

Check the gates relevant to the change:

- A pushed remote predicate never loses a DuckDB-true row; exactness is
  bidirectional equivalence.
- `AND`, `OR`, `NOT`, `IN`, and `NULL` retain DuckDB three-valued semantics.
- Every residual has exactly one owner, and limit or offset occurs only after
  required filtering and ordering.
- An unavailable DuckDB capability changes optimization, never correctness.
- Providers produce exactly one row state per input row or fail.
- Pagination remains sequential unless independence and consistency are proven.
- Retry and cache use require replay safety; retries stop after commitment.
- Connector policy only narrows host network, proxy, secret, and resource
  capabilities.
- Ordinary bind and planning remain network-free.
- Plans and schema snapshots remain immutable for active scans.
- Conversion is strict and lossless where required by the DuckDB type.

Do not describe a correctness requirement only in prose when a property,
fixture, type, or validator can enforce it.

## Keep the specification durable

- Describe invariants and capability requirements, not delivery stages or
  time-bound promises.
- Use `duckdb_api/draft` until compatibility publication is an explicit product
  decision.
- Reject unknown fields and ambiguous behavior rather than guessing intent.
- Keep examples valid under the same validation rules as real packages.
- Do not claim support for an upstream API or DuckDB surface without primary
  evidence and a compatibility test.

## Verify propagation

1. Search for the old term, field, enum variant, and example form.
2. Compare corresponding spec and IR field names and defaults.
3. Check Markdown links, headings, code fences, and `git diff --check` for
   documentation changes.
4. Run schema, compiler, planner, fixture, property, and integration tests that
   cover every affected layer once those harnesses exist.
5. Inspect diagnostics for correctness and secret-safe redaction.
6. Use `$adversarial-review` for changes that affect correctness or policy.

Complete the change only when the traceability set contains authoritative
evidence for every affected layer.
