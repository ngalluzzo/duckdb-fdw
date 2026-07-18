#!/usr/bin/env python3
"""Stage an exact visible allowlist for sanitizer artifact upload."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys


SOURCE_NAMES = {
    "compile_commands.json",
    "duckdb_api.duckdb_extension",
    "envelope.json",
    "envelope.sha256",
    "manifest.json",
    "manifest.sha256",
    "sanitizer-flags.txt",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def regular_path(value: str, *, directory: bool, label: str) -> pathlib.Path:
    lexical = pathlib.Path(value).expanduser()
    if not lexical.is_absolute():
        lexical = pathlib.Path.cwd() / lexical
    require(not lexical.is_symlink(), f"{label} must not be a symlink leaf: {lexical}")
    path = lexical.resolve(strict=True)
    expected = path.is_dir() if directory else path.is_file()
    require(expected, f"{label} has the wrong file type: {path}")
    return path


def new_output(value: str) -> pathlib.Path:
    lexical = pathlib.Path(value).expanduser()
    if not lexical.is_absolute():
        lexical = pathlib.Path.cwd() / lexical
    require(not lexical.exists() and not lexical.is_symlink(), f"output already exists: {lexical}")
    nearest = lexical.parent
    missing: list[str] = []
    while not nearest.exists() and not nearest.is_symlink():
        missing.append(nearest.name)
        nearest = nearest.parent
    require(not nearest.is_symlink(), f"output parent must not be a symlink leaf: {nearest}")
    require(nearest.is_dir(), f"output parent is not a directory: {nearest}")
    canonical_parent = nearest.resolve(strict=True)
    for name in reversed(missing):
        canonical_parent /= name
    canonical_parent.mkdir(parents=True, exist_ok=True)
    return canonical_parent.resolve(strict=True) / lexical.name


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: stage-ci-evidence.py TAG_REPOSITORY SANITIZER_ROOT LOG OUTPUT_ROOT"
        )
    repository = regular_path(sys.argv[1], directory=True, label="tag repository")
    source = regular_path(sys.argv[2], directory=True, label="sanitizer root")
    log = regular_path(sys.argv[3], directory=False, label="sanitizer log")
    output = new_output(sys.argv[4])

    entries = set()
    for path in source.iterdir():
        require(not path.is_symlink(), f"sanitizer source contains a symlink: {path.name}")
        require(path.is_file(), f"sanitizer source contains a non-file: {path.name}")
        require(not path.name.startswith("."), f"sanitizer source contains a hidden file: {path.name}")
        entries.add(path.name)
    require(entries == SOURCE_NAMES, "sanitizer source inventory does not match the upload allowlist")

    subprocess.run(
        [
            sys.executable,
            "-I",
            str(repository / "scripts/verify-sanitizer-manifest.py"),
            str(repository),
            str(source / "manifest.json"),
            str(source / "manifest.sha256"),
            str(source / "duckdb_api.duckdb_extension"),
            str(source / "compile_commands.json"),
            str(source / "sanitizer-flags.txt"),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )

    output.mkdir(mode=0o755)
    sanitizer = output / "sanitizer"
    sanitizer.mkdir(mode=0o755)
    for name in sorted(SOURCE_NAMES):
        destination = sanitizer / name
        shutil.copyfile(source / name, destination)
        os.chmod(destination, 0o444)
    staged_log = output / "linux-amd64-sanitized.log"
    shutil.copyfile(log, staged_log)
    os.chmod(staged_log, 0o444)

    data_files = sorted(
        ["linux-amd64-sanitized.log", *(f"sanitizer/{name}" for name in SOURCE_NAMES)]
    )
    custody = {
        "files": {
            name: {
                "sha256": sha256(output / name),
                "size": (output / name).stat().st_size,
            }
            for name in data_files
        },
        "schema": "duckdb_api/ci-evidence-custody/v1",
    }
    custody_path = output / "custody.json"
    custody_path.write_text(json.dumps(custody, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    custody_anchor = output / "custody.sha256"
    custody_anchor.write_text(f"{sha256(custody_path)}  custody.json\n", encoding="utf-8")
    os.chmod(custody_path, 0o444)
    os.chmod(custody_anchor, 0o444)

    subprocess.run(
        [
            sys.executable,
            "-I",
            str(pathlib.Path(__file__).with_name("verify-ci-evidence-roundtrip.py")),
            "--staged-only",
            str(repository),
            str(output),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
