# Product delivery

This document defines how a product outcome becomes an agent-led engineering
goal. It is the process entry point for product work; architecture, connector,
and runtime documents remain the sources of truth for durable system behavior.

The product manager describes the value to unlock and the boundaries that
matter. The lead agent determines the technical decomposition, implementation,
tests, review, and Git history. A product brief does not need user stories,
component tasks, or a proposed architecture.

## Operating agreement

| Product manager owns | Lead agent owns | Shared checkpoint |
| --- | --- | --- |
| Target user or author outcome | Technical interpretation and decomposition | Confirm the intended observable behavior |
| Why the outcome matters now | Architecture and implementation choices | Resolve decisions reserved by `AGENTS.md` |
| Product guardrails and priorities | Acceptance evidence, testing, and review | Evaluate the completed demonstration |
| Public-policy and risk decisions | Agent orchestration and repository hygiene | Choose the next valuable outcome |

The lead agent proceeds autonomously within an approved outcome. It asks the
product manager for input only for the decision classes in `AGENTS.md` or when
new evidence would materially change the approved outcome.

## What makes a good goal

A goal describes one coherent, observable capability. It is large enough to
demonstrate meaningful progress and small enough to prove as a single
acceptance narrative.

Useful progress can unlock:

- **User value:** a DuckDB user can complete a new task.
- **Author value:** a connector author can define, validate, or diagnose useful
  behavior more reliably.
- **Decision value:** an uncertain technical boundary is resolved by an
  executable trial that determines whether or how to proceed.
- **Platform value:** a reusable capability directly enables a named user or
  author outcome that follows from it.

An internal component is not a sufficient goal by itself unless it produces
decision evidence or is the smallest coherent path to a named product outcome.
A goal is too broad when it contains independent user behaviors that can be
demonstrated, accepted, or rejected separately.

## Goal lifecycle

1. **Brief.** The product manager supplies the PM brief from the template
   below. Unknown details can remain unknown when they do not change the
   product outcome or a reserved decision.
2. **Shape.** The lead agent uses `$draft-product-goal`, reads the relevant
   contracts, confirms the observable interpretation, identifies the cheapest
   convincing evidence, and completes the agent commitment. It raises only
   material product decisions.
3. **Activate.** The persistent goal explicitly says to follow this document
   and records the approved outcome and completion evidence. For example:

   ```text
   Follow docs/PRODUCT_DELIVERY.md. Pursue: <outcome>.
   Completion requires: <acceptance evidence>.
   Preserve: <goal-specific guardrails, or AGENTS.md and relevant contracts>.
   ```

4. **Deliver.** The lead agent follows the repository delivery loop. When a
   boundary is unproven, the first increment is a thin end-to-end trial. Later
   increments deepen the same outcome rather than creating unrelated partial
   systems.
5. **Checkpoint.** The lead agent interrupts delivery only for a reserved
   product decision, evidence that invalidates the outcome, or an external
   condition that prevents meaningful progress. Routine engineering choices do
   not require approval.
6. **Prove.** Independent review and the relevant repository gates must support
   the acceptance evidence. A passing component test is not a substitute for
   the promised user-visible demonstration.
7. **Hand back.** The lead agent records what changed, the evidence, material
   tradeoffs, and any newly discovered product options. The product manager
   evaluates behavior and value rather than implementation task completion.
8. **Close or continue.** If the evidence satisfies the approved outcome, the
   goal closes. Feedback within that outcome continues the same goal; a
   materially different outcome starts a new brief.

Delivery paths and experiments are working records, not durable product
promises. Stable behavior belongs in the architecture, connector, and runtime
contracts and in executable tests. Do not copy transient delivery
classifications or planned ordering into those contracts.

## Goal brief template

Copy the template into the task when proposing a product goal. The PM brief is
the only portion the product manager is expected to write. The lead agent owns
the remainder and may tighten wording without changing product intent.

```markdown
# Goal: <short outcome name>

## PM brief

### Outcome

For <target user or author>, enable <observable capability> so that <value>.

### Why now

<Why this is the most valuable or uncertainty-reducing outcome to pursue next.>

### Product guardrails

- Must: <public behavior, compatibility, safety, or quality boundary>
- Must not: <explicit exclusion or unacceptable outcome>
- Preserve: <existing behavior or invariant that matters>

Omit this section when the repository contracts and `AGENTS.md` are sufficient.
The lead agent will name those defaults in the persistent goal.

### Success signals

- <What the user or author can demonstrably do when the goal is complete>
- <Any product-level quality, safety, or compatibility signal that matters>

### Reserved product decisions

- <A goal-specific decision within the product-manager classes in `AGENTS.md`>

Omit this section to use the default decision ownership in `AGENTS.md`. Do not
use it to prescribe technical decomposition, architecture, libraries, tests, or
other lead-agent responsibilities.

## Agent commitment

### Observable interpretation

<A concrete acceptance narrative and the visible boundaries of the outcome.>

### Acceptance evidence

- Demonstration: <end-to-end behavior and expected result>
- Automated oracle: <fixture, test, property, or reproducible probe>
- Quality gates: <relevant format, lint, test, safety, or compatibility checks>
- Independent review: <the perspectives required by risk and repository rules>

### Contract and invariant impact

- <Authoritative documents and interfaces affected>
- <Relational, security, lifecycle, or resource invariants that constrain work>

### Unknowns and first trial

- Unknown: <fact that could change the implementation or feasibility>
- Trial: <smallest end-to-end experiment that resolves it>

Write “None identified” when the path is already proven.

### Delivery path

1. <First coherent increment and the evidence it produces>
2. <Next increment within the same outcome, if needed>

This is an agent-owned working plan, not a durable product commitment.

## Completion record

### Delivered

<The behavior now available.>

### Evidence

- <Demonstration, tests, review, and repository gates>

### Material decisions and deviations

- <Decision, rationale, and effect on the approved outcome>

### Product options discovered

- <A valuable follow-on outcome, not an implied commitment>
```
