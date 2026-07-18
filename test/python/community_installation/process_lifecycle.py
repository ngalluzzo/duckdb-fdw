"""Bound stdout/stderr, cancellation, and POSIX process-group teardown.

Every spawned command starts a private session, receives no stdin, and has one
combined output and wall-clock authority.  TERM receives a short grace, KILL
follows, and the direct child is reaped on normal exit and every failure path.
This contains the inherited POSIX process group; a trusted launcher must not
create a different session to evade that documented scope.
"""

from __future__ import annotations

import os
import pathlib
import selectors
import signal
import subprocess
import time
from collections.abc import Sequence


HOST_TIMEOUT_SECONDS = 60.0
OUTPUT_LIMIT_BYTES = 128 * 1024
TERMINATION_GRACE_SECONDS = 0.25


class ProcessBoundaryError(ValueError):
    """The requested process authority is malformed."""


class ProcessOutputLimitExceeded(Exception):
    """The process crossed its combined stdout/stderr byte authority."""


def _decoded(value: bytearray) -> str:
    return bytes(value).decode("utf-8", errors="replace")


def _signal_group(process: subprocess.Popen[bytes], value: signal.Signals) -> None:
    try:
        os.killpg(process.pid, value)
    except ProcessLookupError:
        pass
    except PermissionError:
        # macOS can report EPERM after the leader is reaped and only an already
        # killed zombie remains in the group.  An active direct child must
        # never lose Query's signal authority.
        if process.poll() is None:
            raise


def terminate_process_group(process: subprocess.Popen[bytes]) -> None:
    """Apply TERM/KILL and reap the direct child on every completion path."""

    _signal_group(process, signal.SIGTERM)
    deadline = time.monotonic() + TERMINATION_GRACE_SECONDS
    while process.poll() is None and time.monotonic() < deadline:
        time.sleep(0.01)
    _signal_group(process, signal.SIGKILL)
    try:
        process.wait(timeout=TERMINATION_GRACE_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def run_bounded_process(
    command: Sequence[str],
    *,
    cwd: pathlib.Path | None,
    environment: dict[str, str],
    executable_path: pathlib.Path | None = None,
    inherited_descriptors: Sequence[int] = (),
    timeout_seconds: float = HOST_TIMEOUT_SECONDS,
    output_limit_bytes: int = OUTPUT_LIMIT_BYTES,
) -> subprocess.CompletedProcess[str]:
    """Incrementally drain one command and always close its process group."""

    if timeout_seconds <= 0 or output_limit_bytes <= 0:
        raise ProcessBoundaryError("stock host process bounds must be positive")
    executable = str(executable_path) if executable_path is not None else None
    pass_fds = tuple(dict.fromkeys(inherited_descriptors))
    try:
        process = subprocess.Popen(
            list(command),
            executable=executable,
            pass_fds=pass_fds,
            cwd=cwd,
            env=environment,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            start_new_session=True,
        )
    except OSError as error:
        raise ProcessBoundaryError("stock host process could not start") from error
    assert process.stdout is not None and process.stderr is not None
    selector = selectors.DefaultSelector()
    buffers = {"stdout": bytearray(), "stderr": bytearray()}
    total_output = 0
    deadline = time.monotonic() + timeout_seconds
    try:
        selector.register(process.stdout, selectors.EVENT_READ, "stdout")
        selector.register(process.stderr, selectors.EVENT_READ, "stderr")
        while selector.get_map():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise subprocess.TimeoutExpired(
                    command,
                    timeout_seconds,
                    output=_decoded(buffers["stdout"]),
                    stderr=_decoded(buffers["stderr"]),
                )
            for key, _ in selector.select(min(remaining, 0.05)):
                chunk = os.read(key.fileobj.fileno(), 8192)
                if not chunk:
                    selector.unregister(key.fileobj)
                    continue
                allowed = output_limit_bytes - total_output
                if allowed > 0:
                    buffers[key.data].extend(chunk[:allowed])
                total_output += len(chunk)
                if total_output > output_limit_bytes:
                    raise ProcessOutputLimitExceeded(
                        "stock host output exceeded its byte limit"
                    )
            if process.poll() is not None:
                _signal_group(process, signal.SIGKILL)

        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise subprocess.TimeoutExpired(command, timeout_seconds)
        process.wait(timeout=remaining)
        return subprocess.CompletedProcess(
            list(command),
            process.returncode,
            _decoded(buffers["stdout"]),
            _decoded(buffers["stderr"]),
        )
    finally:
        try:
            terminate_process_group(process)
        finally:
            selector.close()
            process.stdout.close()
            process.stderr.close()
