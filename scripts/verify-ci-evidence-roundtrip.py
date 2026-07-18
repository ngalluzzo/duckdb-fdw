#!/usr/bin/env python3
"""Verify staged or downloaded sanitizer evidence byte-for-byte."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import subprocess
import sys


DATA_FILES = {
    "linux-amd64-sanitized.log",
    "sanitizer/compile_commands.json",
    "sanitizer/duckdb_api.duckdb_extension",
    "sanitizer/envelope.json",
    "sanitizer/envelope.sha256",
    "sanitizer/manifest.json",
    "sanitizer/manifest.sha256",
    "sanitizer/sanitizer-flags.txt",
}
ALL_FILES = DATA_FILES | {"custody.json", "custody.sha256"}
TRUST_PATH = (
    pathlib.Path(__file__).resolve().parent.parent
    / "experiments/repeatable-installation/enablement/trusted-release.json"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git(repository: pathlib.Path, revision: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(repository), "rev-parse", revision], text=True
    ).strip()


def regular_root(value: str, label: str) -> pathlib.Path:
    lexical = pathlib.Path(value).expanduser()
    if not lexical.is_absolute():
        lexical = pathlib.Path.cwd() / lexical
    require(not lexical.is_symlink(), f"{label} must not be a symlink leaf: {lexical}")
    path = lexical.resolve(strict=True)
    require(path.is_dir(), f"{label} is not a directory: {path}")
    return path


def relative_files(root: pathlib.Path) -> set[str]:
    result: set[str] = set()
    for path in root.rglob("*"):
        relative = path.relative_to(root)
        require(not path.is_symlink(), f"custody contains a symlink: {relative}")
        require(
            all(not part.startswith(".") for part in relative.parts),
            f"custody contains a hidden path: {relative}",
        )
        if path.is_dir():
            continue
        require(path.is_file(), f"custody contains a non-regular file: {relative}")
        result.add(relative.as_posix())
    return result


def verify_sha256_anchor(
    anchor_path: pathlib.Path,
    target_path: pathlib.Path,
    expected_filename: str,
    label: str,
) -> None:
    anchor = re.fullmatch(
        r"([0-9a-f]{64})  ([^\x00\r\n]+)\n",
        anchor_path.read_text(encoding="utf-8"),
    )
    require(anchor is not None, f"{label} anchor syntax drifted")
    assert anchor is not None
    require(
        pathlib.PurePath(anchor.group(2)).name == expected_filename,
        f"{label} anchor target drifted",
    )
    require(anchor.group(1) == sha256(target_path), f"{label} anchor mismatch")


def verify_envelope(repository: pathlib.Path, sanitizer: pathlib.Path) -> None:
    envelope_path = sanitizer / "envelope.json"
    verify_sha256_anchor(
        sanitizer / "envelope.sha256",
        envelope_path,
        "envelope.json",
        "sanitizer envelope",
    )
    envelope = json.loads(envelope_path.read_text(encoding="utf-8"))
    pins = json.loads((repository / "release/0.1.0/pins.json").read_text(encoding="utf-8"))
    trust = json.loads(TRUST_PATH.read_text(encoding="utf-8"))
    require(
        isinstance(trust, dict)
        and trust.get("schema") == "duckdb_api/installability-trusted-release/v1"
        and isinstance(trust.get("source"), dict),
        "tracked release trust record drifted",
    )
    trusted_source = trust["source"]
    require(
        set(trusted_source) == {"commit", "tag", "tag_object", "tree"},
        "tracked release source identity drifted",
    )
    require(pins["project"]["tag"] == trusted_source["tag"], "sanitizer tag pin drifted")
    require(
        git(repository, "HEAD") == trusted_source["commit"]
        and git(repository, "HEAD^{tree}") == trusted_source["tree"],
        "sanitizer verification checkout differs from the tracked release source",
    )
    expected_source = {
        "commit": trusted_source["commit"],
        "tag": trusted_source["tag"],
        "tree": trusted_source["tree"],
    }
    require(
        envelope.get("schema") == "duckdb_api/sanitizer-launcher-envelope/v1",
        "sanitizer envelope schema drifted",
    )
    require(envelope.get("cell") == pins["sanitizer_cell"]["name"], "cell drifted")
    require(envelope.get("source") == expected_source, "sanitizer envelope source drifted")

    executor = envelope.get("executor")
    require(isinstance(executor, dict), "sanitizer envelope executor is absent")
    require(
        executor.get("client_host_os") == "Linux"
        and executor.get("client_host_architecture") == "x86_64"
        and executor.get("docker_context") == "default"
        and str(executor.get("docker_endpoint", "")).startswith("unix://")
        and executor.get("daemon_os") == "linux"
        and executor.get("daemon_architecture") == "x86_64"
        and bool(executor.get("daemon_id"))
        and executor.get("container_platform") == pins["sanitizer_cell"]["platform"],
        "sanitizer executor identity drifted",
    )
    image = envelope.get("image")
    require(isinstance(image, dict), "sanitizer envelope image is absent")
    required_image = pins["sanitizer_cell"]["base_image"]
    required_digest = required_image.split("@", 1)[1]
    require(
        image.get("requested") == required_image
        and image.get("os") == "linux"
        and image.get("architecture") == "amd64"
        and any(
            str(value).endswith(f"@{required_digest}")
            for value in image.get("repo_digests", [])
        ),
        "sanitizer image identity drifted",
    )
    require(
        envelope.get("inner_evidence")
        == {
            "artifact_sha256": sha256(sanitizer / "duckdb_api.duckdb_extension"),
            "compile_commands_sha256": sha256(sanitizer / "compile_commands.json"),
            "manifest_sha256": sha256(sanitizer / "manifest.json"),
            "sanitizer_flags_report_sha256": sha256(sanitizer / "sanitizer-flags.txt"),
        },
        "sanitizer envelope does not bind the retained bytes",
    )


def verify(repository: pathlib.Path, root: pathlib.Path, label: str) -> None:
    require(relative_files(root) == ALL_FILES, f"{label} custody inventory mismatch")
    custody_path = root / "custody.json"
    anchor = re.fullmatch(
        r"([0-9a-f]{64})  custody\.json\n",
        (root / "custody.sha256").read_text(encoding="utf-8"),
    )
    require(anchor is not None, f"{label} custody anchor syntax drifted")
    assert anchor is not None
    require(anchor.group(1) == sha256(custody_path), f"{label} custody anchor mismatch")
    custody = json.loads(custody_path.read_text(encoding="utf-8"))
    require(custody.get("schema") == "duckdb_api/ci-evidence-custody/v1", "schema drifted")
    files = custody.get("files")
    require(isinstance(files, dict) and set(files) == DATA_FILES, "custody file map drifted")
    for name in sorted(DATA_FILES):
        path = root / name
        require(
            files[name] == {"sha256": sha256(path), "size": path.stat().st_size},
            f"custody identity drifted for {name}",
        )

    sanitizer = root / "sanitizer"
    verify_sha256_anchor(
        sanitizer / "manifest.sha256",
        sanitizer / "manifest.json",
        "manifest.json",
        "sanitizer manifest",
    )
    subprocess.run(
        [
            sys.executable,
            "-I",
            str(repository / "scripts/verify-sanitizer-manifest.py"),
            str(repository),
            str(sanitizer / "manifest.json"),
            str(sanitizer / "manifest.sha256"),
            str(sanitizer / "duckdb_api.duckdb_extension"),
            str(sanitizer / "compile_commands.json"),
            str(sanitizer / "sanitizer-flags.txt"),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    verify_envelope(repository, sanitizer)


def same_bytes(left: pathlib.Path, right: pathlib.Path) -> bool:
    if left.stat().st_size != right.stat().st_size:
        return False
    with left.open("rb") as left_file, right.open("rb") as right_file:
        while True:
            left_block = left_file.read(1024 * 1024)
            right_block = right_file.read(1024 * 1024)
            if left_block != right_block:
                return False
            if not left_block:
                return True


def compare_roots(staged: pathlib.Path, downloaded: pathlib.Path) -> None:
    require(relative_files(staged) == ALL_FILES, "staged custody inventory mismatch")
    require(relative_files(downloaded) == ALL_FILES, "downloaded custody inventory mismatch")
    for name in sorted(ALL_FILES):
        require(
            same_bytes(staged / name, downloaded / name),
            f"downloaded custody bytes differ from staged input: {name}",
        )


def main() -> int:
    if len(sys.argv) == 4 and sys.argv[1] == "--staged-only":
        repository = regular_root(sys.argv[2], "tag repository")
        staged = regular_root(sys.argv[3], "staged custody root")
        verify(repository, staged, "staged")
        print("staged CI evidence custody and inner sanitizer verification passed")
        return 0
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: verify-ci-evidence-roundtrip.py TAG_REPOSITORY STAGED_ROOT DOWNLOADED_ROOT"
        )
    repository = regular_root(sys.argv[1], "tag repository")
    staged = regular_root(sys.argv[2], "staged custody root")
    downloaded = regular_root(sys.argv[3], "downloaded custody root")
    verify(repository, staged, "staged")
    verify(repository, downloaded, "downloaded")
    compare_roots(staged, downloaded)
    print("downloaded CI evidence exactly matches staged verified bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
