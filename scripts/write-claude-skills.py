#!/usr/bin/env python3
"""Mirror .agents/skills/*/SKILL.md into .claude/skills/ so Claude Code can
discover the same repository skills that scripts/validate-agent-assets.py
validates for Codex. .agents/skills/ remains the single source of truth;
this script only regenerates the Claude-facing copy.

Run after adding, removing, or editing a skill under .agents/skills/.
"""
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE_ROOT = ROOT / ".agents" / "skills"
DEST_ROOT = ROOT / ".claude" / "skills"


def main() -> int:
    source_names = {path.parent.name for path in SOURCE_ROOT.glob("*/SKILL.md")}
    if not source_names:
        print("no repository skills found under .agents/skills", file=sys.stderr)
        return 1

    DEST_ROOT.mkdir(parents=True, exist_ok=True)

    if DEST_ROOT.exists():
        for existing in sorted(DEST_ROOT.iterdir()):
            if existing.is_dir() and existing.name not in source_names:
                shutil.rmtree(existing)
                print(f"removed stale {existing.relative_to(ROOT)}")

    for name in sorted(source_names):
        source_file = SOURCE_ROOT / name / "SKILL.md"
        dest_dir = DEST_ROOT / name
        dest_dir.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source_file, dest_dir / "SKILL.md")

    print(f"wrote {len(source_names)} skill(s) to {DEST_ROOT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
