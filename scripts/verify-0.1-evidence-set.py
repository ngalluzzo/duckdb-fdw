#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import pathlib
import sys


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    if len(sys.argv) != 6:
        raise SystemExit(
            "usage: verify-0.1-evidence-set.py PRODUCT_MANIFEST PRODUCT_ANCHOR "
            "SANITIZER_MANIFEST SANITIZER_ANCHOR REPOSITORY"
        )
    product_path = pathlib.Path(sys.argv[1]).resolve(strict=True)
    product_anchor = pathlib.Path(sys.argv[2]).resolve(strict=True)
    sanitizer_path = pathlib.Path(sys.argv[3]).resolve(strict=True)
    sanitizer_anchor = pathlib.Path(sys.argv[4]).resolve(strict=True)
    repository = pathlib.Path(sys.argv[5]).resolve(strict=True)
    if sha256(product_path) != product_anchor.read_text().split()[0]:
        raise AssertionError("product manifest anchor mismatch")
    if sha256(sanitizer_path) != sanitizer_anchor.read_text().split()[0]:
        raise AssertionError("sanitizer manifest anchor mismatch")

    product = json.loads(product_path.read_text())
    sanitizer = json.loads(sanitizer_path.read_text())
    pins = json.loads((repository / "release/0.1.0/pins.json").read_text())
    if product["cell"] != "osx_arm64":
        raise AssertionError("evidence set is missing the product cell")
    if sanitizer["cell"] != pins["sanitizer_cell"]["name"]:
        raise AssertionError("evidence set is missing the sanitizer cell")
    if product["source"]["commit"] != sanitizer["source"]["commit"]:
        raise AssertionError("product and sanitizer evidence use different source commits")
    if product["content"] != sanitizer["content"]:
        raise AssertionError("product and sanitizer evidence use different content identities")
    if product["dependencies"] != pins["dependencies"] or sanitizer[
        "dependencies"
    ] != pins["dependencies"]:
        raise AssertionError("evidence-set dependency identities drifted")
    if sanitizer["base_image"] != pins["sanitizer_cell"]["base_image"]:
        raise AssertionError("sanitizer base-image identity drifted")
    if sanitizer["toolchain"]["architecture"] != "x86_64":
        raise AssertionError("sanitizer evidence is not native Linux amd64")
    if sanitizer["toolchain"]["cxx_standard"] != "11" or sanitizer[
        "toolchain"
    ]["sanitizers"] != {"address": True, "undefined": True}:
        raise AssertionError("sanitizer evidence does not attest ASan, UBSan, and C++11")
    if not sanitizer["source"].get("clean"):
        raise AssertionError("sanitizer evidence does not attest clean source")
    print("0.1.0 product and sanitizer evidence are bound to one source identity")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
