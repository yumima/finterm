#!/usr/bin/env python3
"""vendor_skills_diff.py — compare finterm's vendored SKILL.md files
against the upstream `anthropics/financial-services` source.

Usage:
    vendor_skills_diff.py --upstream /path/to/anthropics/financial-services

Output:
    - `vendored`     — SKILL.md files finterm carries that exist upstream
    - `diverged`     — vendored files whose content differs from upstream
    - `new_upstream` — SKILL.md files upstream has that finterm doesn't
    - `local_only`   — SKILL.md files finterm carries that don't exist
                      upstream (finterm-original methodology)

Exit codes:
    0  no divergence + no new upstream content (or --strict not set)
    1  divergence or new upstream content detected (with --strict)
    2  setup error (missing upstream path, etc.)

Wire into PR review: `--strict` makes drift a CI failure; without it,
the script is informational.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

# The skills shipped beside this script, in the repo and in an install alike.
LOCAL_SKILLS = Path(__file__).resolve().parents[1] / "finagent_core" / "skills"


def hash_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def collect_skills(root: Path) -> dict[str, Path]:
    """Walk a skills tree, return a {name → SKILL.md path} dict where
    `name` is the directory containing the SKILL.md file.

    Empty / missing root returns {} — the caller surfaces that
    differently from a real diff."""
    out: dict[str, Path] = {}
    if not root.exists():
        return out
    for skill_md in root.rglob("SKILL.md"):
        out[skill_md.parent.name] = skill_md
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--upstream", required=True,
        help="Path to a clone of github.com/anthropics/financial-services")
    parser.add_argument(
        "--local", default=str(LOCAL_SKILLS),
        help="The skills tree to compare (default: the one beside this script)")
    parser.add_argument(
        "--overlay", default="",
        help="The user's own copies, laid out as --local: one there is compared instead")
    parser.add_argument(
        "--strict", action="store_true",
        help="Exit non-zero on any divergence or new-upstream content")
    parser.add_argument(
        "--json", action="store_true",
        help="Emit JSON instead of human-readable text")
    args = parser.parse_args()

    upstream_root = Path(args.upstream).expanduser().resolve()
    if not upstream_root.exists():
        print(f"ERROR: --upstream path does not exist: {upstream_root}",
              file=sys.stderr)
        return 2

    # Upstream layout: `skills/<vertical>/<name>/SKILL.md` typically.
    # We accept either the repo root (we hunt for `skills/`) or a
    # direct skills directory.
    upstream_skills_root = upstream_root / "skills"
    if not upstream_skills_root.exists():
        upstream_skills_root = upstream_root

    local_root = Path(args.local).expanduser().resolve()
    if not local_root.is_dir():
        print(f"ERROR: --local path does not exist: {local_root}", file=sys.stderr)
        return 2
    overlay = Path(args.overlay).expanduser() if args.overlay else None
    local = collect_skills(local_root)
    upstream = collect_skills(upstream_skills_root)

    vendored: list[str] = []
    diverged: list[dict] = []
    new_upstream: list[str] = []
    local_only: list[str] = []

    for name, local_path in sorted(local.items()):
        up_path = upstream.get(name)
        if up_path is None:
            local_only.append(name)
            continue
        rel = local_path.relative_to(local_root)
        if overlay is not None and (overlay / rel).is_file():
            local_path = overlay / rel
        local_hash = hash_file(local_path)
        up_hash = hash_file(up_path)
        if local_hash == up_hash:
            vendored.append(name)
        else:
            diverged.append({
                "name": name,
                "local_sha": local_hash[:12],
                "upstream_sha": up_hash[:12],
                "rel_path": rel.as_posix(),
                "local_path": str(local_path),
                "upstream_path": str(up_path),
            })

    for name in sorted(upstream.keys()):
        if name not in local:
            new_upstream.append(name)

    report = {
        "upstream_root": str(upstream_root),
        "local_root": str(local_root),
        "vendored": vendored,
        "diverged": diverged,
        "new_upstream": new_upstream,
        "local_only": local_only,
    }

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"Upstream root: {upstream_root}")
        print(f"Local root:    {local_root}")
        print()
        print(f"Vendored & unchanged ({len(vendored)}):")
        for name in vendored:
            print(f"  ✓ {name}")
        print()
        print(f"Vendored but diverged ({len(diverged)}):")
        for entry in diverged:
            print(f"  ◆ {entry['name']}  local={entry['local_sha']} upstream={entry['upstream_sha']}")
        print()
        print(f"New upstream not yet vendored ({len(new_upstream)}):")
        for name in new_upstream:
            print(f"  + {name}")
        print()
        print(f"Local-only (finterm-original) ({len(local_only)}):")
        for name in local_only:
            print(f"  ★ {name}")

    if args.strict and (diverged or new_upstream):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
