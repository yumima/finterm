"""One SEC EDGAR HTTP client for every script.

SEC asks every client to (1) name a contact in its User-Agent — the user's,
from Settings › Credentials (finterm_identity.user_agent(); read on EVERY
request, so a contact set after import is used) — and (2) stay under 10
requests/second, shared across everything the user runs. One pacing gate per
process (0.4 s between request starts, the floor every SEC script used) and
one reading of the answer:

    get(url, timeout, *, params, headers, gap, max_bytes, deadline)
        -> (response | None, status | None)
        (resp, 200)    an answer
        (None, 404)    no such file — including EDGAR's archive answering a
                       missing object with 403 + S3 AccessDenied
        (None, 403)    a refusal (no contact, or too fast) — NOT "no such file"
        (None, None)   nothing came back (network failure, timeout, or the
                       deadline passed before a request could be sent)

Options, each for a caller that needs it:
    params     query parameters (efts full-text search)
    headers    extra request headers (Range, Accept-Encoding: identity,
               Accept); the User-Agent is always the contact and cannot be
               replaced
    gap        this request's spacing from the previous start. The 13F sync
               walks thousands of archive files at its EDGAR_GAP (0.12 s,
               ~8/s, still inside SEC's 10/s); everything else keeps MIN_GAP_S.
    max_bytes  read at most this many bytes of the body (streamed, the rest
               never downloaded); resp.content is the capped body. For
               documents of unknown size (earnings_links' release exhibits).
               Without it the body is read whole — the largest download, a
               13F data set, is tens of MB and is unzipped from memory anyway.
    deadline   seconds from the call after which no attempt (first try,
               redirect hop or connection retry) is started: a gate slot
               later than that gives (None, None) with nothing sent. The gate
               stays shared (SEC's limit is per user, so a quick caller may
               not jump the queue); this lets a caller holding an answer
               already (the statement overlay) stop waiting behind a 13F walk
               instead. A request already sent still runs its own `timeout`.

Retries — one policy: a CONNECTION failure (no response at all: refused,
reset, DNS, connect timeout) is retried CONNECT_RETRIES times, each attempt
through the gate after CONNECT_RETRY_WAIT_S. An HTTP answer is never
retried — 403/404/429/5xx come back as answered — and neither is a read
timeout (the server took the request; asking again doubles the load). A
caller that wants more (the 13F sync re-asking a large submission on 5xx or
timeout) does it itself, through the same gate.

The one SEC client outside this module is edgartools (scripts/sec_edgar): it
builds its own httpx client. Every module
that imports it calls route_edgartools() (test_identity checks), which
(1) declares the user's contact as its identity — set_identity(user_agent()),
unless the user set EDGAR_IDENTITY themselves; without one edgartools PROMPTS
on stdin for an identity — and (2) puts its rate limiter on this gate, so its
network requests (cache hits are not) are paced with everything else. Its
own retries and a few helper paths that call httpx directly (xmlfiling,
sgml_common) remain its own; the identity reaches whatever host edgartools
asks, which is SEC except its ticker-icon helper (GitHub), unused here.
"""

from __future__ import annotations

import threading
import time
from typing import Any, Dict, Optional, Tuple

MIN_GAP_S = 0.4
CONNECT_RETRIES = 2
CONNECT_RETRY_WAIT_S = 1.0
_MAX_REDIRECTS = 5

_gate = threading.Lock()
_last = float("-inf")  # time.monotonic() of the latest reserved start


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


def _pace(gap: float = MIN_GAP_S, until: Optional[float] = None) -> bool:
    """Space request STARTS by `gap`; the requests themselves run concurrently.
    The slot is reserved under the lock and slept for outside it (monotonic
    clock: a wall-clock step cannot stall the gate). False — nothing reserved
    — when the slot is later than `until` (a monotonic time)."""
    global _last
    with _gate:
        now = time.monotonic()
        slot = max(now, _last + gap)
        if until is not None and slot > until:
            return False
        _last = slot
    if slot > now:
        time.sleep(slot - now)
    return True


def _read_capped(r: Any, max_bytes: int) -> None:
    """Read at most max_bytes of a streamed body into r.content, then close."""
    import requests
    buf = bytearray()
    try:
        for chunk in r.iter_content(64 * 1024):
            buf += chunk
            if len(buf) >= max_bytes:
                break
    except requests.exceptions.ConnectionError as e:
        # requests reports a read timeout mid-body as ConnectionError; the
        # server took the request, so it is not retried (see Retries).
        raise RuntimeError(f"body read failed: {e}") from None
    finally:
        r.close()
    r._content = bytes(buf[:max_bytes])
    r._content_consumed = True


