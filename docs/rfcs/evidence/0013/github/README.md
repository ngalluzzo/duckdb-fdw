# RFC 0013 GitHub package trace

This directory is decision evidence for RFC 0013, not an implemented or
loadable connector package. It fixes the proposed `duckdb_api/v1` spelling and
maps every field required by the permanent four-relation native GitHub catalog.

The semantic digest includes `connector.yaml` and the four manifest-selected
relation files only. `README.md` is deliberately excluded. RFC acceptance does
not promote this directory into product source; the follow-on delivery must
move the accepted package into a Connector-owned product fixture and prove it
through the permanent compiler, planner, runtime, Query registration, and
offline author-fixture paths.

Using `sha256-length-prefixed-path-and-bytes-v1`, the reviewed semantic source
identity is:

```text
sha256.b286e6f7481b437b243dfe2ce017a59d601d909272b9d2b35788fb78753ff23b
```
