# 0.2.0 Community enablement records

This directory preserves the non-secret authority and proposal inputs used to
evaluate the `0.2.0` DuckDB Community extension path. It is historical release
evidence, not an installation guide, release candidate, signed artifact, or
current support claim.

## Contents

| File | Purpose |
| --- | --- |
| `pins.json` | Exact upstream refs and dependency-expectation identity for the candidate cycle |
| `dependencies.json` | Inputs admitted by the bounded dependency audit |
| `descriptor.json` | The deliberately non-authoritative descriptor expectation |
| `description.yml` | The proposed DuckDB Community descriptor |
| `descriptor-cycle.json` | Reviewed authority tying the proposal to exact source and evidence identities |
| `build-authorities.json` | Independent authority registry for collected Community build records |

The deterministic provider suite is:

```sh
bash scripts/test-community-enablement.sh
```

Use [RUNBOOK.md](RUNBOOK.md) for the implemented candidate-admission,
descriptor-verification, build-record, and native signature-transition
procedures and trust boundaries. Signed deployment-evidence collection remains
future work; this directory contains no command that produces it. The
product-facing supply-chain interpretation is in
[`docs/releases/0.2.0-supply-chain.md`](../../../docs/releases/0.2.0-supply-chain.md).
