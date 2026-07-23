# RFC 0023: Add durable credential providers and scan-snapshot rotation

```yaml
rfc: "0023"
title: "Add durable credential providers and scan-snapshot rotation"
status: "Accepted"
rfc_type: "Product"
sponsor_team: "Query Experience"
technical_decision_owner: "Lead agent"
product_approver: "Nic Galluzzo"
authors:
  - "Lead agent"
required_reviewers:
  - "Query Experience"
  - "Remote Runtime"
  - "Engineering Enablement"
affected_teams:
  - "Query Experience"
  - "Remote Runtime"
  - "Engineering Enablement"
linked_outcome_or_objective: "Production-resilience credential-provider goal (brief under docs/PRODUCT_DELIVERY.md, pre-activation): operators can configure, rotate, replace, and remove temporary, environment-backed, and persistent credentials while each scan retains one explicit authority snapshot."
supersedes: "none"
```

## Summary

Extend the existing explicitly named `duckdb_api` DuckDB secret boundary with
two credential sources, one project-owned bounded persistent DuckDB storage,
and one provider-neutral execution contract. The `config` provider accepts
either temporary memory or that explicit persistent storage; a new
`environment` provider stores one exact environment-variable reference and
reads its value only when a scan starts.
Successful resolution yields one move-only credential snapshot containing the
opaque authorization value, a stable non-secret authority identity, and one
revision identity. Runtime retains that complete snapshot for the scan, so
replacement, deletion, or environment rotation affects only a later scan.
Provider failures are classified as `credential_provider` and remain distinct
from remote `401`/`403` authorization failures.

## Sponsorship and context

- **RFC type:** Product. This changes public DuckDB secret configuration,
  credential/privacy behavior, the Query-to-Runtime authorization interface,
  and scan lifecycle semantics.
- **Sponsoring team:** Query Experience. The acceptance narrative ends with a
  DuckDB operator configuring credentials and observing prepared, concurrent,
  rotated, and failed scans through ordinary SQL and redacted diagnostics.
- **Linked outcome or objective:** The production-resilience
  credential-provider goal supplied by the product manager. Query Experience
  remains accountable; Remote Runtime supplies the provider-neutral snapshot
  service boundary and scan ownership semantics.
- **Why now:** Production deployments need credentials that survive a process
  restart or can be rotated outside SQL. Accepted RFC 0021 now supplies the
  failure identity, attempt identity, aggregate budgets, and diagnostic
  structure that provider failures and later authority-scoped resilience
  mechanisms require.

Accepted RFC 0006 deliberately narrowed the first authenticated relation to an
explicit temporary `duckdb_api/config` secret and left environment and
persistent sources outside that outcome. It already established execution-time
exact-name resolution and one authorization object per scan. This RFC extends
that boundary without changing connector authentication declarations or remote
credential placement.

## Problem

The installed extension registers only:

```sql
CREATE TEMPORARY SECRET example (
    TYPE duckdb_api,
    PROVIDER config,
    TOKEN '...'
);
```

`secret_integration.cpp` rejects every persistent storage and every provider
except `config`, and resolution queries only DuckDB's temporary `memory`
backend. A process restart therefore loses all supported credential
configuration, and an operator cannot name an environment-backed source whose
value is read at execution.

The existing runtime behavior already owns one moved `ScanAuthorization` for a
complete REST or GraphQL stream, so token bytes cannot change between pages.
However, that behavior has no explicit authority or revision identity, no
provider-neutral resolution service, and no oracle proving replacement or
deletion while a multi-page scan is active. It is therefore unsuitable as the
identity boundary for later rate-limit, retry, cache, or accounting work.

Provider lookup failures are also constructed as unclassified
`ExecutionError`s. RFC 0021 reserves the `credential_provider` primary class,
but Query appends the structured class only when a failure carries explicit
properties. A missing or malformed secret therefore remains visibly different
from neither the provider taxonomy nor its acceptance narrative, while a
remote `401`/`403` is classified inside Runtime as `authorization`.

## Decision drivers and invariants

- **Must preserve:** bind, `DESCRIBE`, `EXPLAIN`, and `PREPARE` resolve no
  credential, inspect no environment, and perform no network I/O.
- **Must preserve:** immutable requests and plans retain only the explicitly
  selected logical DuckDB secret name; no provider, storage, revision,
  principal, or secret material enters package source, plan explanation,
  diagnostics, fixtures, caches, or catalog introspection.
- **Must preserve:** connector-declared authenticator, placement, destination,
  and host policy remain the sole request authority. A credential source never
  widens any of them.
- **Must preserve:** anonymous relations create no provider request and retain
  no credential identity.
- **Must enable:** explicitly configured temporary-memory, environment-backed,
  and bounded DuckDB persistent-storage credentials with deterministic
  creation, replacement, deletion, resolution, and failure behavior.
- **Must enable:** one stable non-secret authority identity and one revision
  identity on every resolved credential snapshot; concurrent scans share no
  mutable credential state.
- **Must enable:** one scan uses one moved snapshot for all pages and attempts;
  a new scan resolves again and may observe replacement, deletion, or a changed
  environment value.
