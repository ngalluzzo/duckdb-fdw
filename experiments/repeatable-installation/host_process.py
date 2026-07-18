"""Run the Query-owned DuckDB process edge in isolated subprocesses.

This module owns subprocess construction, environment isolation, invocation
counting, and JSON exchange with ``query_host.py``. It deliberately knows
nothing about release manifests or the meaning of individual installation
scenarios.
"""

from __future__ import annotations

import json
import os
import pathlib
import selectors
import signal
import subprocess
import time
from collections.abc import Sequence


HOST_ACTION_TIMEOUT_SECONDS = 30.0
PROCESS_TERMINATION_GRACE_SECONDS = 0.25
PROCESS_OUTPUT_LIMIT_BYTES = 64 * 1024
ISOLATED_PATH = "/usr/bin:/bin"


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


class ProcessOutputLimitExceeded(Exception):
    """Raised after a noisy child is reaped and its process group is killed."""

    def __init__(
        self,
        command: Sequence[str],
        limit_bytes: int,
        stdout: str,
        stderr: str,
    ):
        super().__init__(f"process output exceeded {limit_bytes} bytes")
        self.command = tuple(command)
        self.limit_bytes = limit_bytes
        self.stdout = stdout
        self.stderr = stderr


def _signal_process_group(
    process: subprocess.Popen[bytes], value: signal.Signals
) -> None:
    try:
        os.killpg(process.pid, value)
    except ProcessLookupError:
        pass


def _terminate_process_group(process: subprocess.Popen[bytes]) -> None:
    """Give the private group a short TERM grace, then always send KILL."""

    _signal_process_group(process, signal.SIGTERM)
    deadline = time.monotonic() + PROCESS_TERMINATION_GRACE_SECONDS
    while process.poll() is None and time.monotonic() < deadline:
        time.sleep(0.01)
    # Descendants can ignore TERM and close inherited pipes after the direct
    # child exits. KILL is unconditional so that case cannot escape cleanup.
    _signal_process_group(process, signal.SIGKILL)
    try:
        process.wait(timeout=PROCESS_TERMINATION_GRACE_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def _decoded(value: bytearray) -> str:
    return bytes(value).decode("utf-8", errors="replace")


def run_bounded_process(
    command: Sequence[str],
    *,
    cwd: pathlib.Path,
    environment: dict[str, str],
    timeout_seconds: float,
    output_limit_bytes: int = PROCESS_OUTPUT_LIMIT_BYTES,
) -> subprocess.CompletedProcess[str]:
    """Run one isolated process group with bounded output and teardown.

    Pipes are drained incrementally and the combined byte count has a hard
    ceiling. Timeout or overflow terminates the whole private process group;
    normal completion also kills any descendant that outlived the direct child.
    """

    _require(timeout_seconds > 0, "process timeout must be positive")
    _require(output_limit_bytes > 0, "process output limit must be positive")
    process = subprocess.Popen(
        command,
        cwd=cwd,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    assert process.stdout is not None and process.stderr is not None
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ, "stdout")
    selector.register(process.stderr, selectors.EVENT_READ, "stderr")
    buffers = {"stdout": bytearray(), "stderr": bytearray()}
    total_output = 0
    deadline = time.monotonic() + timeout_seconds
    try:
        while selector.get_map():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise subprocess.TimeoutExpired(
                    command,
                    timeout_seconds,
                    output=_decoded(buffers["stdout"]),
                    stderr=_decoded(buffers["stderr"]),
                )
            # Poll the direct child even when a silent descendant keeps a pipe
            # open; otherwise normal child exit could wait until the full bound.
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
                        command,
                        output_limit_bytes,
                        _decoded(buffers["stdout"]),
                        _decoded(buffers["stderr"]),
                    )
            if process.poll() is not None:
                # The direct child can exit while a descendant retains a pipe.
                # End that descendant now so draining cannot wait indefinitely.
                _signal_process_group(process, signal.SIGKILL)

        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise subprocess.TimeoutExpired(command, timeout_seconds)
        process.wait(timeout=remaining)
    except (subprocess.TimeoutExpired, ProcessOutputLimitExceeded):
        _terminate_process_group(process)
        raise
    finally:
        selector.close()
        process.stdout.close()
        process.stderr.close()
    # A successful direct child has no authority to leave native descendants.
    _signal_process_group(process, signal.SIGKILL)
    return subprocess.CompletedProcess(
        command,
        process.returncode,
        _decoded(buffers["stdout"]),
        _decoded(buffers["stderr"]),
    )


