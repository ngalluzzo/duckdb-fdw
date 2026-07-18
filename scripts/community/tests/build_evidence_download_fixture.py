"""Exact local artifact and log downloads for build evidence tests."""

from __future__ import annotations

import pathlib


def write_downloads(
    root: pathlib.Path,
) -> tuple[pathlib.Path, pathlib.Path, bytes, dict[int, bytes]]:
    artifacts_root = root / "artifacts"
    logs_root = root / "logs"
    artifacts_root.mkdir()
    logs_root.mkdir()
    artifact_bytes = b"synthetic Community artifact archive\n"
    (artifacts_root / "linux-amd64.zip").write_bytes(artifact_bytes)
    log_bytes = {
        job_id: f"complete log for {job_id}\n".encode()
        for job_id in (101, 102, 103)
    }
    for job_id, payload in log_bytes.items():
        (logs_root / f"job-{job_id}.log").write_bytes(payload)
    return artifacts_root, logs_root, artifact_bytes, log_bytes