- **Must enable:** fixed, redacted `credential_provider` failures distinct from
  remote `authorization` failures, with cancellation remaining DuckDB
  interruption.
- **Must not introduce:** implicit or ambient provider selection, caller-
  selected authority, OAuth, refresh, interactive login, executable providers,
  external secret-system plugins, retries, rate-limit waiting, caching, or
  result sharing.
- **Must not claim:** encryption of DuckDB's persistent secret files, secure
  zeroization, hostile-process isolation, or safe concurrent mutation of the
  process environment.

## Proposed decision

### Public behavior

The existing `duckdb_api` secret type retains `config` as its default provider.
The provider surface is closed at two providers:

1. **`config`** accepts exactly `TOKEN VARCHAR`.
   - `CREATE TEMPORARY SECRET ...` stores the redacted token in DuckDB's
     built-in `memory` storage.
   - `CREATE PERSISTENT SECRET ... IN duckdb_api` stores the redacted token in
     the project-owned bounded `duckdb_api` storage rooted beneath DuckDB's
     host-owned `secret_directory` setting.
   - Alternate temporary or persistent storage implementations are rejected;
     supporting one later requires a new compatibility and lifecycle decision.
2. **`environment`** accepts exactly `VARIABLE VARCHAR`.
   - The value must be one non-empty portable environment identifier matching
     `[A-Za-z_][A-Za-z0-9_]*`, bounded to 256 bytes.
   - Creation stores only that exact variable reference plus opaque internal
     identity metadata and does not inspect the environment.
   - The provider may be temporary or use explicit `IN duckdb_api`
     persistence; persistence stores the reference and identity metadata,
     never the environment value.
   - Resolution reads exactly that variable once at scan initialization. There
     is no prefix, fallback name, expansion, provider auto-detection, or search.

Both providers require explicit SQL creation and an authenticated relation
continues to require the existing `secret := '<logical-name>'` argument. The
query caller chooses only that logical name. Provider, storage, authenticator,
placement, destination, authority identity, and revision are derived from
validated project and DuckDB state, never supplied as table-function arguments.

Exact-name resolution searches only supported `duckdb_api` entries in the
admitted built-in `memory` and project-owned `duckdb_api` storages. It does not
use Secret Manager's all-storage lookup. DuckDB's generic `local_file` storage
is never queried by a scan. A same-named `http` or other foreign secret type is
outside this authority domain and is ignored; it can neither supply credentials
nor block or redirect a supported credential. No precedence rule exists among
supported authorities: if a supported `duckdb_api` entry with the same
case-insensitive name is present in both admitted storages, resolution fails as
an ambiguous provider state rather than silently selecting one. Replacement is
storage-local. Moving an identity between storages requires dropping the old
entry before creating the new one.

`CREATE OR REPLACE` in the same storage retains the existing opaque authority
identity and installs a new provider revision. `DROP` destroys that authority;
re-creating the same logical name after deletion creates a new authority
identity. A `config` revision is the creation/replacement revision stored with
the entry and survives persistent restart. An `environment` resolution uses a
fresh conservative revision identity on every successful scan initialization,
because a process environment exposes no trustworthy version. This may
distinguish two equal successive values, but it never conflates an unobservable
rotation.

The persistent storage uses a project-owned versioned index plus one bounded
record per logical secret. The index contains only logical/provider and opaque
identity metadata plus an opaque random record filename; credential bytes or
environment-variable names exist only in the exact record. Creation and
resolution cap the storage at 256 records, each record at 16 KiB, and the
index at 128 KiB. Startup reads and validates only the index; exact resolution
reads at most one record. Unknown files are ignored and cannot become
credential authority.

The storage is a fixed `duckdb_api` child of DuckDB's expanded
`secret_directory`, not a caller-selected path. On the supported POSIX cell it
retains a directory descriptor obtained by component-wise `openat` traversal:
every existing component must be an owned directory and no component may be a
symbolic link; missing descendants are created with owner-only permissions.
The project child must be owned by the effective user and have no group/other
permissions. A private regular lock file is held exclusively for the
DatabaseInstance lifetime, so a second live instance using the same project
directory fails closed. Index, record, lock, and temporary files are created
with exclusive owner-only mode; every opened file is checked through its file
descriptor for expected ownership, regular-file type, and no group/other
permissions. Record names are generated internally, contain no logical name,
and every read, rename, and unlink is relative to the retained directory
descriptor. `O_NOFOLLOW`, `O_EXCL`, `renameat`, descriptor validation, file and
directory synchronization, and fixed descendant names prevent link/special-
file admission and path replacement from redirecting credential I/O. Storage
failures expose only a fixed redacted category, never a path, logical name,
environment-variable name, file metadata, or host exception text.

Persistent mutation is explicit, immediate, and autocommit-only. Store and
drop inspect the supplied `CatalogTransaction`'s `ClientContext` and reject an
explicit transaction before any file or catalog mutation. Replacement writes
a new uniquely named record, flushes it, atomically replaces the bounded index,
then removes the superseded record; deletion atomically removes the index entry
before deleting the now-unreachable record. A failure before index replacement
leaves the old authority selected; a failure after replacement leaves at worst
an unreachable old record. Temporary files and unreachable records are never
selected and are cleaned on a later successful mutation. The exclusive
directory lock makes live cross-process or cross-DatabaseInstance mutation a
deterministic provider failure rather than a supported rotation protocol.

