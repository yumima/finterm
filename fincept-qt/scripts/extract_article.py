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