class HostRunner:
    """Run and count one isolated DuckDB host process per action.

    The invocation count is observable evidence for the corrupted-input
    scenario, which must be rejected before a host starts.
    """

    def __init__(
        self,
        query_host: pathlib.Path,
        environment: dict[str, str],
        *,
        timeout_seconds: float = HOST_ACTION_TIMEOUT_SECONDS,
        output_limit_bytes: int = PROCESS_OUTPUT_LIMIT_BYTES,
    ):
        _require(timeout_seconds > 0, "host action timeout must be positive")
        _require(output_limit_bytes > 0, "host output limit must be positive")
        self.query_host = query_host
        self.environment = environment
        self.timeout_seconds = timeout_seconds
        self.output_limit_bytes = output_limit_bytes
        self.invocations = 0

    def run(
        self,
        python: pathlib.Path,
        action: str,
        database: pathlib.Path,
        extension_directory: pathlib.Path,
        *,
        artifact: pathlib.Path | None = None,
        allow_unsigned: bool,
    ) -> dict[str, object]:
        command = [
            str(python),
            "-I",
            str(self.query_host),
            action,
            "--database",
            str(database),
            "--extension-directory",
            str(extension_directory),
        ]
        if artifact is not None:
            command.extend(("--artifact", str(artifact)))
        if allow_unsigned:
            command.append("--allow-unsigned")

        self.invocations += 1
        try:
            completed = run_bounded_process(
                command,
                cwd=database.parent,
                environment=self.environment,
                timeout_seconds=self.timeout_seconds,
                output_limit_bytes=self.output_limit_bytes,
            )
        except subprocess.TimeoutExpired as error:
            raise AssertionError(
                f"query host action {action!r} timed out after "
                f"{self.timeout_seconds:g} seconds; the child was terminated "
                "and reaped and its process group was killed"
            ) from error
        except ProcessOutputLimitExceeded as error:
            raise AssertionError(
                f"query host action {action!r} exceeded the "
                f"{self.output_limit_bytes}-byte output limit; the child was "
                "reaped and its process group was killed"
            ) from error
        _require(
            completed.returncode == 0,
            "query host failed outside the observed DuckDB action:\n"
            f"command: {' '.join(command)}\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}",
        )
        try:
            observation = json.loads(completed.stdout)
        except json.JSONDecodeError as error:
            raise AssertionError(
                "query host did not emit one JSON observation: "
                f"{completed.stdout!r}; stderr={completed.stderr!r}"
            ) from error
        _require(
            isinstance(observation, dict),
            "query host observation is not an object",
        )
        return observation


def isolated_environment(root: pathlib.Path) -> dict[str, str]:
    """Create an env-i-style environment for one complete clean-state trial.

    Host credentials, DuckDB flags, Python settings, and test variables are not
    inherited. Pinned Python executables are passed as absolute paths, so the
    fixed system PATH is present only for deterministic child utilities.
    """

    root.mkdir(mode=0o700, exist_ok=True)
    for name in ("home", "tmp", "cache", "config"):
        (root / name).mkdir()
    return {
        "HOME": str(root / "home"),
        "LC_ALL": "C",
        "PATH": ISOLATED_PATH,
        "TMPDIR": str(root / "tmp"),
        "XDG_CACHE_HOME": str(root / "cache"),
        "XDG_CONFIG_HOME": str(root / "config"),
    }
