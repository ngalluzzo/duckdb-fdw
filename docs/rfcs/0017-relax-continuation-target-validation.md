# RFC 0017: Relax continuation-target validation to the page-progression boundary

```yaml
rfc: "0017"
title: "Relax continuation-target validation to the page-progression boundary"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Connector Experience"
technical_decision_owner: "Lead agent"
product_approver: "Nic Galluzzo"
authors:
  - "Lead agent"
required_reviewers:
  - "Connector Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
affected_teams:
  - "Connector Experience"
  - "Remote Runtime"
  - "Relational Semantics"
  - "Engineering Enablement"
linked_outcome_or_objective: "docs/goals/body-signaled-rest-pagination.md (0.10.0): the rickandmorty adoption trial surfaced that the existing query-multiset validation rejects most real-world APIs."
supersedes: "Not applicable — refines RFC 0007's and RFC 0016's validation rule without changing the reconstruct-and-verify trust model"
```

## Summary

`ValidateNextTarget` currently requires the continuation URL to contain
**every** declared query parameter (a full query-multiset comparison). This was
modeled on GitHub's shape (`per_page=100&page=2`) and rejects the majority of
real-world REST APIs, whose `next` URLs contain only the changing parameter
(e.g. `?page=2` without `page_size`). This RFC relaxes the check to verify only
the security-relevant invariants: exact origin, exact path, and correct
page-number progression. Non-page-number query parameters are no longer
required to appear in the continuation URL, because Runtime reconstructs the
actual request locally from the admitted profile — the continuation URL is a
verified signal, never a dereferenced fetch target.

## Problem

When `connectors/rickandmorty` attempted to adopt `response_next` (RFC 0016),
the API's `info.next` value — `https://rickandmortyapi.com/api/character?page=2`
— was rejected by `ValidateNextTarget` because it does not contain a
`page_size` parameter. The API has a server-fixed page size; it does not accept
or echo a `page_size` query parameter. The same rejection occurs for any API
whose `next` URL omits query parameters the client originally sent.

This is not a rickandmorty-specific issue. The majority of paginated REST APIs
return continuation URLs that include only the page-advancing parameter, not
the full original query string. The current design does not scale beyond APIs
that happen to echo every parameter back.

The root cause is that RFC 0007 (which established Link pagination) and RFC
0016 (which extended it to `response_next`) both assumed the continuation URL
must match the full reconstructed request. In practice, the security boundary
is narrower: Runtime must not be redirected off the declared origin and path,
and the page progression must be correct. The full query multiset is not a
security invariant — Runtime uses its own declared parameters for the actual
request, not the continuation URL's parameters.

## Decision

**Accepted (product-manager direction, 2026-07-22):** relax
`ValidateNextTarget` and make `page_size_parameter`/`page_size` optional for
both `link_next` and `response_next`. The continuation URL need only match:

1. the exact operation origin (scheme + host + port);
2. the exact operation path; and
3. the page-number parameter's expected next value.

Non-page-number query parameters in the continuation URL are ignored — they
are neither required nor validated. Runtime continues to reconstruct the
actual request from the admitted profile's declared parameters; the
continuation URL is still a verified signal, never a fetch target.

## What does not change

- **Reconstruct-and-verify trust model.** Runtime still reconstructs every
  request locally and advances only the page number. The continuation URL is
  still verified, never dereferenced.
- **Exact-origin and exact-path enforcement.** A continuation URL at a
  different origin or path is still rejected. A malicious body or header
  cannot redirect the scan off the declared operation.
- **Replay rejection.** The `seen_pages` check is unchanged.
- **Sequential, mutable, bounded pagination.** All other invariants
  (`max_pages_per_scan`, resource ceilings, cancellation, etc.) are unchanged.

## Contract propagation

| Source of truth or artifact | Impact | Completion evidence |
| --- | --- | --- |
| `src/runtime/pagination/link_pagination.cpp` `ValidateNextTarget` | Drops the query-multiset comparison; keeps origin, path, and page-number checks | This RFC's implementation |
| `src/connector/package/assets/connector-package-v1.schema.json` + `.inc` | `page_size_parameter` and `page_size` move from required to optional in `linkPagination` and `responsePagination` | This RFC's implementation |
| `src/connector/package/package_rest_schema.cpp` | `page_size_parameter`/`page_size` no longer required for `link_next`/`response_next`; still validated if present | This RFC's implementation |
| `src/connector/package/package_operation_compiler.cpp` | `found_page_size` no longer required; only `found_page_number` is mandatory | This RFC's implementation |
| `src/runtime/execution/rest_pagination_admission.cpp` | `HasPageBindings` requires only the page-number binding, not page-size | This RFC's implementation |
| `test/cpp/runtime/pagination/link_pagination_tests.cpp` | Existing query-multiset rejection tests updated; new tests prove URLs with fewer parameters are accepted | This RFC's implementation |
| `connectors/rickandmorty/relations/character_search.yaml` | Adopts `response_next` with only `page` (no `page_size`) | This RFC's implementation |
| `docs/CONNECTOR_SPECIFICATIONS.md` | REST pagination section updated to reflect optional page_size | This RFC's implementation |

## Compatibility

Strictly additive relaxation. Existing packages that declare both
`page_size` and `page_number` continue to work — the validator no longer
checks the page_size parameter in the continuation URL, but the request still
includes it (Runtime uses the declared parameters, not the URL's). No existing
accepted package changes behavior.
