#!/usr/bin/env python3
"""The contact identity finterm declares to SEC / FINRA / OSM.

SEC refuses requests without a declared contact (403 "Undeclared Automated
Tool", since 2026-10), and the address goes into an HTTP header inside a
"finterm (<email>)" comment — so only a plain local@domain.tld in printable
ASCII is sent. The C++ side (MarketDataService header_safe_email) applies the
same rule; the cases below are the ones its review named.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts"))
import finterm_identity as fi

fails = []
for e, want in [("me@example.com", True), ("a.b+c@sub.example.co", True),
                ("me@", False), ("@example.com", False), ("me@example", False),
                ("me@.com", False), ("me@example.com.", False),
                ("a)b@c.com", False), ("a(b@c.com", False), ("a b@c.com", False),
                ("josé@example.de", False), ("me@example.com\r\nX: y", False), ("", False)]:
    if fi.valid_contact(e) != want:
        fails.append(f"valid_contact({e!r}) should be {want}")

_saved_contact = os.environ.get("FINTERM_CONTACT_EMAIL")


def _restore_contact():
    if _saved_contact is None:
        os.environ.pop("FINTERM_CONTACT_EMAIL", None)
    else:
        os.environ["FINTERM_CONTACT_EMAIL"] = _saved_contact


try:
    os.environ["FINTERM_CONTACT_EMAIL"] = "  me@example.com  "
    if fi.user_agent() != "finterm (me@example.com)":
        fails.append(f"configured contact -> {fi.user_agent()!r}")
    os.environ["FINTERM_CONTACT_EMAIL"] = "me@"
    if fi.user_agent() != "finterm (https://github.com/yumima/finterm)":
        fails.append(f"an invalid contact must not be sent: {fi.user_agent()!r}")
    os.environ.pop("FINTERM_CONTACT_EMAIL")
    if fi.user_agent() != "finterm (https://github.com/yumima/finterm)":
        fails.append("unset -> the project identity")
finally:
    _restore_contact()

# The contact is declared to SEC, FINRA and OpenStreetMap ONLY (the setting
# says so). Every other caller must use project_user_agent(): a scan of the
# scripts for user_agent() outside these files is a leak of the address.
import re
SCRIPTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts")
DECLARED = {  # host each file talks to; every other SEC caller goes through sec_http
    "sec_http.py",                                               # the one SEC client: refuses any non-*.sec.gov host, redirects included
    "finra_short_volume.py", "finra_short_interest.py",          # FINRA
    "nominatim_data.py", "overpass_api_data.py",                 # OpenStreetMap
}
for root, _, files in os.walk(SCRIPTS):
    for fn in files:
        if not fn.endswith(".py") or fn == "finterm_identity.py":
            continue
        path = os.path.join(root, fn)
        rel = os.path.relpath(path, SCRIPTS)
        src = open(path, encoding="utf-8", errors="replace").read()
        # any import of the address-bearing names — aliased or not — counts;
        # project_user_agent is the safe one
        imports = re.findall(r"from\s+finterm_identity\s+import\s+([^\n]+)", src)
        names = {n.strip().split(" as ")[0].strip("() ") for line in imports for n in line.split(",")}
        if re.search(r"import\s+finterm_identity", src):
            names.add("<module>")  # finterm_identity.user_agent / .contact_email reachable
        leaky = names & {"user_agent", "contact_email", "<module>"}
        if leaky and rel not in DECLARED:
            fails.append(f"{rel} can send the user's contact ({', '.join(sorted(leaky))}) "
                         f"to a host the setting does not name")

# edgartools is the one SEC client that is not sec_http (it builds its own httpx
# client). Every module importing it must call sec_http.route_edgartools() —
# the contact as its identity (without one SEC refuses it and edgartools
# prompts on stdin) and its requests on the shared gate — or, inside the
# sec_edgar package, import the package's base, which does.
EDGAR_IMPORT = re.compile(r"^\s*(from\s+edgar(\.\w+)*\s+import|import\s+edgar\b)", re.M)
edgar_users = 0
for root, _, files in os.walk(SCRIPTS):
    for fn in files:
        if not fn.endswith(".py") or fn == "sec_http.py":
            continue
        path = os.path.join(root, fn)
        rel = os.path.relpath(path, SCRIPTS)
        src = open(path, encoding="utf-8", errors="replace").read()
        if not EDGAR_IMPORT.search(src):
            continue
        edgar_users += 1
        routed = "sec_http.route_edgartools()" in src
        via_base = (rel.startswith("sec_edgar" + os.sep)
                    and re.search(r"^from \.base import", src, re.M))
        if not (routed or via_base):
            fails.append(f"{rel} imports edgartools without sec_http.route_edgartools() "
                         f"(no contact identity, unpaced requests)")
if edgar_users < 2:
    fails.append(f"the edgartools import scan found {edgar_users} users: the pattern no longer matches")

# sec_http.get carries the contact: it must refuse every host but *.sec.gov,
# look-alikes included (redirects are followed by hand through the same test).
sys.path.insert(0, SCRIPTS)
import sec_http  # noqa: E402
for url, ok in [("https://www.sec.gov/x", True), ("https://data.sec.gov/x", True), ("https://sec.gov/", True),
                ("https://evilsec.gov/", False), ("https://sec.gov.evil.com/", False), ("https://example.com/", False)]:
    if sec_http.is_sec_host(url) != ok:
        fails.append(f"sec_http.is_sec_host({url!r}) should be {ok}")
try:
    sec_http.get("https://example.com/")
    fails.append("sec_http.get sent a request to a non-SEC host")
except ValueError:
    pass

# …and what it sends and how it reads the answer, against a scripted network.
import threading  # noqa: E402
import time  # noqa: E402

import requests  # noqa: E402


class _R:
    def __init__(self, url, status, location=None, text="", chunks=None):
        self.url, self.status_code, self.text = url, status, text
        self.headers = {"Location": location} if location else {}
        self.is_redirect = location is not None
        self.ok = status < 400
        self.chunks, self.read_chunks = chunks or [], 0

    def iter_content(self, size):
        for c in self.chunks:
            self.read_chunks += 1
            yield c

    def close(self):
        pass

    @property
    def content(self):  # as requests serves it: whatever was read into _content
        return self._content


_calls = []


def _script(answers):
    def fake_get(url, params=None, timeout=None, headers=None, allow_redirects=True, stream=False):
        _calls.append((url, dict(headers or {})))
        a = answers[url]
        if isinstance(a, list):
            a = a.pop(0)
        if isinstance(a, Exception):
            raise a
        return a
    return fake_get


def _check_sec_http():
    S3 = "<Error><Code>AccessDenied</Code><Message>Access Denied</Message></Error>"
    requests.get = _script({"https://www.sec.gov/a": _R("https://www.sec.gov/a", 302, "https://evil.example.com/a"),
                            "https://www.sec.gov/b": _R("https://www.sec.gov/b", 301, "https://data.sec.gov/b"),
                            "https://data.sec.gov/b": _R("https://data.sec.gov/b", 200),
                            "https://www.sec.gov/gone": _R("https://www.sec.gov/gone", 403, text=S3),
                            "https://www.sec.gov/slow": _R("https://www.sec.gov/slow", 403, text="<html>Rate</html>")})
    if sec_http.get("https://www.sec.gov/a") != (None, 302) or any("evil" in u for u, _ in _calls):
        fails.append("sec_http followed a redirect off sec.gov")
    r, st = sec_http.get("https://www.sec.gov/b", headers={"User-Agent": "spoof", "Range": "bytes=0-1"})
    if st != 200 or _calls[-1][0] != "https://data.sec.gov/b":
        fails.append("sec_http did not follow a redirect within sec.gov")
    if any(h.get("User-Agent") != "finterm (me@example.com)" for _, h in _calls[1:]):
        fails.append("an extra header replaced the contact")
    if _calls[-1][1].get("Range") != "bytes=0-1":
        fails.append("extra headers are not carried over a redirect")
    if sec_http.get("https://www.sec.gov/gone") != (None, 404):
        fails.append("EDGAR's S3 AccessDenied must read as 404 (no such file)")
    if sec_http.get("https://www.sec.gov/slow") != (None, 403):
        fails.append("a rate-limit 403 must stay a refusal")

    # max_bytes: the body is streamed and reading stops at the cap
    big = _R("https://www.sec.gov/big", 200, chunks=[b"a" * 10, b"b" * 10, b"c" * 10, b"d" * 10])
    requests.get = _script({"https://www.sec.gov/big": big})
    r, st = sec_http.get("https://www.sec.gov/big", max_bytes=15)
    if r is None or r.content != b"a" * 10 + b"b" * 5 or big.read_chunks != 2:
        fails.append(f"max_bytes did not cap the read: {getattr(r, 'content', None)!r}, {big.read_chunks} chunks")

    # retries: connection failures only, a fixed number of times
    sec_http.CONNECT_RETRY_WAIT_S = 0
    CE = requests.exceptions.ConnectionError
    requests.get = _script({"https://www.sec.gov/flaky": [CE("reset"), CE("refused"), _R("https://www.sec.gov/flaky", 200)],
                            "https://www.sec.gov/down": [CE("x")] * (sec_http.CONNECT_RETRIES + 2),
                            "https://www.sec.gov/busy": [_R("https://www.sec.gov/busy", 503), _R("https://www.sec.gov/busy", 200)],
                            "https://www.sec.gov/stall": [requests.exceptions.ReadTimeout("t"),
                                                          _R("https://www.sec.gov/stall", 200)]})
    n = len(_calls)
    if sec_http.get("https://www.sec.gov/flaky")[1] != 200 or len(_calls) - n != 3:
        fails.append("a connection failure was not retried through to the answer")
    n = len(_calls)
    if sec_http.get("https://www.sec.gov/down") != (None, None) or len(_calls) - n != sec_http.CONNECT_RETRIES + 1:
        fails.append(f"connection retries are not bounded at {sec_http.CONNECT_RETRIES}")
    n = len(_calls)
    if sec_http.get("https://www.sec.gov/busy") != (None, 503) or len(_calls) - n != 1:
        fails.append("an HTTP answer (503) was retried")
    n = len(_calls)
    if sec_http.get("https://www.sec.gov/stall") != (None, None) or len(_calls) - n != 1:
        fails.append("a read timeout was retried")

    # one gate for every caller: request STARTS spaced by `gap`, threads included
    starts = []
    requests.get = lambda url, **k: (starts.append(time.monotonic()), _R(url, 200))[1]
    ts = [threading.Thread(target=sec_http.get, args=("https://www.sec.gov/x",), kwargs={"gap": 0.1})
          for _ in range(4)]
    [t.start() for t in ts]
    [t.join() for t in ts]
    starts.sort()
    if min(b - a for a, b in zip(starts, starts[1:])) < 0.095:
        fails.append(f"sec_http let concurrent requests start closer than the gap: {starts}")

    # deadline: a slot further off is not taken (nothing sent, nothing reserved),
    # and the lock is not held while another caller sleeps for its slot
    t = threading.Thread(target=sec_http.get, args=("https://www.sec.gov/x",), kwargs={"gap": 1.0})
    t.start()
    time.sleep(0.05)
    n, reserved = len(starts), sec_http._last
    t0 = time.monotonic()
    got = sec_http.get("https://www.sec.gov/x", deadline=0.2)
    took = time.monotonic() - t0
    if got != (None, None) or len(starts) != n or sec_http._last != reserved:
        fails.append("a request whose gate slot is past its deadline was sent or reserved")
    if took > 0.15:
        fails.append(f"the gate was held while another request slept for its slot ({took:.2f} s)")
    t.join()

    # a wall clock stepped backwards does not stall the gate (monotonic pacing)
    real_time = time.time
    time.time = lambda: 0.0
    try:
        t0 = time.monotonic()
        sec_http.get("https://www.sec.gov/x", gap=0.05)
        sec_http.get("https://www.sec.gov/x", gap=0.05)
        if time.monotonic() - t0 > 1.5:
            fails.append("a wall-clock step stalled the gate")
    finally:
        time.time = real_time

    # edgartools: the contact is its identity (a user's own EDGAR_IDENTITY is
    # kept), and its rate limiter is this gate
    saved_identity = os.environ.pop("EDGAR_IDENTITY", None)
    try:
        routed = sec_http.route_edgartools()
        if routed and os.environ.get("EDGAR_IDENTITY") != "finterm (me@example.com)":
            fails.append(f"route_edgartools did not declare the contact: {os.environ.get('EDGAR_IDENTITY')!r}")
        if routed:
            os.environ["FINTERM_CONTACT_EMAIL"] = "new@example.com"
            sec_http.route_edgartools()
            if os.environ.get("EDGAR_IDENTITY") != "finterm (new@example.com)":
                fails.append("a changed contact did not reach edgartools")
            os.environ["EDGAR_IDENTITY"] = "Their Own them@example.org"
            sec_http.route_edgartools()
            if os.environ.get("EDGAR_IDENTITY") != "Their Own them@example.org":
                fails.append("route_edgartools overwrote an EDGAR_IDENTITY the user set")
    finally:
        if saved_identity is None:
            os.environ.pop("EDGAR_IDENTITY", None)
        else:
            os.environ["EDGAR_IDENTITY"] = saved_identity
        sec_http._edgar_identity = None
    if routed:
        from edgar import httpclient
        lim = httpclient.HTTP_MGR.rate_limiter
        a = time.monotonic()
        lim.try_acquire("x")
        lim.try_acquire("x")
        if not isinstance(lim, sec_http._EdgarToolsGate) or time.monotonic() - a < sec_http.MIN_GAP_S - 0.05:
            fails.append("edgartools' requests are not paced by sec_http's gate")


_real = (requests.get, time.time, sec_http.CONNECT_RETRY_WAIT_S)
os.environ["FINTERM_CONTACT_EMAIL"] = "me@example.com"
try:
    _check_sec_http()
finally:
    requests.get, time.time, sec_http.CONNECT_RETRY_WAIT_S = _real
    _restore_contact()

for f in fails:
    print("FAIL:", f)
print("identity: all checks passed" if not fails else f"{len(fails)} failure(s)")
sys.exit(1 if fails else 0)
