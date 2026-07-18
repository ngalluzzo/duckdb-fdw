# Repeatable-installation trial results

Status: **Passed on the recorded `osx_arm64` target**

This file is the durable, human-readable record for the Query Experience
oracle. Generated artifacts and the complete path-normalized JSON observation
remain in ignored build storage; this record preserves stable evidence without
promoting the trial into installation guidance.

## Invocation

```text
scripts/run-installability-trial.sh --output-root <new-ignored-output-root>
```

Evidence schema: `duckdb_api/repeatable-installation-evidence/v1`

## Immutable inputs

| Identity | Observed value |
| --- | --- |
| Project source commit and tree | `f855dfb5f5de0be7cb8ffd6a58d54552aeaada8d`; `77819955090f36a596a502f6aec8c2ebd9e28873` |
| Release manifest SHA-256 | `764be0f79b373c53f61926e96dd5b56ca51d1c775cbdd949d85a30ac58b8a4f9` |
| Self-contained verifier SHA-256 | `df7911b4b66bef68cb41ba43e529c16db6300bfb2fcf65c5f221b53b839b875f` |
| Original artifact SHA-256 | `4f1a0678fd2a673b433af6248a34966cb8fd41d107d4c0b3b97ca71eb35179ea` |
| Negative-fixture inventory SHA-256 | `ff23df0e204f98e1f60f24b23e9aeec671c0c0ab54f1984d0315f20f8924a684` |
| Synthetic wrong-platform fixture SHA-256 | `c8780636b59e27ca2225cf3ca669b1f929320eaf9e52a5ee9bbd4da6bfdc72a3` |
| Corrupted fixture SHA-256 | `363a91835463c0344f8a52af868beab9a7e658f899363237ed257d98dd2b04b2` |
| Supported DuckDB identity | `v1.5.4`, `08e34c447b` |
| Mismatched DuckDB identity | `v1.5.3`, `14eca11bd9` |

## User journey

| Step | Required observation | Result |
| --- | --- | --- |
| Verify original bundle | Provider verifier accepts the manifest, anchor, and exact artifact | Passed |
| Clean install | `duckdb_api` 0.1.0 is installed from the custom path but not loaded | Passed |
| Installed bytes | Installed SHA-256 equals the verified manifest artifact SHA-256 | Passed |
| Repeated install | A new process preserves the install path, metadata, and byte digest | Passed |
| Restart and load | A third process loads `duckdb_api` by name from the clean extension directory | Passed |
| Query behavior | Observed behavior equals `release/0.1.0/public_contract.json` | Passed |

The oracle records three supported-path process identifiers as observations,
not fixed values. It proves the process boundary by invoking one host action
per subprocess and does not require operating-system process identifiers to be
unique. Every supported and rejected host observation matched both the expected
DuckDB version and its ten-character source commit.

## Reproducible artifact evidence

Two retained clean-workspace `0.1.0` reproductions were independently checked
against the trusted release source, artifact identity, and their own anchored
manifests. Both produced byte-identical 4,859,678-byte extension artifacts with
SHA-256 `4f1a0678fd2a673b433af6248a34966cb8fd41d107d4c0b3b97ca71eb35179ea`.
The reproduction oracle keeps byte reproducibility separate from the semantic
manifest checks and rejects the same evidence root presented twice.

## Deterministic refusals

| Scenario | Required boundary | Diagnostic evidence | Result |
| --- | --- | --- | --- |
| Default signature policy | Invalid signature refused; no function registration or installed artifact | `doesn't have a valid signature` | Passed |
| DuckDB 1.5.3 host | 1.5.4 artifact refused; no function registration or installed artifact | `built specifically for ... v1.5.4`; current host `v1.5.3` | Passed |
| `linux_amd64` fixture on `osx_arm64` | Platform mismatch refused; no function registration or installed artifact | built for `linux_amd64`; host accepts `osx_arm64` | Passed |
| Corrupted artifact | Provider verifier refuses the release-named file before any Query host invocation | `release artifact does not match the tracked trust record`; Query-host invocations `0` | Passed |

Diagnostic excerpts must preserve the actionable version, platform, signature,
or checksum fact while redacting temporary or machine-local paths.

## Interpretation

The evidence answers these separately:

- **Installability:** whether the verified 0.1.0 bytes survive install, repeat
  install, process restart, load-by-name, and the accepted query.
- **Compatibility enforcement:** whether DuckDB rejects known version and
  platform mismatches before registration.
- **Integrity enforcement:** whether corrupted bytes are rejected before the
  host starts.
- **Authenticity and trust:** whether the default DuckDB signature policy
  accepts the artifact. A negative result is expected for a self-built
  unsigned artifact and must not be reframed as ordinary-user readiness.

All byte-bearing inputs and the self-contained host harness are copied into
private read-only trial storage before verification; source replacement after
that boundary cannot change what DuckDB loads. Host and verifier subprocesses
receive a deliberately minimal environment plus hard time and output bounds.
The verifier digest is checked before and after both the original and corrupted
bundle exchanges, so one result cannot silently replace the authority used by
the other.
Focused counterexamples prove that ambient credentials are not inherited,
output floods are stopped, and stuck direct children and same-process-group
descendants are terminated.

## Limitations and RFC handoff

- The trial covers only the accepted 0.1.0 `osx_arm64` compatibility cell and
  one deliberate DuckDB-version mismatch.
- The downloadable path is local decision evidence, not a published artifact
  channel or authenticated download mechanism.
- The synthetic footer and corrupted bytes are negative fixtures, not release
  products.
- Checksums establish evidence binding, not signer identity or source
  authenticity.
- The process-group lifecycle guard bounds the verified trial code but is not
  an OS sandbox; native code that deliberately creates a new session can escape
  that group. The ordinary-user trust decision cannot rely on this harness for
  malicious-code containment.
- Read-only snapshot directories close ordinary verification-to-use races; they
  do not isolate a hostile process already running as the same operating-system
  account. The controlled trial depends on the exact recorded verifier and
  artifact identities.
- RFC 0004 must compare the source-built, downloadable unsigned, and DuckDB
  Community Extension paths. MIT and the Community target direction are now
  selected; compatibility, updates, rollback, history, backports, support, and
  external proof remain gated.
- The repaired sanitizer artifact custody path is structurally guarded and
  locally proves exact staged-versus-downloaded byte comparison. A new remote
  GitHub Actions run is still required to prove the hosted upload/download
  transport.

Decision-ready recommendation: target DuckDB Community Extensions for the
ordinary stock-DuckDB path because it preserves default signature enforcement.
Retain source build and a verified unsigned artifact only for contributors and
controlled previews. Checksums prove byte integrity and evidence binding, not
publisher authenticity. RFC 0004 remains Draft until the product manager
approves compatibility, update, rollback, historical-version, backport, and
support boundaries and the Community path is proved.
