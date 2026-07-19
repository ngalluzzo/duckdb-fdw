# Repeatable installation experiment

Status: completed experiment. Its findings informed
[RFC 0004](../../docs/rfcs/0004-select-repeatable-installation-and-trust-path.md);
it is not a current installation channel or compatibility promise.

## Question and result

This trial asked whether the immutable `v0.1.0` extension behavior could cross
DuckDB's installation boundary while making artifact identity, platform and
DuckDB compatibility, and unsigned-development trust explicit to the user.

The trial proved a controlled install/restart/load path and deterministic
refusal of an unsigned default load, a mismatched DuckDB version, a mismatched
platform, and corrupted bytes. See [RESULTS.md](RESULTS.md) for the retained
findings and [RUNBOOK.md](RUNBOOK.md) for exact inputs, trust boundaries, and
failure handling.

## Reproduce

The integrated repository runner prepares the provider bundle and executes the
Query-owned oracle:

```sh
scripts/run-installability-trial.sh
```

It expects the retained `v0.1.0` evidence and pinned Python hosts documented in
the runbook. Generated databases, extension directories, and evidence stay
under `.build/repeatable-installation/` by default and must not be committed.

Run the Query-side deterministic tests without provider artifacts:

```sh
python3 -I experiments/repeatable-installation/test_query_oracle.py
```

The top-level Python modules own input admission, immutable snapshots, bounded
verifier and host processes, installation scenarios, and normalized evidence.
Provider-side custody and negative-fixture tools are documented in
[`enablement/`](enablement/).
