"""
extract_article.py — pull clean body text from a news URL.

Used by NewsService::extract_article_body to populate the ARTICLE section
of NewsDetailPanel below the action buttons. Stripped of nav, ads,
related-story boxes, comment widgets, and other boilerplate.

Pipeline:
  1. Fetch the URL (requests, short timeout, browser-like UA so a few
     publishers don't 403 the default `python-requests/2.x` agent).
  2. Run trafilatura on the HTML — best-in-class boilerplate stripper.
  3. Fall back to readability-lxml if trafilatura is unavailable or
     produces an empty result (some paywall pages defeat trafilatura
     but readability still gets the first few paragraphs).
  4. Last resort: BeautifulSoup paragraph aggregation.

Output: a single JSON object on stdout so PythonRunner can parse it.

Usage:
    python extract_article.py <url>
"""

from __future__ import annotations

import json
import re
import sys
from typing import Optional


def _emit(payload: dict) -> None:
    """Write the JSON payload and exit. PythonRunner reads stdout only."""
    sys.stdout.write(json.dumps(payload, ensure_ascii=False))
    sys.stdout.flush()


def _fetch_html(url: str, timeout_s: int = 12) -> Optional[str]:
    """Fetch raw HTML. Browser-like UA — `python-requests` gets 403 from
    Bloomberg, WSJ, FT, and a handful of others."""
    try:
        import requests
    except ImportError:
        return None
    headers = {
        "User-Agent": (
            "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
        ),
        "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
        "Accept-Language": "en-US,en;q=0.9",
    }
    try:
        r = requests.get(url, headers=headers, timeout=timeout_s, allow_redirects=True)
        if r.status_code >= 400:
            return None
        # requests guesses encoding from Content-Type; for sites that lie
        # about UTF-8 in the header we trust apparent_encoding more.
        if r.encoding and r.encoding.lower() == "iso-8859-1":
            r.encoding = r.apparent_encoding or r.encoding
        return r.text
    except Exception:
        return None


def _via_trafilatura(html: str, url: str) -> Optional[dict]:
    try:
        import trafilatura
    except ImportError:
        return None
    try:
        text = trafilatura.extract(
            html,
            url=url,
            include_comments=False,
            include_tables=False,
            include_images=False,
            favor_recall=True,  # err on inclusion over over-stripping
            output_format="txt",
        )
        if not text or len(text.strip()) < 80:
            return None
        # Trafilatura also exposes structured metadata (title, author, date)
        # via extract_metadata. Returns a Document with attribute access (2.x)
        # or a dict (older). Handle both.
        title = ""
        try:
            meta = trafilatura.extract_metadata(html, default_url=url)
            if meta is not None:
                title = getattr(meta, "title", None) or (
                    meta.get("title", "") if isinstance(meta, dict) else ""
                )
        except Exception:
            pass
        return {"title": title or "", "text": text.strip(), "extractor": "trafilatura"}
    except Exception:
        return None


def _via_readability(html: str) -> Optional[dict]:
    try:
        from readability import Document
        from bs4 import BeautifulSoup
    except ImportError:
        return None
    try:
        doc = Document(html)
        title = (doc.short_title() or "").strip()
        summary_html = doc.summary(html_partial=True)
        text = BeautifulSoup(summary_html, "html.parser").get_text("\n", strip=True)
        # Collapse runs of blank lines so paragraphs read cleanly in QLabel.
        text = re.sub(r"\n{3,}", "\n\n", text).strip()
        if len(text) < 80:
            return None
        return {"title": title, "text": text, "extractor": "readability"}
    except Exception:
        return None


def _via_soup(html: str) -> Optional[dict]:
    """Last-ditch: pull every <p> with at least 40 chars and join.
    Misses structure but always returns something if the page has prose."""
    try:
        from bs4 import BeautifulSoup
    except ImportError:
        return None
    try:
        soup = BeautifulSoup(html, "html.parser")
        # Kill obvious boilerplate containers before scraping paragraphs.
        for tag in soup(["script", "style", "nav", "header", "footer", "aside", "form", "noscript"]):
            tag.decompose()
        title = (soup.title.string.strip() if soup.title and soup.title.string else "")
        paras = []
        for p in soup.find_all("p"):
            t = p.get_text(" ", strip=True)
            if len(t) >= 40:
                paras.append(t)
        if not paras:
            return None
        text = "\n\n".join(paras)
        if len(text) < 80:
            return None
        return {"title": title, "text": text, "extractor": "bs4"}
    except Exception:
        return None


_SENTENCE_END_RE = re.compile(r"[.!?][\"')\]]*\s*$")
_LIST_LEAD_RE = re.compile(r"^\s*(?:[-*•·]|\d+[.)]|[a-z][.)]\s)\s")


