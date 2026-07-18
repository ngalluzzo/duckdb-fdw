"""Compose one bounded stock-DuckDB process action.

``StockHostRunner`` is Query's small public process seam.  The caller owns the
explicit launcher and new state/environment roots.  The runner composes root
and environment policy, bounded process lifecycle, and the versioned JSON
protocol; it contains no DuckDB SQL, support-matrix, build, or custody logic.
Each call creates one process and the child owns and closes its connection.
"""

from __future__ import annotations

import pathlib
import re
import subprocess
from collections.abc import Sequence

try:
    from .host_protocol import ProtocolError, parse_observation
    from .file_admission import (
        FileAdmissionError,
        regular_file,
        stage_content_identified_file,
    )
    from .host_environment import HostEnvironmentError, isolated_environment, private_root
    from .launcher import Launcher, LauncherError
    from .lifecycle import HostObservation
    from .matrix import RowIdentity
    from .process_lifecycle import (
        HOST_TIMEOUT_SECONDS,
        OUTPUT_LIMIT_BYTES,
        ProcessBoundaryError,
        ProcessOutputLimitExceeded,
        run_bounded_process,
    )
    from .state_capability import StateCapability, StateCapabilityError
except ImportError:
    from host_protocol import ProtocolError, parse_observation
    from file_admission import (
        FileAdmissionError,
        regular_file,
        stage_content_identified_file,
    )
    from host_environment import HostEnvironmentError, isolated_environment, private_root
    from launcher import Launcher, LauncherError
    from lifecycle import HostObservation
    from matrix import RowIdentity
    from process_lifecycle import (
        HOST_TIMEOUT_SECONDS,
        OUTPUT_LIMIT_BYTES,
        ProcessBoundaryError,
        ProcessOutputLimitExceeded,
        run_bounded_process,
    )
    from state_capability import StateCapability, StateCapabilityError


SHA256 = re.compile(r"[0-9a-f]{64}")


class HostActionError(AssertionError):
    """The stock host did not complete one safe observation exchange."""