The canonical removal statement is:

```sql
DROP PERSISTENT SECRET credential_name FROM duckdb_api;
```

Pinned DuckDB also routes `DROP PERSISTENT SECRET credential_name` or
`DROP SECRET credential_name` to this storage when exactly one eligible
storage contains the name. That standard unique-match shorthand is supported;
multiple matches fail before mutation and never select an authority by
precedence. The storage API cannot distinguish qualified from unique-match
unqualified drop, so this RFC records both forms instead of promising an
unenforceable qualification rule. `DROP TEMPORARY SECRET` retains DuckDB's
existing memory behavior.

The files are not encrypted. User documentation must state that limitation and
recommend environment-backed or temporary credentials when plaintext-at-rest
storage is unacceptable. Safe inventory remains redacted; privileged process
owners and DuckDB's explicitly unredacted secret access are outside the
protection claim.

### Shared interfaces

Remote Runtime provides one DuckDB-free two-phase internal team API:

```text
ScanExecutor::OpenWithCredentialProvider(plan, provider, cancellation)
    1. completely admit the immutable plan without provider observation
    2. CredentialProvider::Resolve(logical reference, cancellation)
         -> CredentialSnapshot(
                opaque authorization,
                opaque authority identity,
                opaque revision identity)
    3. open one stream from the admitted profile and moved snapshot
```

The public entry point enforces the ordering rather than relying on Query to
call two methods correctly. Concrete Runtime admission validates the complete
plan/profile intersection before invoking the call-scoped provider. A rejected
plan therefore observes no catalog, persistent record, environment variable,
or credential value. Direct pre-built `ScanAuthorization` entry points remain
bounded compatibility/test services, but the installed Query path always uses
the provider entry point.

Provider resolution is synchronous for the supported local sources, bounded by
the record/index and credential byte ceilings above, cancellation-aware before
and after every host/provider read, and call-scoped. It retains no plan,
DuckDB object, environment variable name, provider-specific record, or
plaintext after returning. Implementations may create a snapshot only through
the provider contract; consumers can compare or hash the opaque identities for
isolation/accounting but cannot serialize or render them.

Query's DuckDB adapter implements the provider interface for the two admitted
DuckDB storages and passes one call-scoped provider, bound to the current
`ClientContext`, into `ScanExecutor` during global scan initialization. Runtime
asks it to resolve the plan's logical reference only after admission and moves
the complete result into the stream. Query does not switch on `config` versus
`environment` outside the DuckDB provider module and never learns the
connector's bearer/api-key placement.

The adapter retains a DatabaseInstance-scoped handle to the project storage
state when it registers that storage. Persistent exact-name resolution calls
that bounded state directly and never asks `SecretManager` to initialize or
enumerate storages. A monotonic `memory_may_contain_duckdb_api` bit is set by
either `duckdb_api` create provider before any attempted temporary-memory
store. If false, no project temporary secret can exist in that instance and
the scan skips memory lookup. If true, the creating DDL has already forced
DuckDB's one-time Secret Manager initialization, so an exact
`GetSecretByName(..., "memory")` lookup cannot cold-initialize or enumerate
`local_file`. This deliberately conservative bit may cause an empty exact
memory lookup after a failed create or later drop; it can never suppress a
  supported temporary entry created through the public project provider. Each
  provider returns the module-private `DuckdbApiSecret` concrete type whose
  `Clone` preserves its provider-owned identity metadata through DuckDB catalog
  transaction versions. The resolver admits the exact memory result only when
  it is that concrete project type, declares `duckdb_api`, and names one of
  this RFC's closed providers; a same-named foreign or generic `KeyValueSecret`
  is ignored. It compares that supported result with the direct project result
  and rejects ambiguity.

DuckDB's public C++ `RegisterSecret` can bypass the project create providers
and manually inject an entry whose metadata claims the `duckdb_api` type. Such
host-side construction is not a supported SQL credential source, is outside
the hostile-process claim, and does not become authority merely by occupying
`memory`; it is rejected by the module-private concrete-type check even when
the conservative bit is already true. Ordinary SQL cannot create a supported
`duckdb_api` entry without invoking one of the registered project providers.
This boundary lets unrelated or manually fabricated host state
neither redirect a scan nor force provider work, while all project-owned
persistent metadata/record mismatches still fail closed.

Pinned DuckDB invokes `InitializeSecrets` before every `CREATE SECRET` bind and
may enumerate its generic `local_file` directory during that host DDL. The
current temporary-only product already inherits that one-time host behavior;
an extension cannot redirect or disable it without changing all DuckDB secret
semantics. This RFC does not classify host secret DDL as scan provider work.
It does require the executable cold-restart and hostile-directory oracle to
prove that an admitted scan using project persistence performs no such
enumeration, and that temporary exact lookup occurs only after the host has
already initialized.

