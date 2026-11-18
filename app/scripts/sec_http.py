"""One SEC EDGAR HTTP client for every script.

SEC asks every client to (1) name a contact in its User-Agent — the user's,
from Settings › Credentials (finterm_identity.user_agent(); read on EVERY
request, so a contact set after import is used) — and (2) stay under 10
requests/second, shared across everything the user runs. One pacing gate per
process (0.4 s between request starts, the floor every SEC script used) and
one reading of the answer:

    get(url, timeout, *, params, headers, gap) -> (response | None, status | None)
        (resp, 200)    an answer
        (None, 404)    no such file — including EDGAR's archive answering a
                       missing object with 403 + S3 AccessDenied
        (None, 403)    a refusal (no contact, or too fast) — NOT "no such file"
        (None, None)   nothing came back (network failure, timeout)

Options, each for a caller that needs it:
    params   query parameters (efts full-text search)
    headers  extra request headers (Range, Accept-Encoding: identity, Accept);
             the User-Agent is always the contact and cannot be replaced
    gap      this request's spacing from the previous start. The 13F sync
             walks thousands of archive files at its EDGAR_GAP (0.12 s, ~8/s,
             still inside SEC's 10/s); everything else keeps MIN_GAP_S.

No retries here: a request either answers or says why not, once. A caller
that wants to ask again (a large 13F submission timing out, a schedule header
racing the Form 4 fetches) does so itself, and each attempt goes through the
same gate. Responses are read whole (no streaming): the largest download, a
13F data set, is tens of MB and is unzipped from memory anyway.
"""

from __future__ import annotations

import threading
import time
from typing import Any, Dict, Optional, Tuple

MIN_GAP_S = 0.4

_gate = threading.Lock()
_last = 0.0


def is_sec_host(url: str) -> bool:
    from urllib.parse import urlparse
    host = (urlparse(url).hostname or "").lower()
    return host == "sec.gov" or host.endswith(".sec.gov")


def has_contact() -> bool:
    """Whether a contact is configured: without one SEC refuses every request
    (403 "Undeclared Automated Tool"), so a long job can say so up front."""
    from finterm_identity import contact_email
    return bool(contact_email())


def _headers(extra: Optional[Dict[str, str]] = None) -> dict:
    from finterm_identity import user_agent
    h = {"Accept-Encoding": "gzip, deflate"}
    h.update(extra or {})
    h["User-Agent"] = user_agent()  # last: an extra header never replaces the contact
    return h


def missing_archive_object(body: str) -> bool:
    """EDGAR's archive is object storage: a file that does not exist (the
    daily index of a day SEC was closed) comes back 403 with an S3
    AccessDenied document. A refusal for going too fast is an HTML page."""
    b = body or ""
    return "<Code>AccessDenied</Code>" in b and "<Error>" in b


def _pace(gap: float) -> None:
    """Space request STARTS; the requests themselves run concurrently."""
    global _last
    with _gate:
        wait = gap - (time.time() - _last)
        if wait > 0:
            time.sleep(wait)
        _last = time.time()


def get(url: str, timeout: Any = 20, *, params: Optional[Dict[str, Any]] = None,
        headers: Optional[Dict[str, str]] = None,
        gap: float = MIN_GAP_S) -> Tuple[Optional[Any], Optional[int]]:
    """GET an SEC URL. The user's contact goes to *.sec.gov ONLY: any other
    host is refused here, and redirects are followed by hand so a hop off
    sec.gov never carries the header either."""
    if not is_sec_host(url):
        raise ValueError(f"sec_http.get is for *.sec.gov only, not {url!r}")
    try:
        import requests
    except ImportError:
        return None, None
    hdrs = _headers(headers)
    _pace(gap)
    try:
        r = requests.get(url, params=params, timeout=timeout, headers=hdrs, allow_redirects=False)
        for _ in range(5):
            if not r.is_redirect:
                break
            nxt = requests.compat.urljoin(r.url, r.headers.get("Location", ""))
            if not is_sec_host(nxt):
                return None, r.status_code  # a hop off sec.gov: not followed with the contact
            _pace(gap)
            r = requests.get(nxt, timeout=timeout, headers=hdrs, allow_redirects=False)
        if r.is_redirect:
            return None, r.status_code  # still redirecting after five hops: no answer
        if r.status_code == 403 and missing_archive_object(r.text):
            return None, 404
    except Exception:  # noqa: BLE001 — a body that will not decode is no answer either
        return None, None
    return (r, r.status_code) if r.ok else (None, r.status_code)