def _once(requests: Any, url: str, timeout: Any, params: Optional[Dict[str, Any]], hdrs: dict,
          gap: float, max_bytes: Optional[int], until: Optional[float]):
    """One attempt: (response|None, status|None), following redirects by hand.
    Raises requests' ConnectionError for the caller's retry policy."""
    stream = max_bytes is not None
    if not _pace(gap, until):
        return None, None
    r = requests.get(url, params=params, timeout=timeout, headers=hdrs, allow_redirects=False, stream=stream)
    for _ in range(_MAX_REDIRECTS):
        if not r.is_redirect:
            break
        nxt = requests.compat.urljoin(r.url, r.headers.get("Location", ""))
        r.close()
        if not is_sec_host(nxt):
            return None, r.status_code  # a hop off sec.gov: not followed with the contact
        if not _pace(gap, until):
            return None, None
        r = requests.get(nxt, timeout=timeout, headers=hdrs, allow_redirects=False, stream=stream)
    if r.is_redirect:
        r.close()
        return None, r.status_code  # still redirecting after five hops: no answer
    if stream:
        _read_capped(r, max_bytes)
    if r.status_code == 403 and missing_archive_object(r.text):
        return None, 404
    return (r, r.status_code) if r.ok else (None, r.status_code)


def get(url: str, timeout: Any = 20, *, params: Optional[Dict[str, Any]] = None,
        headers: Optional[Dict[str, str]] = None, gap: float = MIN_GAP_S,
        max_bytes: Optional[int] = None,
        deadline: Optional[float] = None) -> Tuple[Optional[Any], Optional[int]]:
    """GET an SEC URL. The user's contact goes to *.sec.gov ONLY: any other
    host is refused here, and redirects are followed by hand so a hop off
    sec.gov never carries the header either. Never raises for the network."""
    if not is_sec_host(url):
        raise ValueError(f"sec_http.get is for *.sec.gov only, not {url!r}")
    try:
        import requests
    except ImportError:
        return None, None
    hdrs = _headers(headers)
    until = time.monotonic() + deadline if deadline is not None else None
    for attempt in range(CONNECT_RETRIES + 1):
        try:
            return _once(requests, url, timeout, params, hdrs, gap, max_bytes, until)
        except requests.exceptions.ConnectionError:
            if attempt == CONNECT_RETRIES:
                return None, None
            if until is not None and time.monotonic() + CONNECT_RETRY_WAIT_S > until:
                return None, None
            time.sleep(CONNECT_RETRY_WAIT_S)
        except Exception:  # noqa: BLE001 — a read timeout, a body that will not decode: no answer
            return None, None
    return None, None


class _EdgarToolsGate:
    """The two calls edgartools' transport makes on its pyrate_limiter
    Limiter, answered by this module's gate."""

    def try_acquire(self, name: str = "", weight: int = 1) -> bool:
        return _pace(MIN_GAP_S)

    async def try_acquire_async(self, name: str = "", weight: int = 1) -> bool:
        import asyncio
        return await asyncio.to_thread(_pace, MIN_GAP_S)


_edgar_identity: Optional[str] = None  # the EDGAR_IDENTITY this module set, if any


def route_edgartools() -> bool:
    """Give edgartools the user's contact and pace its network requests
    through this gate (see the module docstring for what stays its own).
    Call it from every module that imports edgartools, and again before work
    if the contact may have changed. False when edgartools is absent or its
    client manager has changed shape (identity is still set when it can be)."""
    global _edgar_identity
    import os
    try:
        from edgar import httpclient, set_identity
    except Exception:  # noqa: BLE001
        return False
    current = os.environ.get("EDGAR_IDENTITY")
    if current is None or current == _edgar_identity:  # not one the user set themselves
        from finterm_identity import user_agent
        ua = user_agent()
        if ua != current:
            set_identity(ua)
            _edgar_identity = ua
            mgr = getattr(httpclient, "HTTP_MGR", None)
            client = getattr(mgr, "_client", None)
            if client is not None:  # its User-Agent is fixed when the client is built
                client.close()
                mgr._client = None
    mgr = getattr(httpclient, "HTTP_MGR", None)
    if mgr is None or not hasattr(mgr, "rate_limiter"):
        return False
    if not isinstance(mgr.rate_limiter, _EdgarToolsGate):
        mgr.rate_limiter = _EdgarToolsGate()
        mgr.rate_limiter_enabled = True
        client = getattr(mgr, "_client", None)
        if client is not None:  # built with the old limiter: rebuilt on next use
            client.close()
            mgr._client = None
    return True