`ScanAuthorization` retains the value and the opaque identity pair. Runtime's
admission and authenticators remain the only readers of credential bytes.
Runtime exposes only opaque identity equality/hash operations to future
authority-scoped accounting; neither Query nor Connector sees provider-private
configuration. The existing `ScanPlan`, `ScanRequest`, `CompiledConnector`, and
connector-package grammar do not change.

### Operational behavior

- Resolution occurs once after complete Runtime admission and before the first
  request. A rejected plan produces zero provider observations; a provider
  failure produces zero transport observations.
- The moved snapshot is owned by the `BatchStream` for its complete lifecycle,
  including all sequential pages and any future attempts. Replacement, drop,
  or environment change cannot mutate it.
- Every later execution of a prepared statement resolves again. It observes
  the then-current entry/value or a provider failure.
- Concurrent resolutions return independent move-only authorization storage.
  Sharing an authority/revision identity never shares token buffers or mutable
  provider state.
- Cancellation before or immediately after each bounded provider read raises
  `ExecutionCancelled`; no snapshot enters Runtime. Host shutdown or a provider
  exception becomes one fixed redacted provider failure and releases cloned
  DuckDB state and plaintext before unwinding.
- Missing entries, ambiguous supported storage, wrong type/provider inside a
  project concrete entry or persistent record, malformed project metadata,
  missing/empty/invalid/oversized values, environment absence, persistent read
  or deserialization failure, and provider shutdown failure are explicitly
  classified as `credential_provider`. Same-named foreign or generic host
  secrets are ignored rather than classified as project configuration.
  Resource-stage details may remain coarse `ErrorStage` facts, but the primary
  class stays provider-specific.
- Runtime attaches complete RFC 0021 properties to a provider failure after
  admission: class `credential_provider`, phase `admit`, step and rows zero,
  v1 attempt ordinal one, no remote status, and replay derived conservatively
  from the admitted plan.
- A remote endpoint's `401` or `403` remains `authorization`; no provider error
  is relabeled from a remote status and no remote message enters diagnostics.
- Close, cancellation, terminal failure, and destruction release the snapshot
  non-throwingly. The existing no-zeroization/hostile-process limitation
  remains explicit.

## Topology impact

| Team | Role in this RFC | Interface or charter impact | Interaction mode | Exit condition |
| --- | --- | --- | --- | --- |
| Query Experience | Sponsor and DuckDB adapter owner | Registers the closed providers, adapts DuckDB Secret Manager to the provider service, owns SQL lifecycle and redacted user failures | Collaboration | Prepared, concurrent, persistent-restart, environment, replacement, deletion, cancellation, and failure demonstrations pass through ordinary SQL while adapter code consumes only the provider snapshot API |
| Remote Runtime | Provider-contract and scan-snapshot owner | Supplies opaque authority/revision values and retains exactly one immutable snapshot for the complete stream | Collaboration, then X-as-a-Service | Query resolves one logical credential through the documented service; Runtime's focused provider/snapshot tests and Query's consumer target need no provider-private construction |
| Engineering Enablement | Public-contract gate facilitator | Adds a freeze-bound provider/storage/source vocabulary and mutation tests, then transfers maintenance to Query and Runtime | Facilitation | Query and Runtime maintain the closed vocabulary and product gates without Enablement-specific intervention |

No accountability boundary moves. Connector Experience is not affected because
package credential kinds, logical `secret_field`, placements, destinations,
schema, and compiler output are unchanged. Relational Semantics is not affected
because `ScanRequest`/`ScanPlan` retain the same logical reference and no
relational or planning decision can inspect provider or revision state.

## Correctness, security, and lifecycle analysis

- **Relational semantics and conservative fallback:** Not affected. Credential
  snapshots grant no predicate, projection, ordering, limit, cardinality, or
  operation-selection fact. Provider failure produces no rows and no fallback
  to anonymous execution.
- **Authentication, credentials, network policy, and privacy:** Directly
  affected. Exact named selection remains mandatory; explicit provider config
  replaces ambient selection; the environment source reads one admitted name;
  persistent plaintext-at-rest limitations are documented; Runtime alone
  applies bytes to the admitted authenticator/destination; opaque identity
  values never render.
- **Resource budgets, backpressure, and cancellation:** Credential values retain
  the existing 8 KiB ceiling. The project storage caps index bytes, record
  count, and exact-record bytes before allocation or parsing; resolution never
  invokes DuckDB's generic persistent-directory enumeration/deserializer. The
  DatabaseInstance-scoped project handle plus the monotonic temporary-presence
  bit make that claim executable at cold restart rather than assuming an exact
  Secret Manager lookup is exact internally. DuckDB secret DDL retains the
  pinned host's pre-existing one-time generic initialization limitation, which
  is outside scan provider execution and is stated rather than hidden.
  Resolution occurs after admission and before streaming, observes cancellation
  around each bounded read, and creates no queue or prefetch state. Future
  remote providers require their own deadline/resource contract before
  implementation.
- **Replay units, retries, caching, and duplicate prevention:** No mechanism is
  enabled. One immutable credential snapshot is part of the scan/attempt
  identity, and every future attempt in that scan must reuse it. A future retry,
  cache, single-flight, or rate-limit design must isolate by opaque authority
  and apply RFC 0021's replay/budget rules; this RFC does not authorize one.
