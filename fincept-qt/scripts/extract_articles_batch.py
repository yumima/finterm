"""
extract_articles_batch.py — pull clean body text from several news URLs at once.

Used by NewsService::extract_article_bodies to enrich the top stories of the
AI news brief. The brief needs the opening paragraphs of ~8 articles before it
can write a word, and doing that as 8 separate extract_article.py invocations
was the wrong shape twice over:

  * PythonRunner caps concurrency at 3 processes, so 8 URLs serialised into
    three waves of process startup.
  * Every one of those processes pays the trafilatura/lxml import cost, which
    is comparable to the fetch it is there to support.

One process, one import, all URLs fetched on a thread pool — the wall time is
the slowest single fetch rather than the sum of three waves of them.

The extraction itself is not reimplemented here; this imports extract_article
so the two paths cannot drift. A body pulled for the reading pane and a body
pulled for the brief are byte-identical, which matters because they share a
cache slot.

Output: one JSON object on stdout, {"results": [{url, success, title, text}]}.
Order matches the input; a failed URL still gets an entry so the caller can
tell "extraction failed" from "never asked".

Usage:
    python extract_articles_batch.py <url> [<url> ...]
"""

from __future__ import annotations

import json
import sys
from concurrent.futures import ThreadPoolExecutor

import extract_article

# Fetches are IO-bound and independent, so the pool can be wider than the core
# count. Bounded anyway: each worker holds a socket and a parsed DOM, and the
# caller only ever asks for a handful of URLs.
MAX_WORKERS = 8


def _one(url: str) -> dict:
    """Never raises. A single bad URL must not cost the whole batch — the
    brief is written from whatever came back, and a story with no body still
    contributes its headline and RSS blurb."""
    try:
        result = extract_article.extract(url)
    except Exception as exc:  # noqa: BLE001 — deliberate catch-all, see above
        return {"url": url, "success": False, "error": f"{type(exc).__name__}: {exc}"}
    return {
        "url": url,
        "success": bool(result.get("success")),
        "title": result.get("title", ""),
        "text": result.get("text", ""),
        "error": result.get("error", ""),
    }


def main() -> int:
    urls = sys.argv[1:]
    if not urls:
        sys.stdout.write(json.dumps({"results": []}))
        return 0
    with ThreadPoolExecutor(max_workers=min(MAX_WORKERS, len(urls))) as pool:
        results = list(pool.map(_one, urls))
    sys.stdout.write(json.dumps({"results": results}, ensure_ascii=False))
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
