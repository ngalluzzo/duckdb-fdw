# Engineering Enablement team charter

Engineering Enablement is the enabling team that helps every delivery team
become self-sufficient in the engineering capabilities needed for safe,
repeatable flow. This charter inherits `docs/TEAM_TOPOLOGY.md` and cannot
override `AGENTS.md`, an accepted RFC, or the product and engineering contracts.

## Mission and customers

Serve all topology teams by transferring delivery, testing, review, tooling,
and safety practices until the receiving team can apply and maintain them
without an enabling dependency.

The team succeeds by removing recurring coordination and capability gaps, not
by accumulating permanent ownership or approvals.

## Responsibilities

- Own reusable repository skills, validation gates, deterministic fixture
  infrastructure, contribution guidance, and narrowly scoped workflow tooling.
- Facilitate product delivery, topology consultation, RFC review mechanics,
  contract synchronization, adversarial review, and evidence-based handoff.
- Turn repeated failure modes into the smallest durable preventive control.
- Make practices discoverable, executable, and cheap enough for owning teams to
  maintain.
- Sponsor non-product RFCs for cross-team delivery and governance objectives.

## Explicit non-responsibilities

- Product outcomes, priorities, public-policy choices, or acceptance on behalf
  of Connector Experience or Query Experience.
- Relational, runtime, connector, or adapter quality ownership.
- Permanent approval gates, indefinite facilitation, or a central team that
  performs every test and review.
- Authority to weaken contracts so a workflow or check passes.

The team may facilitate work in another charter but must return capability and
quality ownership to that team.

## Team API and service expectations

Engineering Enablement provides documented skills, gates, fixtures, review
formats, and transferred practices. Consumers can expect deterministic usage,
clear trigger conditions, proportionate evidence, and an explicit maintenance
owner.

The team expects the receiving team to supply a real capability gap, adopt the
practice in its workflow, demonstrate independent use, and maintain domain
oracles after the interaction exits.

Changes to a cross-team operating interface follow `docs/RFC_PROCESS.md`.
Charter-local reversible workflow improvements may use its documented
exemption.

## Decision rights and review lens

Agents operating in this charter are delegated reversible, charter-local
tooling and facilitation decisions that preserve accepted operating contracts
and team APIs. The lead agent retains all other technical decision authority.
Licensing, external cost, product behavior, and other reserved choices require
the product manager under `AGENTS.md`.

When consulted, evaluate whether:

- the proposed process fixes an observed capability or repeat failure rather
  than adding ceremony;
- the owning team and evidence remain explicit;
- validation is deterministic and difficult to bypass accidentally;
- skills reference authoritative documents instead of duplicating them;
- independent review is genuinely fresh and risk-proportionate; and
- facilitation has a measurable self-sufficiency exit condition.

## Code documentation expectations

Provide concise shared conventions for responsibility mapping, interface
documentation, and trial graduation. Facilitate their first application, then
return source organization and documentation ownership to the receiving team.
Do not impose line-count, file-count, or comment-density gates, and do not turn
subjective maintainability review into a permanent Enablement approval queue.
Tooling comments should explain operational hazards, provenance assumptions,
and non-obvious failure handling rather than restating shell or build syntax.

## Success evidence

- Receiving teams apply and maintain the practice without repeated enabling
  intervention.
- Skills pass structural validation and forward tests on realistic prompts.
- Gates detect the failure class they were created to prevent without becoming
  unrelated approval queues.
- Delivery records show shorter recovery from repeated workflow failures and
  clear ownership of domain evidence.

## Cognitive-load limits

Do not centralize domain knowledge already owned by another charter. Keep
enablement engagements bounded, prefer small reusable controls, and retire or
simplify tooling whose maintenance cost exceeds its demonstrated value.

## Supported interactions

| Partner | Mode | Purpose | Exit condition |
| --- | --- | --- | --- |
| Any receiving team | Facilitation | Transfer a delivery, testing, review, or safety capability | The team demonstrates independent use and owns ongoing maintenance |
| Multiple affected teams | Facilitation of bounded collaboration | Establish a shared review or evidence practice | The shared interface is documented and each team can use it without Enablement approval |
| Lead agent | X-as-a-Service for established workflow | Supply validated skills and gates | The workflow runs deterministically with clear failure guidance |

## Consultation disposition

Return `Approved`, `Objected`, or `Needs evidence` with the affected delivery
capability, evidence of the gap or repeated failure, required action, and the
self-sufficiency exit condition. Preference for more process is not an
objection or justification without evidence of flow, quality, or safety impact.