- **Concurrency, immutability, and state ownership:** Provider reads clone one
  temporary DuckDB entry or one bounded persistent record in the active
  call-scoped context. The result owns independent token storage.
  Authority/revision values are immutable and comparable only as opaque
  identifiers. Temporary entries retain DuckDB transaction behavior;
  persistent mutation is autocommit-only and serialized in-process. The private
  exclusive directory lock rejects a second live DatabaseInstance before it
  can read or mutate the store. Environment mutation concurrent with a read is
  not a supported
  rotation protocol; operators change it between scans or restart the process.
- **FFI, initialization, reload, shutdown, and failure containment:** No public
  FFI surface or connector reload coupling. The storage implementation uses a
  small supported-cell POSIX descriptor boundary (`openat`/`fstat`/`renameat`/
  `unlinkat`/`fsync`/advisory lock) whose ownership, no-follow, close, and
  failure invariants are documented and failure-path tested. Secret
  type/provider registration keeps
  DuckDB 1.5.4's non-transactional orphan limitation. The project storage
  explicitly rejects persistent mutation inside a user transaction, avoiding
  DuckDB `local_file`'s file-before-catalog-commit rollback divergence.
  Resolution is call-scoped to the active `ClientContext`; cancellation or host
  failure unwinds before snapshot ownership. Provider and snapshot destructors
  are non-throwing.
- **Diagnostics, redaction, metrics, and progress:** Provider failures receive
  RFC 0021's closed `credential_provider` class with fixed safe fields/messages.
  Secret names, environment-variable names, authority/revision values, storage
  paths, token bytes, and host exception text remain absent. No metrics or live
  progress surface is added.

## Compatibility and migration

The change is additive under the pre-`1.0.0` policy. Existing explicit
temporary `duckdb_api/config` statements and authenticated relation calls keep
their syntax and per-execution replacement behavior. Connector packages,
prepared SQL, plans, and stored package generations require no migration.

The intentional lookup changes are from memory-only to a closed two-storage
search and from accepting any generic memory object that claims the type to
accepting only the module-private project concrete type. Same-named foreign
DuckDB secret types and manually registered generic objects are ignored; they
cannot become credential authority or block a supported entry. A database that
contains supported same-named `duckdb_api` entries in both project storages
fails explicitly as ambiguous; it never silently chooses persistent state over
temporary state. Existing releases could not create a supported persistent
`duckdb_api` entry, so a supported cross-storage conflict can arise only after
adoption.

Persistent `duckdb_api/config` records are readable only by versions that
register the same secret type/provider and project storage format. Rolling back
to a release that rejects persistence leaves project-owned files on disk but makes
them unusable through the old extension; rollback does not delete credential
material. Operators must drop persistent secrets before rollback when that
retained state is unacceptable.

The supported compatibility cell remains pinned DuckDB 1.5.4 on `osx_arm64`.
Alternate DuckDB secret storages and future DuckDB provider APIs fail closed.
The public addition is classified as the next pre-`1.0` minor release by the
accepted release policy; this RFC does not broaden the host/platform matrix.

## Evidence and bounded trials

