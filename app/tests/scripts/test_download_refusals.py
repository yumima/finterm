#!/usr/bin/env python3
"""A quote download that could not reach Yahoo is a refusal, never "no data".

yfinance looks each ticker's timezone up before its prices. When that lookup
cannot reach Yahoo (no network, a dead proxy, DNS), it files the ticker under
"possibly delisted; no timezone found" — the very words it uses for a ticker
Yahoo doesn't know. _download_refusals ignored those words (so a chunk of
delisted tickers wouldn't pause every refresh), and an unreachable Yahoo
therefore reached the screen as "answered, but had nothing", 90 tickers at a
time. Pinned here: those errors are refusals — with the network's own reason —
when the same download had a network failure or Yahoo can't be reached; and
are still answers when Yahoo is reachable.

Self-contained: no network (the reachability probe is replaced).
"""

import os
import sys
import types

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
try:
    import pandas  # noqa: F401
except ImportError:
    print("SKIP : pandas not available")
    sys.exit(77)
for _name in ("yfinance", "numpy", "requests", "curl_cffi"):
    if _name not in sys.modules:
        try:
            __import__(_name)
        except ImportError:
            sys.modules[_name] = types.ModuleType(_name)

import yfinance_data as yd  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    print(("PASS : " if cond else "FAIL : ") + name + ("" if cond else f" {detail}"))
    if not cond:
        FAILURES.append(name)


TZ = "YFTzMissingError('possibly delisted; no timezone found')"
CONN = ("ConnectionError('Failed to perform, curl: (7) Failed to connect to 127.0.0.1 port 9 after 1 ms: "
        "Could not connect to server.')")
probes = []


def reachable():
    probes.append(1)
    return None


def unreachable():
    probes.append(1)
    return "ConnectionError: Failed to perform, curl: (6) Could not resolve host: query1.finance.yahoo.com"


# 1. One ticker's error names the network: the timezone misses are refusals
#    with that reason, and nothing needs probing.
yd._yahoo_unreachable = reachable
probes.clear()
got = yd._download_refusals(["AAPL", "msft", "GE"], {"AAPL": TZ, "MSFT": TZ, "GE": CONN})
check("network failure in the download: every ticker refused", set(got) == {"AAPL", "msft", "GE"}, got)
check("…with the network's reason", got.get("AAPL") == CONN and got.get("msft") == CONN, got)
check("…without a probe", not probes)

# 2. Only timezone misses, and Yahoo can't be reached: refusals, the probe's reason.
yd._yahoo_unreachable = unreachable
probes.clear()
got = yd._download_refusals(["AAPL", "MSFT"], {"AAPL": TZ, "MSFT": TZ})
check("unreachable Yahoo: timezone misses are refusals", set(got) == {"AAPL", "MSFT"}, got)
check("…reason is the probe's", "Could not resolve host" in got.get("AAPL", ""), got)

# 3. Only timezone misses, and Yahoo answers: genuinely unknown tickers — not
#    refusals (a delisted chunk must not pause every refresh).
yd._yahoo_unreachable = reachable
got = yd._download_refusals(["XYZQ", "OLDCO"], {"XYZQ": TZ, "OLDCO": TZ})
check("reachable Yahoo: unknown tickers are not refusals", got == {}, got)

# 4. Not this download's tickers: ignored as before.
got = yd._download_refusals(["AAPL"], {"TSLA": CONN})
check("another download's ticker is not ours", got == {}, got)

print(f"{len(FAILURES)} failure(s)")
sys.exit(1 if FAILURES else 0)