class StockHostRunner:
    """Run and observe one fresh stock process per lifecycle action.

    The instance is single-orchestrator state.  Calls are sequential and share
    only the named DuckDB state beneath ``state_root``.  Timeout/output failure
    cancels the process group and reaps the direct child.  The runner never
    deletes caller roots, enables unsigned extensions, or interprets provider
    records. The child protocol binds its cwd to the explicitly inherited state
    descriptor before it resolves either relative state leaf; parent-side
    pre-exec callbacks are not used.
    """

    def __init__(
        self,
        *,
        launcher: Launcher,
        row: RowIdentity,
        state_root: pathlib.Path,
        environment_root: pathlib.Path,
        incompatible_artifact: pathlib.Path | None = None,
        incompatible_artifact_size: int | None = None,
        incompatible_artifact_sha256: str | None = None,
        incompatible_artifact_limit_bytes: int = 256 * 1024 * 1024,
        action_program: pathlib.Path | None = None,
        diagnostic_roots: Sequence[pathlib.Path] = (),
        timeout_seconds: float = HOST_TIMEOUT_SECONDS,
        output_limit_bytes: int = OUTPUT_LIMIT_BYTES,
    ):
        if len(
            {
                incompatible_artifact is None,
                incompatible_artifact_size is None,
                incompatible_artifact_sha256 is None,
            }
        ) != 1:
            raise HostActionError(
                "incompatible artifact path, size, and digest must be supplied together"
            )
        if (
            incompatible_artifact_sha256 is not None
            and SHA256.fullmatch(incompatible_artifact_sha256) is None
        ):
            raise HostActionError("incompatible artifact digest is malformed")
        incompatible_source = (
            pathlib.Path(incompatible_artifact).expanduser().absolute()
            if incompatible_artifact is not None
            else None
        )
        try:
            self.state_root = private_root(state_root, "host state root")
            self.environment_root = private_root(
                environment_root, "host environment root"
            )
            if self.state_root == self.environment_root:
                raise HostEnvironmentError(
                    "host state and environment roots must be distinct"
                )
            self.launcher = launcher.stage(self.environment_root)
            self.environment = isolated_environment(
                self.environment_root,
                venv_launcher=self.launcher.venv_launcher,
            )
            default_program = pathlib.Path(__file__).with_name("duckdb_action.py")
            self.action_program = regular_file(
                action_program or default_program, "Query DuckDB action program"
            )
            self.incompatible_artifact = None
            if incompatible_source is not None:
                assert incompatible_artifact_sha256 is not None
                assert incompatible_artifact_size is not None
                self.incompatible_artifact = stage_content_identified_file(
                    incompatible_source,
                    self.state_root / ".incompatible.duckdb_extension",
                    expected_size=incompatible_artifact_size,
                    expected_sha256=incompatible_artifact_sha256,
                    limit_bytes=incompatible_artifact_limit_bytes,
                )
        except (FileAdmissionError, HostEnvironmentError, LauncherError) as error:
            raise HostActionError(str(error)) from error
        self.row = row
        self.incompatible_artifact_sha256 = incompatible_artifact_sha256
        self.timeout_seconds = timeout_seconds
        self.output_limit_bytes = output_limit_bytes
        self.calls: list[tuple[str, str]] = []
        replacements = [
            (self.state_root, "<state-root>"),
            (self.environment_root, "<environment-root>"),
            (self.action_program.parent, "<query-root>"),
        ]
        if incompatible_source is not None:
            replacements.append(
                (incompatible_source.parent, "<incompatible-root>")
            )
        replacements.extend((path, "<diagnostic-root>") for path in diagnostic_roots)
        self.diagnostic_replacements = tuple(replacements)

    def run(self, action: str, state_id: str) -> HostObservation:
        """Run exactly one action in a fresh process over persistent state."""

        if action not in {
            "pre_install",
            "install",
            "repeat_install",
            "load_query",
            "incompatible",
        }:
            raise HostActionError("unknown Community host action")
        if action == "incompatible" and self.incompatible_artifact is None:
            raise HostActionError("incompatible action requires an explicit artifact")
        try:
            state = StateCapability.admit(self.state_root, state_id)
        except StateCapabilityError as error:
            raise HostActionError(str(error)) from error
        try:
            arguments = [
                action,
                "--database",
                state.child_database,
                "--extension-directory",
                state.child_extension_directory,
                "--state-directory-fd",
                str(state.descriptor),
            ]
            if action == "incompatible":
                assert self.incompatible_artifact is not None
                arguments.extend(
                    (
                        "--incompatible-artifact",
                        f"/dev/fd/{self.incompatible_artifact.descriptor}",
                    )
                )
            try:
                command = self.launcher.command(self.action_program, arguments)
            except LauncherError as error:
                raise HostActionError(str(error)) from error
            if "unsigned" in " ".join(command.arguments).lower():
                raise HostActionError("stock host command weakens signature policy")
            self.calls.append((action, state_id))
            try:
                completed = run_bounded_process(
                    command.arguments,
                    cwd=None,
                    environment=self.environment,
                    executable_path=command.executable,
                    inherited_descriptors=(
                        (
                            state.descriptor,
                            self.incompatible_artifact.descriptor,
                        )
                        if action == "incompatible"
                        and self.incompatible_artifact is not None
                        else (state.descriptor,)
                    ),
                    timeout_seconds=self.timeout_seconds,
                    output_limit_bytes=self.output_limit_bytes,
                )
            except subprocess.TimeoutExpired as error:
                raise HostActionError(
                    f"stock host action {action!r} timed out; "
                    "its process group was killed"
                ) from error
            except ProcessOutputLimitExceeded as error:
                raise HostActionError(
                    f"stock host action {action!r} exceeded its output limit; "
                    "its process group was killed"
                ) from error
            except ProcessBoundaryError as error:
                raise HostActionError(str(error)) from error
        finally:
            try:
                state.finish()
            except StateCapabilityError as error:
                raise HostActionError(str(error)) from error
        if completed.returncode != 0:
            raise HostActionError(
                f"stock host action {action!r} failed before a safe observation "
                f"with exit status {completed.returncode}"
            )
        try:
            observation = parse_observation(
                completed.stdout, self.row, self.diagnostic_replacements
            )
        except ProtocolError as error:
            raise HostActionError(str(error)) from error
        return observation

    def close(self) -> None:
        """Close the retained staged-artifact capability after all actions."""

        if self.incompatible_artifact is not None:
            self.incompatible_artifact.close()
