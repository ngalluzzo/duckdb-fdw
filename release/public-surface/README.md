# Public surface inventory

This directory is the machine-readable source of truth for project-owned SQL
table functions. It records the shipped `0.7.0` baseline, the accepted
`0.8.0` candidate, and the planned `0.9.0` dispatcher removal without treating
a removed identifier as part of the current surface.

Query Experience owns SQL entries, content-addressed shapes, and the independent
exact-surface contract in `query-contract.json`. Engineering Enablement owns
only the reusable schema and verifier. A Query contributor can add or revise an
entry without Enablement approval by:

1. adding a shape whose identifier is `sha256.<canonical-shape-digest>`;
2. adding a release revision with the classification required by the verifier;
3. updating the exact active and removed identifier sets in `release_views`;
4. updating Query's independent RFC-to-identity and transition contract;
5. citing an RFC that resolves to an accepted repository decision; and
6. running:

   ```sh
   python3 -I -B scripts/verify-public-surface-inventory.py
   python3 -I -B test/python/public_surface_inventory_tests.py
   ```

The verifier checks the schema, canonical SQL identities, case-insensitive
uniqueness, content-addressed shapes, release ordering, lifecycle
classifications, Query's exact identity and transition set, accepted RFC
resolution, release-view completeness, and the checked-in `0.7.0` public
contract. Mutation tests prove that coordinated omissions and extras,
recomputed shape edits, nonexistent or unaccepted decisions, unknown or false
classifications, stale shape references, and current/removed confusion fail
closed.

During RFC review only, reviewers may evaluate a candidate before acceptance
with an explicit scoped exception:

```sh
python3 -I -B scripts/verify-public-surface-inventory.py --review-rfc "RFC 0012"
```

The normal repository gate never supplies this exception; it admits only
accepted decisions.

The inventory is a compatibility oracle, not registration code. Delivery must
also compare the materialized release view with the actual loadable artifact
and behavior tests before a candidate ships.
