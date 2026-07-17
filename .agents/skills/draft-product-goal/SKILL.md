---
name: draft-product-goal
description: Draft or revise a duckdb-fdw product goal brief from concise product-manager direction using docs/PRODUCT_DELIVERY.md. Use when proposing, shaping, splitting, clarifying, or materially revising a product outcome before activation, including when a request names desired value but omits user stories, acceptance criteria, or technical decomposition. Produce the PM brief and agent commitment; do not activate a persistent goal or begin delivery unless the user explicitly asks to pursue it.
---

# Draft Product Goal

Turn product direction into a reviewable, verifiable goal without transferring
engineering authorship to the product manager.

## Establish context

1. Read `AGENTS.md` and `docs/PRODUCT_DELIVERY.md` completely.
2. Inspect the relevant architecture, connector, and runtime contracts plus the
   current worktree when they can clarify existing behavior or constraints.
3. Treat repository facts as constraints, not evidence of unstated product
   intent. Do not create a new compatibility, security, or public-behavior
   promise by inference.
4. Keep the pre-activation draft in the task only. After explicit activation,
   put only the approved outcome, acceptance evidence, and guardrails in the
   persistent goal record. Do not add transient goal briefs to durable system
   contracts.

For a drafting-only request, do not edit product code or activate delivery.
Read-only investigation is allowed when it improves the draft.

## Separate ownership

Use the exact template structure in `docs/PRODUCT_DELIVERY.md` and preserve its
ownership boundary:

- The **PM brief** states the target user or author, value, priority,
  product-level guardrails, success signals, and any retained product decision.
- The **agent commitment** supplies the observable interpretation, acceptance
  evidence, affected contracts and invariants, unknowns, first trial, and
  delivery path.

Do not put architecture, libraries, file layout, component tasks, test names,
or agent assignments into the PM brief. Do not ask the product manager to
author technical acceptance criteria.

## Draft the PM brief

1. Express the outcome as: `For <target>, enable <observable capability> so
   that <value>.`
2. Ground **Why now** in priority, pain, opportunity, or uncertainty actually
   stated by the product manager. Do not substitute generic project progress.
   A deadline can express urgency but cannot supply an invented value rationale
   or become acceptance evidence.
3. Include goal-specific guardrails only when the product manager supplied or
   confirmed them. When repository defaults are sufficient, omit Product
   guardrails from the PM brief; carry applicable constraints in Contract and
   invariant impact and in the persistent goal's `Preserve` clause.
4. Write success signals as behavior a user or connector author can observe.
   Tests and internal components belong in the agent commitment. Do not combine
   capabilities that could be demonstrated and accepted independently.
5. Limit reserved decisions to the product-manager decision classes in
   `AGENTS.md`.

Preserve missing material product input as `Decision needed: <question>` rather
than guessing. Ask one concise question only when its answer would materially
change the outcome and no useful draft can be produced without it. Otherwise
return the draft with a short decisions-needed section.

## Complete the agent commitment

### Observable interpretation

Write one concrete acceptance narrative. Name the actor, action, visible
result, and meaningful failure boundary. Keep technical mechanisms out unless
they are themselves public behavior.

### Acceptance evidence

Map every success signal to evidence:

- an end-to-end demonstration with an expected result;
- the cheapest convincing deterministic oracle;
- the repository quality and safety gates that apply; and
- independent review perspectives proportionate to the risk.

Do not redefine success around a convenient component test.

### Contract and invariant impact

Identify authoritative documents and public interfaces that could change.
Carry forward relevant relational, security, resource, conversion, concurrency,
and lifecycle invariants from `AGENTS.md` and the design contracts.

### Unknowns and first trial

List only facts that could change feasibility, architecture, or acceptance.
Resolve agent-owned questions through repository inspection or primary-source
research. For an unproven boundary, specify the smallest end-to-end trial that
can decide the issue without pretending to deliver the whole outcome.

### Delivery path

Choose coherent increments within the same outcome. Every increment must
produce user, author, decision, or directly enabling platform value and the
evidence for it. Do not turn the section into a component inventory, durable
product ordering, or a time estimate.

## Check goal quality

Revise the draft until all statements are true:

- One acceptance narrative can demonstrate the outcome.
- The goal is not merely an internal component unless it resolves a named
  uncertainty or directly enables a named user or author outcome.
- Independent behaviors that can be accepted separately are not bundled. In
  particular, protocol support, authentication, pagination, retries, caching,
  and distinct relational operations do not belong in one goal merely because
  a broad request named all of them.
- The PM brief contains no routine engineering choices.
- Success signals describe value; acceptance evidence proves each signal.
- No unresolved agent-owned question is sent to the product manager.
- No material product decision has been invented or hidden as an assumption.
- Transient delivery classifications and speculative dates are absent.

If the proposed goal is too broad, recommend one thin user-visible path only
when product priority or repository evidence makes it the necessary first
choice. Exclude every independently demonstrable capability from that brief
and list the other outcomes as options, not commitments. If the first choice is
a product-priority decision, leave it unresolved instead of choosing by
technical convenience. If the proposal is too narrow, connect it to the
observable capability or decision it unlocks.

## Present and activate

Return the completed **PM brief** and **agent commitment** in the template from
`docs/PRODUCT_DELIVERY.md`. Follow them with only one of:

- `Ready to activate` when no product decision remains; or
- `Decisions needed before activation` with the smallest set of material
  questions.

Treat any wording that materially narrows, expands, or otherwise changes the
target, value, public behavior, compatibility, guardrails, or priority as a
blocking product decision. Show the revised wording and require explicit
product-manager approval before marking the draft ready or activating it, even
when the original request also said to start or pursue the goal. Do not
silently approve the PM-owned portion on their behalf.

If the user asked only for a draft, stop after presenting it. If the user
explicitly asked to start, pursue, or activate the goal and neither an
unresolved product decision nor an unapproved intent-changing rewrite remains,
create the persistent goal using the syntax in
`docs/PRODUCT_DELIVERY.md`, then use `$delivery-loop`. Never activate from an
ambiguous expression of interest.
