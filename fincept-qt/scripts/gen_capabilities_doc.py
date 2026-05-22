#!/usr/bin/env python3
"""Generate `docs/how-to/capabilities.md` from
`fincept-qt/resources/ai/capabilities.json`.

Run from the repo root:

    python3 fincept-qt/scripts/gen_capabilities_doc.py

The file the script writes is committed alongside this script.
Updating the JSON without re-running this script makes the docs
drift; CI should re-run + diff to catch that.

Stdlib-only.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
JSON_PATH = REPO_ROOT / "fincept-qt" / "resources" / "ai" / "capabilities.json"
OUT_PATH = REPO_ROOT / "docs" / "how-to" / "capabilities.md"

STATUS_GLYPH = {
    "supported": "✓",
    "partial": "◐",
    "planned": "…",
    "unsupported": "—",
}


def render(matrix: dict) -> str:
    runtimes: list[str] = list(matrix.get("runtimes", []))
    caps: list[dict] = list(matrix.get("capabilities", []))

    lines: list[str] = []
    lines.append("# AI capability matrix")
    lines.append("")
    lines.append(
        "Generated from "
        "[`fincept-qt/resources/ai/capabilities.json`]"
        "(../../fincept-qt/resources/ai/capabilities.json) by "
        "`fincept-qt/scripts/gen_capabilities_doc.py`.  Don't edit this "
        "file directly — change the JSON, re-run the generator, "
        "commit both.")
    lines.append("")
    lines.append("Glyphs:")
    for status, glyph in STATUS_GLYPH.items():
        lines.append(f"- **{glyph}** {status}")
    lines.append("")

    # ── Matrix table ────────────────────────────────────────────────
    header = ["Capability"] + [r.capitalize() for r in runtimes] + ["What it does"]
    lines.append("| " + " | ".join(header) + " |")
    lines.append("|" + "|".join(["---"] * len(header)) + "|")
    for c in caps:
        row = [f"**{c.get('label', c.get('id', '?'))}**"]
        for rt in runtimes:
            status = c.get(rt) or ""
            row.append(STATUS_GLYPH.get(status, "?"))
        desc = (c.get("description") or "").replace("|", "\\|")
        row.append(desc)
        lines.append("| " + " | ".join(row) + " |")
    lines.append("")

    # ── Per-capability notes ────────────────────────────────────────
    notes_caps = [c for c in caps if any(
        k.startswith("notes") and c.get(k) for k in c.keys())]
    if notes_caps:
        lines.append("## Notes")
        lines.append("")
        for c in notes_caps:
            lines.append(f"### {c.get('label', c.get('id'))}")
            lines.append("")
            for rt in runtimes:
                note = c.get(f"notes_{rt}")
                if note:
                    lines.append(f"- *{rt.capitalize()}*: {note}")
            generic = c.get("notes")
            if generic:
                lines.append(f"- {generic}")
            lines.append("")

    return "\n".join(lines) + "\n"


def main() -> int:
    if not JSON_PATH.exists():
        print(f"ERROR: {JSON_PATH} not found", file=sys.stderr)
        return 1
    matrix = json.loads(JSON_PATH.read_text(encoding="utf-8"))
    rendered = render(matrix)
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text(rendered, encoding="utf-8")
    print(f"Wrote {OUT_PATH.relative_to(REPO_ROOT)} ({len(rendered)} chars)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
