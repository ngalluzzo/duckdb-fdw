#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import sys


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(repository: pathlib.Path, revision: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(repository), "rev-parse", revision], text=True
    ).strip()


def main() -> int:
    if len(sys.argv) != 7:
        raise SystemExit(
            "usage: verify-sanitizer-manifest.py REPOSITORY MANIFEST ANCHOR "
            "ARTIFACT COMPILE_COMMANDS FLAGS_REPORT"
        )
    repository = pathlib.Path(sys.argv[1]).resolve(strict=True)
    manifest_path = pathlib.Path(sys.argv[2]).resolve(strict=True)
    anchor = pathlib.Path(sys.argv[3]).resolve(strict=True)
    artifact = pathlib.Path(sys.argv[4]).resolve(strict=True)
    compile_commands = pathlib.Path(sys.argv[5]).resolve(strict=True)
    flags_report = pathlib.Path(sys.argv[6]).resolve(strict=True)
    if sha256(manifest_path) != anchor.read_text().split()[0]:
        raise AssertionError("sanitizer manifest anchor mismatch")

    manifest = json.loads(manifest_path.read_text())
    pins = json.loads((repository / "release/0.1.0/pins.json").read_text())
    tag = pins["project"]["tag"]
    if manifest["schema"] != "duckdb_api/release-evidence/v1":
        raise AssertionError("sanitizer manifest schema drifted")
    if manifest["cell"] != pins["sanitizer_cell"]["name"]:
        raise AssertionError("sanitizer cell identity drifted")
    tag_commit = git(repository, f"{tag}^{{commit}}")
    tag_tree = git(repository, f"{tag}^{{tree}}")
    if git(repository, "HEAD") != tag_commit or git(repository, "HEAD^{tree}") != tag_tree:
        raise AssertionError("sanitizer verification repository is not at the canonical tag")
    if subprocess.check_output(
        [
            "git",
            "-C",
            str(repository),
            "status",
            "--porcelain",
            "--untracked-files=all",
        ],
        text=True,
    ):
        raise AssertionError("sanitizer verification repository is not clean")
    if manifest["source"] != {
        "commit": tag_commit,
        "tree": tag_tree,
        "tag": tag,
        "clean": True,
    }:
        raise AssertionError("sanitizer source identity drifted")
    if manifest["dependencies"] != pins["dependencies"]:
        raise AssertionError("sanitizer dependency identities drifted")
    if manifest["content"] != {
        "compiled_connector_sha256": pins["identities"]["compiled_connector_sha256"],
        "fixture_sha256": pins["identities"]["fixture_sha256"],
    }:
        raise AssertionError("sanitizer content identities drifted")
    if manifest["artifact"] != {
        "filename": artifact.name,
        "sha256": sha256(artifact),
        "size": artifact.stat().st_size,
    }:
        raise AssertionError("sanitizer artifact identity drifted")
    if manifest["sanitizer_compile_commands"] != {
        "filename": compile_commands.name,
        "sha256": sha256(compile_commands),
    }:
        raise AssertionError("retained sanitizer compile database drifted")
    if manifest["sanitizer_flags_report"] != {
        "filename": flags_report.name,
        "sha256": sha256(flags_report),
    }:
        raise AssertionError("retained sanitizer flags report drifted")
    toolchain = manifest["toolchain"]
    if toolchain["architecture"] != "x86_64":
        raise AssertionError("sanitizer evidence is not native Linux amd64")
    if toolchain["cxx_standard"] != "11" or toolchain["sanitizers"] != {
        "address": True,
        "undefined": True,
    }:
        raise AssertionError("sanitizer toolchain declaration drifted")
    subprocess.run(
        [
            sys.executable,
            "-I",
            str(repository / "scripts/verify-sanitizer-flags.py"),
            str(compile_commands),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    expected_report = "production extension objects contain ASan, UBSan, and C++11 flags\n"
    if flags_report.read_text() != expected_report:
        raise AssertionError("sanitizer flags report content drifted")
    print("sanitizer manifest and retained compile evidence verification passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