| Question or claim | Evidence required | Method or fixture | Result and limitations |
| --- | --- | --- | --- |
| Is DuckDB's built-in `local_file` safe for the promised transaction/rotation behavior? | Explicit create/replace/drop commit/rollback plus restart | Pinned DuckDB 1.5.4 CLI with isolated `secret_directory`; source inspection of `CatalogSetSecretStorage::{StoreSecret,DropSecretByName}` and `LocalFileSecretStorage::{WriteSecret,RemoveSecret}` | **Rejected by trial.** `BEGIN; CREATE PERSISTENT SECRET tx_create ...; ROLLBACK` removed the in-process entry but a new process loaded `tx_create` from disk. Source confirms write/delete precedes transaction commit and replacement removes the old file first. This evidence invalidated the original built-in-storage proposal; the decision now requires a bounded project storage with autocommit-only mutation and failure-atomic index selection. |
| Can a project storage enforce autocommit and bounded exact reads through the pinned host API? | Source-level interface proof plus eventual restart/failure oracle | DuckDB 1.5.4 `SecretManager::LoadSecretStorage`, virtual `SecretStorage` API, and `CatalogTransaction::context`/`ClientContext::transaction.IsAutoCommit()` | Source-confirmed: an extension storage controls store/get/list/drop, receives the active catalog transaction, can reject explicit transactions before mutation, and can avoid generic local-file enumeration. Product oracle pending implementation. |
| Can a cold-restart scan check supported temporary ambiguity without initializing generic `local_file`? | Complete pinned call path plus oversized-host-directory and foreign-entry oracles | DatabaseInstance project-state handle, monotonic project temporary-create bit, module-private concrete secret type, direct bounded project lookup, and exact `memory` lookup only after project temporary DDL has initialized Secret Manager | Source-confirmed: `GetSecretByName` always calls `InitializeSecrets` and generic `local_file` enumerates matching host filenames, invalidating an unconditional memory lookup. The revised path skips memory when no supported project temporary create has occurred, otherwise proves initialization already happened, and ignores same-named foreign/generic objects that cannot be project authority. Product restart/hostile-directory/foreign-object oracles pending. |
| Can plaintext project files retain private-file and path-race protections? | Wrong-owner/mode, link, special-file, second-instance, and path-replacement failure oracles | Retained directory descriptor; component-wise `openat`; owner/mode/type checks; `O_NOFOLLOW`/`O_EXCL`; `renameat`/`unlinkat`; exclusive private lock | Decision requires private descriptor-relative I/O and fixed redacted failures. Supported-cell syscall feasibility is source/platform established; injected and native failure oracles pending. |
| What persistent drop syntax is enforceable through pinned DuckDB? | Parser/API proof for qualified and unqualified removal | `DROP PERSISTENT SECRET ... FROM duckdb_api`; `SecretManager::DropSecretByName`; virtual storage drop callback | Pinned CLI accepted qualified, persistent-unqualified, and default-unqualified grammar; source confirms the manager passes the selected backend only a name and transaction, so it cannot enforce that the caller wrote `FROM`. The RFC supports canonical qualified removal and DuckDB's ambiguity-checked unique-match shorthand instead of claiming mandatory qualification. |
| Can replacement preserve authority while changing revision without caller input? | Storage-index prior-entry read and opaque-ID oracle | Project storage index plus `CreateSecretInput::on_conflict`, `CREATE OR REPLACE` focused test | Interface-confirmed: the provider receives conflict mode/name/storage and the storage index owns the current opaque identity. Product oracle pending. |
| Can environment values remain late-bound without ambient selection? | Creation with zero environment reads; exact-name resolution with one read per scan | Injected deterministic environment reader plus ordinary-process end-to-end probe | Interface is feasible; implementation must prove no bind/prepare read, exact-variable lookup, rotation only on a new scan, and absent/invalid value failure. |
| Does one scan retain one credential across every page? | Mid-scan replace/drop/env-change counterexample | Controlled two-page Runtime service pauses after page one while Query rotates or drops the source, then records page-two authorization and a later scan | Existing stream ownership strongly supports the claim; explicit revision/value oracle pending. |
| Are provider and API failures distinguishable and redacted? | Stable structured class and sentinel-absence oracle | Missing/malformed/read-failure provider cases versus controlled `401`/`403` | RFC 0021 provides both classes; current Query provider failures are unclassified and require the implementation change. |
| Can RFC acceptance remain freeze-visible before implementation? | A generic decided-future record plus omission/drift/graduation mutations | `accepted_contract_revisions`, scoped current-artifact SHA-256 identities, `scripts/contract_freeze.py`, and `test/python/contract_freeze_tests.py` | Acceptance evidence added with this RFC: a temporary Accepted-status mirror passes canonical verification and fails closed on section/entry omission, current/target drift, premature graduation, one-sided and coordinated retained-exclusion loss, and additive current-source or SQL-oracle content drift. The actual gate becomes green only after the required review rows permit this RFC's status to become Accepted. |

The bounded trial resolved the only decision-critical storage uncertainty by
rejecting DuckDB's generic `local_file` path. The pinned host source proves the
custom storage and transaction-admission hooks, and the existing execution path
proves single-authorization stream ownership. Delivery must supply the pending
executable oracles before the goal closes.

## Alternatives considered

### Keep temporary memory only

This retains the smallest attack surface and the existing implementation. It
does not provide restart durability or execution-time environment rotation and
therefore does not meet the approved operator outcome.

### Use DuckDB's generic `http` secret providers

Generic HTTP secrets include proxy, TLS, path scopes, arbitrary headers, and
providers owned by another extension. Reuse would grant broader authority,
couple behavior to optional autoloading, and blur which provider failures the
runtime owns. The narrow project type remains selected.

### Resolve environment values during `CREATE SECRET`

This matches many host provider implementations and makes the resulting entry
indistinguishable from config. It violates the approved late execution-time
resolution boundary and prevents a prepared query from observing environment
rotation on a new scan without recreating the secret.

### Let the table-function caller choose provider or storage

Adding `provider :=` or `storage :=` arguments would put authority selection in
the query and plan, allow one prepared statement to cross credential domains by
arguments, and expose provider concepts to Semantics. Exact logical-name lookup
with host-derived provider state is selected.

### Prefer temporary over persistent state on duplicate names

Precedence is convenient for local overrides but lets creation or deletion in
one storage silently redirect a prepared statement to another principal.
Ambiguity fails closed; storage movement is explicit drop-then-create.

### Derive revision identity from credential bytes

A stable digest would recognize equal environment values, but it creates a
secret-derived identifier with offline-guessing and leakage risk. Stored config
revisions change on replacement; environment resolutions conservatively receive
a fresh opaque revision each time.

### Add arbitrary executable or external secret providers now

Commands, scripts, OAuth, cloud vaults, and network providers need independent
trust, timeout, resource, cancellation, refresh, and cost decisions. The
provider-neutral snapshot interface is intentionally capable of a later
adapter, but this decision implements only bounded local DuckDB sources.

## Drawbacks and failure modes

