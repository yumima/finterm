"""One SEC EDGAR HTTP client for every script.

SEC asks every client to (1) name a contact in its User-Agent — the user's,
from Settings › Credentials (finterm_identity.user_agent(); read on EVERY
request, so a contact set after import is used) — and (2) stay under 10
requests/second, shared across everything the user runs. One pacing gate per
process (0.4 s between request starts, the floor every SEC script used) and
one reading of the answer:

    get(url, timeout) -> (response | None, status | None)
        (resp, 200)    an answer
        (None, 404)    no such file — including EDGAR's archive answering a
                       missing object with 403 + S3 AccessDenied
        (None, 403)    a refusal (no contact, or too fast) — NOT "no such file"
        (None, None)   nothing came back (network failure, timeout)
"""

from __future__ import annotations

import threading
import time
from typing import Any, Optional, Tuple

MIN_GAP_S = 0.4

_gate = threading.Lock()
_last = 0.0


def is_sec_host(url: str) -> bool:
    from urllib.parse import urlparse
    host = (urlparse(url).hostname or "").lower()
    return host == "sec.gov" or host.endswith(".sec.gov")


def headers() -> dict:
    from finterm_identity import user_agent
    return {"User-Agent": user_agent(), "Accept-Encoding": "gzip, deflate"}


def missing_archive_object(body: str) -> bool:
    """EDGAR's archive is object storage: a file that does not exist (the
    daily index of a day SEC was closed) comes back 403 with an S3
    AccessDenied document. A refusal for going too fast is an HTML page."""
    b = body or ""
    return "<Code>AccessDenied</Code>" in b and "<Error>" in b


def get(url: str, timeout: Any = 20) -> Tuple[Optional[Any], Optional[int]]:
    """GET an SEC URL. The user's contact goes to *.sec.gov ONLY: any other
    host is refused here, and redirects are followed by hand so a hop off
    sec.gov never carries the header either."""
    global _last
    if not is_sec_host(url):
        raise ValueError(f"sec_http.get is for *.sec.gov only, not {url!r}")
    try:
        import requests
    except ImportError:
        return None, None
    with _gate:
        wait = MIN_GAP_S - (time.time() - _last)
        if wait > 0:
            time.sleep(wait)
        _last = time.time()
    try:
        r = requests.get(url, timeout=timeout, headers=headers(), allow_redirects=False)
        for _ in range(5):
            if not r.is_redirect:
                break
            nxt = requests.compat.urljoin(r.url, r.headers.get("Location", ""))
            if not is_sec_host(nxt):
                return None, r.status_code  # a hop off sec.gov: not followed with the contact
            r = requests.get(nxt, timeout=timeout, headers=headers(), allow_redirects=False)
    except Exception:
        return None, None
    if r.status_code == 403 and missing_archive_object(r.text):
        return None, 404
    return (r, r.status_code) if r.ok else (None, r.status_code)
