# Live REST product proof results

Status: **Passed on the recorded target**

This experiment proves the central HTTP-to-relation mechanism through a real
loadable DuckDB extension. It is decision evidence, not an accepted public SQL,
transport, dependency, compatibility, connector-authoring, or distribution
contract.

## Product result

The source-built `live_rest_product_proof` extension loaded into DuckDB 1.5.4
and executed `duckdb_api_live_rest_proof()` as an ordinary table function. Bind
produced a fixed immutable plan without network I/O. Scan execution performed
one unauthenticated HTTPS GET to the public GitHub API, strictly decoded the
response, and returned three rows with DuckDB logical types `BIGINT`, `VARCHAR`,
and `BOOLEAN`.

The result establishes that a native DuckDB extension can carry this project's
core mechanism through bind, planning, bounded HTTP execution, strict JSON
conversion, pull batches, and typed DuckDB output. Connector YAML and package
distribution are not prerequisites for that mechanism.

## Reproduction

From the repository root on the recorded target:

```sh
experiments/live-rest-product-proof/scripts/run-live-rest-product-proof.sh --real
```

The runner starts from a fresh build directory, overlays the experiment onto a
pinned official DuckDB extension template, builds both static and loadable
targets, runs focused native tests, loads the artifact directly in the pinned
Python DuckDB host, exercises a controlled loopback HTTP oracle, and finally
runs the opt-in public-service compatibility query.

## Recorded target

| Dimension | Observed value |
| --- | --- |
| Platform | `osx_arm64` |
| DuckDB | `1.5.4`, commit `08e34c447bae34eaee3723cac61f2878b6bdf787` |
| Native extension template | `cfaf3e236008e782d27f4341b0ee036002d0a449` |
| Extension CI tools | `b777c70d30942cca5bef62d6d4fa23a13362f398` |
| Compiler | Apple clang 17.0.0 (`clang-1700.0.13.5`) |
| CMake | 4.1.2 |
| Ninja | 1.13.0 |
| Python DuckDB host | `duckdb==1.5.4` |
| Configured libcurl | macOS SDK headers and stub, version 8.7.1 |
| Runtime libcurl | 8.7.1, queried from the linked implementation |
| Artifact linkage | `/usr/lib/libcurl.4.dylib` |
| Extension version | `0.0.0-live-rest-trial` |
| Installation mode | Direct unsigned load of a local experimental artifact |

The runner records the CMake-selected libcurl library, matching include and
configured-version evidence, the loadable artifact's actual dynamic linkage,
and `curl_version_info()` from a probe with the same linkage. That dependency
choice is proven only for this target and remains a production RFC decision.

## Deterministic evidence

The controlled success oracle proved:

- extension load, `DESCRIBE`, and prepared-statement bind performed zero HTTP
  requests;
- each scan made exactly one request with only the fixed host, accept,
  user-agent, and API-version headers;
- lowercase and uppercase ambient proxy variables could not reroute the
  request;
- the three boundary-oriented fixture rows arrived with exact values and
  DuckDB logical types; and
- a prepared scan retained the immutable authority bound before an invalid
  environment change.

The controlled failure and lifecycle oracle proved one bounded attempt and
redacted diagnostics for a non-success status, malformed JSON, oversized
response, forbidden redirect, and peer disconnect. It also proved the
five-second wall budget, sub-second cooperative DuckDB interruption,
peer-socket abort, and post-failure recovery. On this DuckDB Python host,
closing a connection with an active scan does not initiate cancellation; it
waits safely for the five-second query deadline, after which the stream and
peer terminate. Prompt close-driven cancellation therefore remains unproven.

Focused native tests separately cover plan snapshots and authority rejection,
strict JSON schema and resource conversion, one-attempt batch-stream behavior,
cancellation and idempotent close, and post-DNS rejection of loopback, private,
link-local, carrier-grade NAT, documentation, benchmark, transition,
multicast, and other special-purpose IPv4 and IPv6 destinations.

## Material findings

- DuckDB 1.5.4's built-in native HTTP client is HTTP-only, so it could prove
  the controlled loopback path but not the required public HTTPS path.
- The recorded cell's system libcurl completed the HTTPS proof while allowing
  explicit proxy disablement, TLS verification, response/time limits,
  cancellation callbacks, HTTP/1.1 pinning, and a pre-socket resolved-address
  policy.
- HTTP/1.1 is pinned because libcurl can internally replay some refused HTTP/2
  streams even when the project runtime invokes the transport only once.
- DuckDB table-function bind data must implement `Copy()` for optimizer and
  prepared-statement paths; the prepared-plan oracle now guards that host
  lifecycle requirement.

## Limits and handoff

- Only DuckDB 1.5.4 on `osx_arm64` is proven.
- The public GitHub result demonstrates current upstream compatibility; its
  changing rows are not a correctness oracle.
- The connector, endpoint, schema, function name, libcurl dependency, C++
  interfaces, and direct-load workflow are trial choices, not compatibility
  promises.
- Authentication, pagination, retries, caching, pushdown, GraphQL, declarative
  compilation, connector packages, and public distribution remain unproven.
- Production promotion requires the RFC and ownership pass for durable SQL,
  network, runtime, dependency, and team boundaries. The experiment's topology
  interactions therefore remain open until product modules replace the trial.

Decision-ready conclusion: continue the product through a native live REST
relation. Do not put declarative connector compilation or distribution back on
the critical path until the query, security, traversal, relational, and
workflow mechanisms have earned a public authoring surface.