Persistent config credentials are plaintext-at-rest in the project storage
beneath DuckDB's configured secret directory. This is a material operational
risk. The technical proposal contains it with owner-only descriptor-relative
storage and documents it rather than hiding behind an encryption claim, but
the product manager explicitly approved that at-rest boundary on 2026-07-23.

The project storage rejects a secret-directory path containing a symbolic-link
component and rejects files or its project directory when ownership, type, or
private-mode checks fail. That is stricter than generic host file access and can
require an operator to choose a link-free `secret_directory`. The restriction
is intentional because the supported persistent records contain plaintext
authority.

Persistent DDL is intentionally non-transactional and allowed only in
autocommit. This diverges from temporary-memory secret DDL, whose current
transaction visibility and rollback behavior remains unchanged. The stable
rejection prevents the worse pinned-host behavior demonstrated by the bounded
trial, where rollback changed catalog state without rolling back the file.

All DuckDB `CREATE SECRET` statements still enter pinned DuckDB's global
Secret Manager initialization, which may enumerate the host's generic
`local_file` directory once. The installed temporary-only product already has
that host DDL limitation. The scan-resolution path introduced here avoids it;
this RFC does not pretend an extension can replace the host manager's DDL
initialization without changing unrelated DuckDB secret behavior.

Environment values are process-global and their provider records name a process
variable. They improve rotation and deployment ergonomics but are not a safe
concurrent mutable store. A missing variable, an empty value, or a value changed
during unsupported concurrent process mutation fails or yields the single value
returned by the host read; the scan never performs a second read.

Opaque authority/revision metadata and the index/record protocol add persistent
format state. A malformed or older record fails closed; silent repair would
risk changing identity. A crash may leave an unreachable record, but index
selection never points to a partial record and later successful mutation cleans
orphans. Future format evolution needs an explicit compatibility path.

All credentials still exist transiently in the process, DuckDB entry or
environment, authorization state, and request buffers. The extension cannot
protect against the process owner, debugger, core dump, SQL history containing a
literal token, or explicitly unredacted DuckDB inspection.

Provider registration remains non-transactional in pinned DuckDB: a later
provider or function registration failure can leave an orphan type/provider.
Load fails and does not attempt repair, matching the existing contract.

## Acceptance and verification

- **End-to-end demonstration:** Configure a temporary config credential, an
  environment credential, and a persistent config credential; execute one
  prepared authenticated query with replacement between executions; pause a
  multi-page scan while replacing and deleting its source; run concurrent
  identities; restart for persistent resolution; and observe that active scans
  retain their snapshot while new scans see the new value or a classified
  provider failure.
- **Automated oracle:** Closed provider/option/storage validation; authority and
  revision create/replace/drop/recreate laws; persistent restart; environment
  exact-read and rotation; prepared execution; concurrent isolation; active-
  scan replacement/deletion; provider cancellation/shutdown; snapshot
  destruction; rejected-plan zero-provider-observation; same-named foreign type
  and manually registered generic-object non-authority with the temporary bit
  both false and true; and sentinel absence from plans, explanation, inventory,
  diagnostics, logs, caches, and failure properties.
- **Failure-path evidence:** Missing and ambiguous state, wrong type/provider,
  malformed identity metadata, missing/invalid/oversized environment value,
  bounded index/record exhaustion, persistent lazy-load/read/write/rename/remove
  failure, wrong owner/mode, link or special-file admission, directory path
  replacement, second-live-instance lock rejection, explicit-transaction
  create/replace/drop rejection with zero file mutation, provider exception,
  cancellation before and after read, invalid plan before provider observation,
  cold restart with an oversized hostile generic-secret directory and zero
  generic enumeration, foreign and generic same-name memory entries before and
  after any project temporary create, source deletion during an active scan,
  concurrent replacement, and host shutdown during resolution.
- **Quality gates:** The complete `AGENTS.md` current-verification and fresh
  native product gates; `make build`, `make test`, and `make demo`; provider
  closed-vocabulary freeze verification and mutation tests; direct-load SQL
  contract; source identities and native dependency gates.
- **Independent review:** Query Experience lifecycle/product review, Remote
  Runtime credential/policy/concurrency review, Engineering Enablement gate
  review, and at least two fresh `$adversarial-review` perspectives covering
  transport/policy and Rust/C++ lifecycle/test oracles.
- **Interaction exit:** Final source and target dependencies show Query consumes
  the documented provider/snapshot API without Runtime-private construction;
  Runtime provider and identity oracles run independently; Query's SQL/product
  tests use a bounded fixture service rather than Runtime internals; the freeze
  vocabulary is maintained by the accountable teams.

## Contract propagation

