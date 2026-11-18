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

os.environ["FINTERM_CONTACT_EMAIL"] = "  me@example.com  "
if fi.user_agent() != "finterm (me@example.com)":
    fails.append(f"configured contact -> {fi.user_agent()!r}")
os.environ["FINTERM_CONTACT_EMAIL"] = "me@"
if fi.user_agent() != "finterm (https://github.com/yumima/finterm)":
    fails.append(f"an invalid contact must not be sent: {fi.user_agent()!r}")
os.environ.pop("FINTERM_CONTACT_EMAIL")
if fi.user_agent() != "finterm (https://github.com/yumima/finterm)":
    fails.append("unset -> the project identity")

# The contact is declared to SEC, FINRA and OpenStreetMap ONLY (the setting
# says so). Every other caller must use project_user_agent(): a scan of the
# scripts for user_agent() outside these files is a leak of the address.
import re
SCRIPTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts")
DECLARED = {  # host each file talks to; every other SEC caller goes through sec_http
    "sec_http.py",                                               # the one SEC client: refuses any non-*.sec.gov host, redirects included
    "finra_short_volume.py", "finra_short_interest.py",          # FINRA
    "nominatim_data.py", "overpass_api_data.py",                 # OpenStreetMap
    os.path.join("mcp", "edgar", "base.py"),                     # EDGAR, through edgartools' own client (set_identity)
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
import requests  # noqa: E402


class _R:
    def __init__(self, url, status, location=None, text=""):
        self.url, self.status_code, self.text = url, status, text
        self.headers = {"Location": location} if location else {}
        self.is_redirect = location is not None
        self.ok = status < 400


_calls = []


def _script(answers):
    def fake_get(url, params=None, timeout=None, headers=None, allow_redirects=True):
        _calls.append((url, dict(headers or {})))
        return answers[url]
    return fake_get


os.environ["FINTERM_CONTACT_EMAIL"] = "me@example.com"
_real_get = requests.get
try:
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
    # one gate for every caller: request STARTS spaced by `gap`, threads included
    import threading  # noqa: E402
    import time  # noqa: E402
    starts = []
    requests.get = lambda url, **k: (starts.append(time.time()), _R(url, 200))[1]
    ts = [threading.Thread(target=sec_http.get, args=("https://www.sec.gov/x",), kwargs={"gap": 0.1})
          for _ in range(4)]
    [t.start() for t in ts]
    [t.join() for t in ts]
    starts.sort()
    if min(b - a for a, b in zip(starts, starts[1:])) < 0.095:
        fails.append(f"sec_http let concurrent requests start closer than the gap: {starts}")
finally:
    requests.get = _real_get

for f in fails:
    print("FAIL:", f)
print("identity: all checks passed" if not fails else f"{len(fails)} failure(s)")
sys.exit(1 if fails else 0)
