#!/usr/bin/env python3
"""Keep the download counts GitHub deletes with a release (site/_data/downloads.json).

    keep_download_counts.py FILE RELEASES.json CURRENT_TAG [REPLACED KEY]

Kept are the releases published before the current one, which the purge deletes.
REPLACED (JSON) is what the files of the current tag had before they were replaced.
The site's counter (site/_includes/counter.html) adds these up, and leaves a
kept release out of the live ones: the two overlap until it is deleted.
"""

import json
import sys
from pathlib import Path

ENDS = {"linux": "-setup.run", "windows": "-setup.exe", "macos": "-setup.dmg"}


def counts(assets: list) -> dict:
    return {os: sum(a["download_count"] for a in assets if a["name"].endswith(end)) for os, end in ENDS.items()}


def keep(data: dict, releases: list, current: str, replaced: dict | None = None, key: str = "") -> dict:
    kept = dict(data.get("earlier_releases") or {})
    now = next((r for r in releases if r["tag_name"] == current), None)
    for r in releases if now else []:
        if r["tag_name"] != current and r.get("published_at") and r["published_at"] < now["published_at"]:
            kept[r["tag_name"]] = counts(r["assets"])
    if replaced and key and sum(replaced.values()):
        kept[key] = replaced
    return {**data, "earlier_releases": kept}


if __name__ == "__main__":
    f = Path(sys.argv[1])
    rest = sys.argv[4:]
    out = keep(json.loads(f.read_text()), json.loads(Path(sys.argv[2]).read_text()), sys.argv[3],
               json.loads(rest[0] or "null") if rest else None, rest[1] if len(rest) > 1 else "")
    f.write_text(json.dumps(out, indent=2) + "\n")
