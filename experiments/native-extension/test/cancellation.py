#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import sys
import threading
import time

import duckdb

EXPECTED_DUCKDB = ("v1.5.4", "08e34c447b", "Variegata")
EXPECTED_EXTENSION = (
    "fdw_boundary_probe",
    "0.0.0-boundary-trial",
    True,
    False,
    "NOT_INSTALLED",
)


def assert_loaded_identity(connection: duckdb.DuckDBPyConnection) -> None:
    duckdb_identity = connection.execute("PRAGMA version").fetchone()
    if duckdb_identity != EXPECTED_DUCKDB:
        raise AssertionError(
            f"unexpected DuckDB host identity: {duckdb_identity!r}"
        )

    extension_identity = connection.execute(
        """
        SELECT extension_name, extension_version, loaded, installed, install_mode
        FROM duckdb_extensions()
        WHERE extension_name = 'fdw_boundary_probe'
        """
    ).fetchone()
    if extension_identity != EXPECTED_EXTENSION:
        raise AssertionError(
            f"unexpected extension artifact identity: {extension_identity!r}"
        )


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: cancellation.py PATH_TO_EXTENSION")

    extension_path = pathlib.Path(sys.argv[1]).resolve()
    connection = duckdb.connect(config={"allow_unsigned_extensions": "true"})
    connection.execute(f"LOAD '{extension_path.as_posix()}'")
    assert_loaded_identity(connection)
    observer = duckdb.connect(config={"allow_unsigned_extensions": "true"})
    observer.execute(f"LOAD '{extension_path.as_posix()}'")
    assert_loaded_identity(observer)

    failure: list[BaseException] = []

    def run_slow_scan() -> None:
        try:
            connection.execute(
                "SELECT count(*) FROM fdw_boundary_probe(1000000, 64, 1000, -1)"
            ).fetchall()
        except Exception as error:  # The query is expected to be interrupted.
            failure.append(error)

    worker = threading.Thread(
        target=run_slow_scan, name="fdw-boundary-probe", daemon=True
    )
    worker.start()

    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        opened, _, chunks, _, active_waiters = observer.execute(
            "SELECT * FROM fdw_boundary_probe_stats()"
        ).fetchone()
        if opened == 1 and chunks > 0 and active_waiters == 1:
            break
        time.sleep(0.01)
    else:
        connection.interrupt()
        raise AssertionError("boundary probe did not begin within five seconds")

    connection.interrupt()
    worker.join(timeout=5)

    if worker.is_alive():
        raise AssertionError("interrupted boundary probe did not stop within five seconds")
    if len(failure) != 1 or "interrupt" not in str(failure[0]).lower():
        raise AssertionError(f"expected one interruption error, received: {failure!r}")

    opened, closed, chunks, interruptions, active_waiters = observer.execute(
        "SELECT * FROM fdw_boundary_probe_stats()"
    ).fetchone()
    if opened != 1 or closed != 1:
        raise AssertionError(
            f"interrupted state was not cleaned up: opened={opened}, closed={closed}"
        )
    if chunks == 0:
        raise AssertionError("interruption occurred before the probe produced any chunk")
    if interruptions != 1:
        raise AssertionError(
            f"probe did not observe exactly one interrupt: {interruptions}"
        )
    if active_waiters != 0:
        raise AssertionError(f"interruptible wait leaked: {active_waiters}")

    print(
        "cancellation evidence: "
        f"opened={opened} closed={closed} chunks={chunks} "
        f"interruptions={interruptions} active_waiters={active_waiters}"
    )
    connection.close()
    observer.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