def _merge_paragraphs(text: str) -> str:
    """Collapse over-fragmented paragraphs into natural prose.

    Many news sites markup individual lines with their own `<p>` or `<br>`,
    and trafilatura/readability faithfully preserve that — producing output
    where a single semantic paragraph is split across 3-5 `\\n\\n`-separated
    fragments. The reader perceives these as "many one-line paragraphs",
    which they are visually.

    Heuristic: if a fragment doesn't end with sentence-terminating
    punctuation (`.`, `!`, `?`, optionally followed by closing quote /
    bracket), it's a soft-wrap continuation — join it to the previous
    fragment with a single space. Skip the merge when the next fragment
    looks like a list item ("- ", "* ", "1. ", "a) ") since those are
    legitimately their own paragraphs.

    Conservative — leaves paragraphs that already end with a period
    untouched. Won't fix true paragraphs that happen to lack terminal
    punctuation (rare), and won't split run-on text without `\\n\\n`.

    Also normalises soft line-wraps WITHIN each paragraph: single `\\n`
    inside a paragraph (as opposed to `\\n\\n` paragraph separators) almost
    always comes from the source page wrapping a sentence across multiple
    `<br>` tags or pre-formatted lines. Collapse those to single spaces so
    the C++ formatter, which turns single `\\n` into `<br>` for visual
    parity, doesn't reintroduce the same soft-wrap problem the merge step
    just fixed."""
    paras = [p.strip() for p in text.split("\n\n") if p.strip()]
    if not paras:
        return text
    # Most extractors emit \n\n between paragraphs and (possibly) \n inside
    # one. A few — Wikipedia is the canonical example — emit only single \n
    # between paragraphs with no \n\n anywhere. If we ended up with one
    # giant fragment full of \n's, treat those as the paragraph separators
    # instead.
    if len(paras) == 1 and paras[0].count("\n") > 5:
        paras = [p.strip() for p in paras[0].split("\n") if p.strip()]
    else:
        # Typical case: collapse soft line-wraps within each fragment first
        # (single \n inside a fragment comes from the source's <br> tags).
        paras = [re.sub(r"\s*\n\s*", " ", p).strip() for p in paras]
    paras = [re.sub(r"[ \t]{2,}", " ", p) for p in paras]
    merged: list[str] = []
    for p in paras:
        if (
            merged
            and not _SENTENCE_END_RE.search(merged[-1])
            and not _LIST_LEAD_RE.match(merged[-1])  # don't bleed prose into a list item
            and not _LIST_LEAD_RE.match(p)           # don't start a list as a continuation
        ):
            merged[-1] = merged[-1].rstrip() + " " + p
        else:
            merged.append(p)

    # Second pass — sentence stitching. The soft-wrap merge above only fires
    # when a fragment LACKS sentence-end punctuation. Some news sites wrap
    # *every individual sentence* in its own <p>, so each fragment is a
    # complete sentence ending in `.!?` and the first pass leaves them all
    # separate — producing the "one sentence per paragraph" wall the reader
    # was looking at. This pass bundles consecutive short fragments into
    # paragraph-sized blocks while keeping anything already paragraph-sized
    # (≥ TARGET_CHARS) on its own line. Tuned to typical news prose: a
    # comfortable paragraph runs 200-450 chars; we let buffer grow up to
    # MAX_CHARS before flushing.
    TARGET_CHARS = 180
    MAX_CHARS = 480
    stitched: list[str] = []
    buffer: list[str] = []

    def flush() -> None:
        if buffer:
            stitched.append(" ".join(buffer))
            buffer.clear()

    for p in merged:
        # List items are paragraph-equivalent in their own right — never
        # bundle them with prose on either side.
        if _LIST_LEAD_RE.match(p) or (buffer and _LIST_LEAD_RE.match(buffer[-1])):
            flush()
            stitched.append(p)
            continue
        # Already paragraph-sized → keep independent.
        if len(p) >= TARGET_CHARS:
            flush()
            stitched.append(p)
            continue
        # Would grow the buffer past MAX_CHARS → flush first.
        buf_len = sum(len(b) for b in buffer) + max(0, len(buffer) - 1)
        if buffer and buf_len + len(p) + 1 > MAX_CHARS:
            flush()
        buffer.append(p)
    flush()

    return "\n\n".join(stitched)


def extract(url: str) -> dict:
    if not url or not (url.startswith("http://") or url.startswith("https://")):
        return {"success": False, "error": "invalid url"}

    html = _fetch_html(url)
    if not html:
        return {"success": False, "error": "fetch failed (network, 4xx/5xx, or paywall)"}

    # Try extractors in quality order; first one that yields text wins.
    candidates = (
        _via_trafilatura(html, url),
        _via_readability(html),
        _via_soup(html),
    )
    for result in candidates:
        if result:
            result["success"] = True
            result["url"] = url
            # Post-process to repair over-fragmented paragraphs (see
            # _merge_paragraphs). Applied uniformly across all extractors
            # since every one of them can produce soft-wrap fragments from
            # certain site layouts.
            result["text"] = _merge_paragraphs(result["text"])
            return result

    return {"success": False, "error": "no extractor produced usable text"}


def main() -> int:
    if len(sys.argv) < 2:
        _emit({"success": False, "error": "usage: extract_article.py <url>"})
        return 2
    _emit(extract(sys.argv[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
