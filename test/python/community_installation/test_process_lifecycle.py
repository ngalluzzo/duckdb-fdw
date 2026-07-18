from __future__ import annotations

import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time
import unittest

try:
    from .process_lifecycle import (
        ProcessOutputLimitExceeded,
        run_bounded_process,
    )
except ImportError:
    from process_lifecycle import ProcessOutputLimitExceeded, run_bounded_process


def wait_absent(pid: int) -> bool:
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return True
        time.sleep(0.01)
    return False


class ProcessLifecycleTests(unittest.TestCase):
    def assert_processes_absent(self, pid_file: pathlib.Path) -> None:
        leader, descendant = (
            int(value)
            for value in pid_file.read_text(encoding="utf-8").split()
        )
        for pid in (leader, descendant):
            if not wait_absent(pid):
                os.kill(pid, signal.SIGKILL)
                self.fail(f"same-group process {pid} survived cleanup")

    def test_returns_bounded_stdout_and_uses_no_stdin(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            completed = run_bounded_process(
                [sys.executable, "-I", "-c", "import sys; print(sys.stdin.read())"],
                cwd=pathlib.Path(directory),
                environment={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
                timeout_seconds=1,
                output_limit_bytes=1024,
            )
            self.assertEqual(completed.returncode, 0)
            self.assertEqual(completed.stdout, "\n")

    def test_child_can_bind_inherited_state_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            descriptor = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
            try:
                completed = run_bounded_process(
                    [
                        sys.executable,
                        "-I",
                        "-c",
                        (
                            "import os,pathlib,sys;os.fchdir(int(sys.argv[1]));"
                            "pathlib.Path('bound').touch()"
                        ),
                        str(descriptor),
                    ],
                    cwd=None,
                    inherited_descriptors=(descriptor,),
                    environment={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
                )
            finally:
                os.close(descriptor)
            self.assertEqual(completed.returncode, 0)
            self.assertTrue((root / "bound").is_file())

    def test_output_overflow_and_timeout_are_hard_failures(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            with self.assertRaises(ProcessOutputLimitExceeded):
                run_bounded_process(
                    [
                        sys.executable,
                        "-I",
                        "-c",
                        "import os,time; os.write(1,b'x'*4096); time.sleep(10)",
                    ],
                    cwd=root,
                    environment={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
                    timeout_seconds=1,
                    output_limit_bytes=1024,
                )
            with self.assertRaises(subprocess.TimeoutExpired):
                run_bounded_process(
                    [sys.executable, "-I", "-c", "import time; time.sleep(10)"],
                    cwd=root,
                    environment={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
                    timeout_seconds=0.05,
                    output_limit_bytes=1024,
                )

    @unittest.skipUnless(hasattr(os, "fork"), "requires POSIX process groups")
    def test_timeout_and_overflow_kill_leader_and_descendant(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for name, overflow in (("timeout", False), ("overflow", True)):
                pid_file = root / f"{name}.pid"
                program = f"""
import os, pathlib, time
child = os.fork()
if child == 0:
    time.sleep(10)
    os._exit(0)
pathlib.Path({str(pid_file)!r}).write_text(f'{{os.getpid()}} {{child}}')
{("os.write(1, b'x' * 4096)" if overflow else "time.sleep(10)")}
"""
                error = (
                    ProcessOutputLimitExceeded
                    if overflow
                    else subprocess.TimeoutExpired
                )
                with self.assertRaises(error):
                    run_bounded_process(
                        [sys.executable, "-I", "-c", program],
                        cwd=root,
                        environment={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
                        timeout_seconds=0.5,
                        output_limit_bytes=1024,
                    )
                self.assert_processes_absent(pid_file)

    @unittest.skipUnless(hasattr(os, "fork"), "requires POSIX process groups")
    def test_normal_exit_kills_descendant_holding_output_pipes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            pid_file = root / "open-pipes.pid"
            program = f"""
import os, pathlib, time
child = os.fork()
if child == 0:
    time.sleep(10)
    os._exit(0)
pathlib.Path({str(pid_file)!r}).write_text(f'{{os.getpid()}} {{child}}')
print('done')
"""
            started = time.monotonic()
            completed = run_bounded_process(
                [sys.executable, "-I", "-c", program],
                cwd=root,
                environment={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
                timeout_seconds=1,
                output_limit_bytes=1024,
            )
            self.assertLess(time.monotonic() - started, 1)
            self.assertEqual(completed.stdout, "done\n")
            self.assert_processes_absent(pid_file)

    @unittest.skipUnless(hasattr(os, "fork"), "requires POSIX process groups")
    def test_normal_exit_kills_same_group_descendant_with_closed_pipes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            pid_file = root / "descendant.pid"
            program = f"""
import os, pathlib, time
child = os.fork()
if child == 0:
    os.close(1)
    os.close(2)
    time.sleep(10)
    os._exit(0)
pathlib.Path({str(pid_file)!r}).write_text(str(child))
print('done')
"""
            completed = run_bounded_process(
                [sys.executable, "-I", "-c", program],
                cwd=root,
                environment={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
                timeout_seconds=1,
                output_limit_bytes=1024,
            )
            self.assertEqual(completed.stdout, "done\n")
            descendant = int(pid_file.read_text(encoding="utf-8"))
            if not wait_absent(descendant):
                os.kill(descendant, signal.SIGKILL)
                self.fail(f"same-group descendant {descendant} survived cleanup")


if __name__ == "__main__":
    unittest.main()
