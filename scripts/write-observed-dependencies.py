#!/usr/bin/env python3

from __future__ import annotations

import json
import pathlib
import subprocess
import sys


def git(repository: pathlib.Path, revision: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(repository), "rev-parse", revision], text=True
    ).strip()


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: write-observed-dependencies.py REPOSITORY TEMPLATE_ROOT OUTPUT"
        )
    repository = pathlib.Path(sys.argv[1]).resolve(strict=True)
    template = pathlib.Path(sys.argv[2]).resolve(strict=True)
    output = pathlib.Path(sys.argv[3]).resolve()
    pins = json.loads((repository / "release/0.1.0/pins.json").read_text())
    observed = json.loads(json.dumps(pins["dependencies"]))
    roots = {
        "extension_template": template,
        "duckdb": template / "duckdb",
        "extension_ci_tools": template / "extension-ci-tools",
    }
    for name, root in roots.items():
        observed[name]["commit"] = git(root, "HEAD")
        observed[name]["tree"] = git(root, "HEAD^{tree}")
    if observed != pins["dependencies"]:
        raise AssertionError(
            f"observed build dependencies do not match release pins: {observed!r}"
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(observed, indent=2, sort_keys=True) + "\n")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