| Source of truth or artifact | Impact | Required update | Completion evidence |
| --- | --- | --- | --- |
| `docs/ARCHITECTURE.md` | Affected | Replace the temporary-memory-only resolution narrative with provider-neutral execution-time snapshot identity, rotation, persistence warning, and scan pinning | Pending implementation |
| `docs/CONNECTOR_SPECIFICATIONS.md` | Clarification only | State that `secret_field` and package credentials never select or inspect the operator's provider/storage/revision; connector grammar is unchanged | Pending implementation |
| `docs/RUNTIME_CONTRACTS.md` | Affected | Define provider/snapshot API, authority/revision identity, provider failure class, scan ownership, concurrency, cancellation, close, and storage/source behavior | Pending implementation |
| `docs/TEAM_TOPOLOGY.md` and active charters | Not affected | Existing Query-to-Runtime credential/lifecycle service responsibilities already cover the interface; no accountability movement | Final dependency audit pending |
| `docs/PRODUCT_DELIVERY.md`, `AGENTS.md`, and skills | Not affected | Existing credential, privacy, interface, lifecycle, contract-change, review, and verification rules govern delivery | Not applicable |
| `ROADMAP.md`, `README.md`, examples, diagnostics, fixtures, and tests | Affected | Record the additive minor outcome, supported SQL forms, plaintext-persistence warning, removed limitation, classified provider diagnostics, deterministic product demo and oracle matrix | Pending implementation |
| `release/1.0.0/freeze.json` / `.md` | Affected | Generalize decided-future tracking for non-schema contracts and record the current/target provider-source-storage sets, current artifact authorities, graduation rule, and retained exclusions; implementation later graduates the entry into a live closed set | Completed by RFC acceptance with fail-closed mutation evidence; graduation pending implementation |
| Public-surface inventory | Not affected | No function, argument, return schema, or overload changes; provider behavior is governed by the freeze and SQL contract instead | Verified by final inventory gate |

## Unresolved questions

None decision-critical. A later external provider must decide network
deadlines, resource budgets, refresh, shutdown, cost, and compatibility in its
own RFC.

## Review record

| Required reviewer | Team | Result | Evidence or objection | Disposition by decision owner |
| --- | --- | --- | --- | --- |
| Query Experience perspective | Query Experience | Approved | Initial review rejected `local_file` transaction divergence; re-reviews required private descriptor-relative files, enforceable drop syntax, and a cold-restart rule for foreign memory entries. Final source review confirmed virtual `Clone` preserves a module-private concrete secret type through `SecretEntry`, so the project-only temporary bit is complete for supported SQL authority | Replaced `local_file` with autocommit-only bounded project storage; specified ownership/mode/type/no-follow/lock rules; recorded qualified plus unique-match drop; scoped ambiguity to project concrete entries and added bit-false/true foreign-object oracles. Acceptance interaction satisfied; delivery exit remains Open |
| Remote Runtime perspective | Remote Runtime | Approved | Initial review objected to Query-before-admission resolution, unbounded `local_file` initialization, private-file loss, attempt zero, and foreign-entry ambiguity. Final review confirmed the admission-first API, direct project handle, project-only concrete-type bit, private storage rules, and RFC 0021 attempt ordinal one resolve those decision gaps | Public provider entry point owns admission ordering; cold restart cannot invoke generic enumeration; foreign objects are non-authority; all executable oracles remain mandatory. Delivery interaction remains Open |
| Engineering Enablement perspective | Engineering Enablement | Approved | Initial gate review found marker-only current authority and coordinated exclusion removal fail-open. Final review confirmed scoped source/SQL SHA-256 identities, explicit negative environment SQL, exact retained exclusions, additive-drift mutation, coordinated-removal mutation, and 53 passing temporary-Accepted freeze tests | Generalized the pending-contract ledger and made exact content/exclusion drift fail closed. Acceptance capability transferred; implementation graduation remains Open |

## Decision and rationale

- **Technical decision owner:** Lead agent.
- **Product approval:** Nic Galluzzo approved the outcome, provider categories,
  scan-snapshot semantics, guardrails, exclusions, and acceptance boundary in
  the supplied product-goal objective on 2026-07-23, and explicitly approved
  the owner-only plaintext-at-rest persistent `config` boundary on 2026-07-23.
- **Rationale:** The project-owned bounded storage is selected over DuckDB
  `local_file` because the pinned rollback/restart counterexample disproves the
  required mutation contract. Admission-first provider resolution and one
  move-only authority/revision snapshot preserve Runtime policy and scan
  immutability. A module-private temporary concrete type plus direct project
  state is the only pinned-host-feasible path found that preserves supported
  ambiguity checks without turning an admitted scan into generic host-directory
  enumeration. With the reserved plaintext-at-rest decision approved and all
  required reviewers approving, the technical decision owner accepts the RFC.
- **Material objections:** Query's transaction, file-safety, drop, and foreign-
  memory objections; Runtime's admission, unbounded initialization, permission,
  attempt-ordinal, and foreign-memory objections; and Enablement's additive-
  drift/coordinated-exclusion objections were all accepted and resolved as
  recorded above. No team objection remains open at decision level.
- **Superseded by:** Not applicable.

## Follow-on goals

| Outcome or objective | Accountable team | Supporting teams and interaction modes | Activation condition |
| --- | --- | --- | --- |
| Operators can configure, rotate, replace, and remove supported durable credentials while every scan retains one explicit authority snapshot | Query Experience | Remote Runtime (Collaboration then X-as-a-Service); Engineering Enablement (Facilitation) | RFC 0023 accepted with product approval and required review recorded |
