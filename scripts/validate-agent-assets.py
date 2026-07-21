#!/usr/bin/env python3
"""Validate AGENTS.md and every repository skill's frontmatter/metadata.

Ports the former scripts/validate-agent-assets.rb to stdlib-only Python.
Skill frontmatter and openai.yaml metadata are both a narrow, known-flat
shape (single-line ``key: value`` pairs, no nesting beyond one level, no
quoting, no anchors/aliases), so this hand-rolled parser accepts exactly
that shape rather than adding a YAML dependency for one file format.
"""

import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FRONTMATTER_RE = re.compile(r"\A---\n(.*?)\n---\n(.+)\Z", re.DOTALL)
SKILL_NAME_RE = re.compile(r"\A[a-z0-9-]{1,63}\Z")
REFERENCED_SKILL_RE = re.compile(r"\$([a-z0-9-]+)")
TRAILING_WHITESPACE_RE = re.compile(r"[ \t]+(?:\r?\n)?\Z")


def parse_flat_mapping(text):
    """Parse a strict flat ``key: value`` mapping: one key per line, plain
    unquoted scalar values, no nesting. Returns None if the text is not
    exactly this shape."""
    result = {}
    for line in text.split("\n"):
        if line.strip() == "":
            continue
        match = re.match(r"\A([A-Za-z_][A-Za-z0-9_]*): (.*)\Z", line)
        if not match:
            return None
        key, value = match.group(1), match.group(2)
        if key in result:
            return None
        result[key] = value
    return result


def parse_interface_mapping(text):
    """Parse the one-level-nested ``interface:`` mapping used by
    agents/openai.yaml: a top-level ``interface:`` key followed by indented
    ``key: "value"`` lines with double-quoted scalar values."""
    lines = text.split("\n")
    if not lines or lines[0].strip() != "interface:":
        return None
    interface = {}
    for line in lines[1:]:
        if line.strip() == "":
            continue
        match = re.match(r'\A  ([A-Za-z_][A-Za-z0-9_]*): "(.*)"\Z', line)
        if not match:
            return None
        key, value = match.group(1), match.group(2)
        if key in interface:
            return None
        interface[key] = value
    return {"interface": interface}


def main():
    failures = []
    asset_paths = [os.path.abspath(__file__)]

    agents_path = os.path.join(ROOT, "AGENTS.md")
    if not (os.path.isfile(agents_path) and os.path.getsize(agents_path) > 0):
        failures.append("AGENTS.md is missing or empty")
    if os.path.isfile(agents_path):
        asset_paths.append(agents_path)

    skill_paths = sorted(glob.glob(os.path.join(ROOT, ".agents", "skills", "*", "SKILL.md")))
    if not skill_paths:
        failures.append("no repository skills found")
    discovered_skills = []

    for skill_path in skill_paths:
        asset_paths.append(skill_path)
        skill_dir = os.path.dirname(skill_path)
        expected_name = os.path.basename(skill_dir)
        with open(skill_path, "r", encoding="utf-8") as handle:
            raw = handle.read()
        match = FRONTMATTER_RE.match(raw)

        if not match:
            failures.append(f"{skill_path}: malformed or empty frontmatter/body")
            continue

        frontmatter = parse_flat_mapping(match.group(1))
        if frontmatter is None:
            failures.append(f"{skill_path}: invalid YAML")
            continue

        if sorted(frontmatter.keys()) != ["description", "name"]:
            failures.append(f"{skill_path}: frontmatter must contain only name and description")
            continue

        name = frontmatter.get("name")
        description = frontmatter.get("description")
        if isinstance(name, str):
            discovered_skills.append(name)
        if name != expected_name:
            failures.append(f"{skill_path}: name must match {expected_name}")
        if not (isinstance(name, str) and SKILL_NAME_RE.match(name)):
            failures.append(f"{skill_path}: invalid skill name")
        if not (isinstance(description, str) and description.strip() != ""):
            failures.append(f"{skill_path}: description is empty")
        if re.search(r"\bTODO\b|\[TODO", raw):
            failures.append(f"{skill_path}: placeholder text remains")

        metadata_path = os.path.join(skill_dir, "agents", "openai.yaml")
        if not os.path.isfile(metadata_path):
            failures.append(f"{metadata_path}: missing")
            continue
        asset_paths.append(metadata_path)

        with open(metadata_path, "r", encoding="utf-8") as handle:
            metadata_text = handle.read()
        metadata = parse_interface_mapping(metadata_text.rstrip("\n"))
        if metadata is None:
            failures.append(f"{metadata_path}: invalid YAML")
            continue

        interface = metadata.get("interface")
        if not isinstance(interface, dict):
            failures.append(f"{metadata_path}: interface mapping is missing")
            continue

        display_name = interface.get("display_name")
        short_description = interface.get("short_description")
        default_prompt = interface.get("default_prompt")
        if not (isinstance(display_name, str) and display_name != ""):
            failures.append(f"{metadata_path}: display_name is missing")
        if not (isinstance(short_description, str) and 25 <= len(short_description) <= 64):
            failures.append(f"{metadata_path}: short_description must be 25-64 characters")
        if not (isinstance(default_prompt, str) and f"${expected_name}" in default_prompt):
            failures.append(f"{metadata_path}: default_prompt must mention ${expected_name}")

    if os.path.isfile(agents_path):
        with open(agents_path, "r", encoding="utf-8") as handle:
            agents_text = handle.read()
        required_skills = []
        for found in REFERENCED_SKILL_RE.findall(agents_text):
            if found not in required_skills:
                required_skills.append(found)
        for name in required_skills:
            if name not in discovered_skills:
                failures.append(f"AGENTS.md references missing repository skill ${name}")

    for path in dict.fromkeys(asset_paths):
        with open(path, "r", encoding="utf-8") as handle:
            for number, line in enumerate(handle, start=1):
                if TRAILING_WHITESPACE_RE.search(line):
                    failures.append(f"{path}:{number}: trailing whitespace")

    if failures:
        sys.stderr.write("\n".join(failures) + "\n")
        return 1

    print(f"Validated AGENTS.md and {len(skill_paths)} repository skills.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
