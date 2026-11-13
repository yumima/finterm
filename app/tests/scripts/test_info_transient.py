#!/usr/bin/env python3
"""A refused lookup is not an answer.

Yahoo answers a burst of requests a few times a week with HTTP 401 "Invalid
Crumb". yfinance logs it and returns None, `.info` then fails on that None
(or, when only one of its two requests was refused, returns what the other
got) — and the app used to read the blanks as facts: "Unclassified" stored for
7 days, a currency "unknown" for 6 hours, NAV ≈0.00.

get_info must say which it was: {"error", "transient": true} when any request
in the lookup was refused or never answered, so no consumer stores a blank as
a fact; a plain answer (even a "not found") stays an answer.

Offline: yfinance's session is replaced by canned responses, so this runs the
real yfinance request path (crumb, retry, raise_for_status, .info parsing)
without the network.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.normpath(os.path.join(HERE, "..", "..", "scripts"))

failed = []


def check(cond, label):
    print(("PASS : " if cond else "FAIL : ") + label)
    if not cond:
        failed.append(label)


def main():
    sys.path.insert(0, SCRIPTS)
    try:
        import pandas  # noqa: F401
        import curl_cffi.requests.exceptions as cx
        import yfinance_data as yd
        from yfinance.data import YfData
    except Exception as exc:  # noqa: BLE001
        print(f"SKIP : yfinance_data not importable here ({exc})")
        return 77

    class Resp:
        def __init__(self, status, url, payload):
            self.status_code, self.url, self._payload = status, url, payload
            self.headers, self.text = {}, json.dumps(payload)

        def raise_for_status(self):
            if self.status_code >= 400:
                raise cx.HTTPError(f"HTTP Error {self.status_code}", response=self)

        def json(self):
            return self._payload

    UNAUTH = {"finance": {"result": None, "error": {"code": "Unauthorized", "description": "Invalid Crumb"}}}
    PROFILE = {"quoteSummary": {"result": [{"assetProfile": {"sector": "Technology", "industry": "Semiconductors"},
                                            "price": {"currency": "USD"}}], "error": None}}
    QUOTE = {"quoteResponse": {"result": [{"currency": "USD", "regularMarketPrice": 904.9}], "error": None}}
    NOT_FOUND = {"quoteSummary": {"result": None, "error": {"code": "Not Found", "description": "Quote not found"}}}
    EMPTY_QUOTE = {"quoteResponse": {"result": [], "error": None}}
    STATS = {"timeseries": {"result": [{}], "error": None}}

    class Session:
        """Routes by endpoint: profile = quoteSummary, quote = v7, stats =
        the key-statistics timeseries .info also fetches (through cache_get)."""
        def __init__(self, profile, quote, stats=(200, STATS)):
            self.profile, self.quote, self.stats = profile, quote, stats

            class Cookies:
                jar = []
            self.cookies = Cookies()

        calls = []  # every URL asked, across sessions

        def get(self, url, params=None, timeout=30, **_):
            Session.calls.append(url)
            route = (self.stats if "timeseries" in url
                     else self.profile if "quoteSummary" in url else self.quote)
            if isinstance(route, list):  # a sequence: one response per call
                route = route.pop(0) if len(route) > 1 else route[0]
            if route == "network":
                raise cx.RequestException("connection reset")
            status, payload = route
            return Resp(status, url, payload)

    YfData._get_cookie_and_crumb = lambda self, timeout=30: ("crumb", "basic")
    YfData._set_cookie_strategy = lambda self, strategy, have_lock=False: None
    data = YfData()

    def lookup(profile, quote, sym, stats=(200, STATS)):
        data._session = Session(profile, quote, stats)
        return yd.get_info(sym)

    r = lookup((401, UNAUTH), (401, UNAUTH), "AAA")
    check(r.get("error") and r.get("transient") is True, "both requests 401 -> transient error")

    r = lookup((401, UNAUTH), (200, QUOTE), "BBB")
    check(r.get("error") and r.get("transient") is True,
          "profile 401 but quote answered -> transient error, not a profile without a sector")

    r = lookup("network", (200, QUOTE), "CCC")
    check(r.get("error") and r.get("transient") is True, "no answer at all (network) -> transient error")

    r = lookup((200, PROFILE), (200, QUOTE), "DDD")
    check("error" not in r and r.get("sector") == "Technology" and r.get("currency") == "USD",
          "both answered -> the data, no error")

    r = lookup((404, NOT_FOUND), (200, EMPTY_QUOTE), "EEE")
    check(not r.get("transient"), "404 not found is an answer, not transient")

    # The trace is per lookup: a refusal in one lookup must not taint the next.
    lookup((401, UNAUTH), (401, UNAUTH), "FFF")
    r = lookup((200, PROFILE), (200, QUOTE), "GGG")
    check("error" not in r, "a previous lookup's 401 does not taint the next one")

    # cache_get is lru_cache'd by URL (one URL per symbol per day): a refused
    # key-statistics request used to be kept, failing that symbol until midnight.
    r = lookup((200, PROFILE), (200, QUOTE), "HHH", stats=(401, UNAUTH))
    check(r.get("transient") is True, "key-statistics 401 -> transient error")
    r = lookup((200, PROFILE), (200, QUOTE), "HHH")
    check("error" not in r and r.get("sector") == "Technology",
          "once Yahoo answers again the same symbol recovers (the 401 was not cached)")

    # A refusal for one symbol must not evict another's cached answer (the
    # first fix cleared the whole cache, re-fetching everything mid-burst).
    lookup((200, PROFILE), (200, QUOTE), "LLL")
    lookup((200, PROFILE), (200, QUOTE), "MMM", stats=(401, UNAUTH))
    Session.calls.clear()
    lookup((200, PROFILE), (200, QUOTE), "LLL")
    check(not any("timeseries" in u and "LLL" in u for u in Session.calls),
          "another symbol's refusal leaves LLL's cached key-statistics in place")

    # yfinance retries any >= 400 once with the other cookie strategy: a 401
    # that the retry recovers is an answer, not a refusal.
    r = lookup([(401, UNAUTH), (200, PROFILE)], (200, QUOTE), "III")
    check("error" not in r and r.get("sector") == "Technology",
          "401 recovered by yfinance's own retry -> the answer is kept")

    # IPO Watch's batch profiles: a refused row is an error row (skipped and
    # not cached), never a profile with a blank sector.
    data._session = Session((401, UNAUTH), (200, QUOTE))
    rows = yd.get_batch_info(["JJJ"])
    check(len(rows) == 1 and rows[0].get("transient") is True and "sector" not in rows[0],
          "batch profile refused -> transient error row, no blank sector")
    check(not yd._is_cacheable_result(rows), "a batch with a refused row is not cached")
    data._session = Session((200, PROFILE), (200, QUOTE))
    rows = yd.get_batch_info(["KKK"])
    check(rows and rows[0].get("sector") == "Technology" and yd._is_cacheable_result(rows),
          "batch profile answered -> data, cacheable")

    # The cause of the bursts: yf.download() installs a brand-new session into
    # the YfData singleton on every call (multi.py), dropping the cookie the
    # cached crumb belongs to — the next .info then 401s. Both guards are
    # checked on their own: the download serializer hands download the live
    # session, and the pin refuses any other replacement. Offline: the swap
    # happens before any ticker is fetched (an empty list fails right after).
    import yfinance as yf
    from curl_cffi import requests as curl_requests
    pinned = data._session
    pin = YfData._set_session
    unpinned = getattr(pin, "__wrapped__", None)
    check(unpinned is not None, "session pin installed (wraps YfData._set_session)")
    # The serializer alone: lift the pin for the call.
    YfData._set_session = unpinned or pin
    try:
        yf.download([], progress=False)
    except Exception:  # noqa: BLE001
        pass
    finally:
        YfData._set_session = pin
    check(data._session is pinned, "yf.download() is handed the live session, so it installs nothing new")
    # The pin alone.
    YfData(session=curl_requests.Session(impersonate="chrome"))
    check(data._session is pinned, "YfData(session=<new curl session>) never replaces a live session")

    if failed:
        print(f"\n{len(failed)} FAILED: " + ", ".join(failed))
        return 1
    print("\nA refused lookup is never an answer.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
