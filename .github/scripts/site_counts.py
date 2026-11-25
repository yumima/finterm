#!/usr/bin/env python3
"""Download counts for the site's counter, as of this build (site/_data/counts.json).

    site_counts.py DOWNLOADS.json RELEASES.json OUT.json

`total` is `kept` (releases GitHub no longer lists) plus the listed ones. The page
starts from it and replaces it with GitHub's current figures. `sum` is what it shows:
the downloads and `clones`, a figure GitHub gives only the owner (kept in
DOWNLOADS.json).
"""

import json
import sys
from pathlib import Path

from keep_download_counts import ENDS, counts


def site_counts(data: dict, releases: list) -> dict:
    earlier = data.get("earlier_releases") or {}
    kept = {os: sum(e[os] for e in earlier.values()) for os in ENDS}
    live = [counts(r["assets"]) for r in releases if r["tag_name"] not in earlier]
    total = {os: kept[os] + sum(c[os] for c in live) for os in ENDS}
    return {"kept": kept, "kept_tags": list(earlier), "total": total, "clones": data.get("clones", 0),
            "sum": data.get("clones", 0) + sum(total.values())}


if __name__ == "__main__":
    downloads, releases, out = map(Path, sys.argv[1:4])
    result = site_counts(json.loads(downloads.read_text()), json.loads(releases.read_text()))
    out.write_text(json.dumps(result) + "\n")
