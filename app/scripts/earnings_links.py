"""Links for the ER Earnings tab's NEXT REPORT header: the company's latest
earnings release, and where its earnings call is webcast.

Both come from SEC EDGAR, not a vendor. A US issuer furnishes every earnings
release as an 8-K under Item 2.02, with the release itself as EX-99.1, so the
newest 2.02 filing IS the last print, in the company's own words. That
release almost always closes with a "Conference Call" paragraph naming where
the call is webcast (investor.apple.com, amazon.com/ir, a Q4 events page, a
YouTube channel). That place is where the company's next call is listed too,
so the link points at the right page before the next call is scheduled.

Yahoo's `irWebsite` was the obvious alternative and was measured stale:
AMZN's still points at a dead corporate-ir.net page. It is not used.

Not covered, and the result says so instead of guessing: foreign private
issuers (their releases are 6-Ks with no item codes), symbols EDGAR doesn't
list, and releases that are images (WMT's presentation exhibit). The release
link still appears when only the webcast is missing.
"""

import html
import json
import re
import time
import urllib.request

from finterm_identity import user_agent

# A release exhibit is a few hundred KB; anything far larger is not a release.
_MAX_DOC_BYTES = 3_000_000
# Per request. The worst case is 1 + 1 + 2×(1 + 3) = 10 requests; the app
# waits 60 s (EquityResearchService::subscribe_earnings_links), and SEC
# normally answers each in well under a second.
_TIMEOUT_SEC = 10
# Documents read per filing: the EX-99s first, then the cover 8-K.
_DOCS_PER_FILING = 3


class _FetchFailed(Exception):
    """A request that should have answered didn't — the result is unknown,
    not empty, and must not be cached as an answer."""


def _get(url, as_json=True, limit=None):
    req = urllib.request.Request(url, headers={"User-Agent": user_agent(), "Accept-Encoding": "identity"})
    try:
        with urllib.request.urlopen(req, timeout=_TIMEOUT_SEC) as r:
            raw = r.read(limit) if limit else r.read()
    except Exception as e:  # noqa: BLE001
        raise _FetchFailed(f"{type(e).__name__} on {url.rsplit('/', 1)[-1]}") from None
    text = raw.decode("utf-8", "replace")
    return json.loads(text) if as_json else text


def _cik_for(symbol):
    # The daemon's one ticker → CIK lookup (disk-cached a week, BRK.B/BRK-B
    # normalised) — not another private copy of company_tickers.json.
    import sec_statement_overlay

    c = sec_statement_overlay.cik(symbol)
    return int(c) if c else None


# ── webcast URL extraction ───────────────────────────────────────────────────

_KEYWORDS = re.compile(r"webcast|conference call|earnings call|live stream|livestream|video interview|"
                       r"listen to the call|audio of the call", re.I)
# Full URLs, and the bare "investor.apple.com" / "amazon.com/ir" form releases
# usually print. The look-behind keeps "ir@amazon.com" (a contact address) out.
_URL = re.compile(
    r"https?://[^\s\"'<>()\[\]]+"
    r"|(?<![@\w.\-/])(?:[a-z0-9][a-z0-9\-]*\.)+(?:com|net|org|io|co|us|ai|tv|info|biz)\b(?:/[^\s\"'<>()\[\]]*)?",
    re.I)
# Netflix's release says "youtube/netflixir" — a channel, not a URL. Only that
# bare form: a real youtube.com URL (GOOGL links the call's own video,
# watch?v=…) is a URL and goes through _URL whole.
_YOUTUBE = re.compile(r"(?<![\w./])youtube/(@?[A-Za-z0-9_.\-]+)", re.I)

_REJECT_HOST = ("sec.gov", "twitter.com", "x.com", "linkedin.com", "facebook.com", "instagram.com",
                "businesswire.com", "prnewswire.com", "globenewswire.com")
_DIRECT = re.compile(r"webcast|/events?\b|events\.|q4inc|media-server|choruscall|viavid|webcaster|"
                     r"zoom\.us|youtube\.com|vimeo|edge\.media|onlinexperiences|brightcove", re.I)
_IR = re.compile(r"investor|\bir\.|/ir\b|/ir/|shareholder|stockholder|\bstock\.|financial-info", re.I)
_NEWS = re.compile(r"news|press|/release|blog|stories", re.I)


def _plain_text(markup):
    t = re.sub(r"(?is)<(script|style)\b.*?</\1>", " ", markup)
    # Keep anchor targets: a release that hyperlinks "our investor website"
    # carries the URL only in the href.
    t = re.sub(r"(?i)<a\b[^>]*\bhref\s*=\s*[\"']([^\"']+)[\"'][^>]*>", r" \1 ", t)
    t = re.sub(r"<[^>]+>", " ", t)
    return re.sub(r"\s+", " ", html.unescape(t))


def _normalise(u):
    u = u.rstrip(".,;:!?)'\"")
    if not re.match(r"https?://", u, re.I):
        u = "https://" + u
    return u


def _host(u):
    m = re.match(r"https?://([^/:?#]+)", u, re.I)
    return (m.group(1) if m else "").lower()


def _score(u, bare):
    host = _host(u)
    if not host or any(host == h or host.endswith("." + h) for h in _REJECT_HOST):
        return None
    if re.search(r"\.(pdf|xlsx?|jpe?g|png|gif)(\?|$)", u, re.I):
        return None
    s = 1
    if _DIRECT.search(u):
        s += 3
    if _IR.search(u):
        s += 2
    if _NEWS.search(u):
        s -= 2
    # "Booking.com", "Amazon.com, Inc." — a company NAME that ends in a TLD,
    # not an address. Written without a scheme, a bare host only counts when
    # it carries a path (amazon.com/ir), a www. prefix, or an IR/events signal.
    if bare and s <= 1 and not host.startswith("www.") and not re.search(r"https?://[^/]+/.", u):
        return None
    return s


def find_webcast_url(text):
    """The best "where is the call" URL in a release's plain text, or None.

    Only URLs within reach of a call keyword count — a release also links its
    newsroom, its sustainability report and its social accounts. Among those,
    a direct webcast/events link beats an investor-relations page, which beats
    anything else; ties go to the earliest mention.
    """
    candidates = []  # (score, order, url)
    order = 0
    for m in _KEYWORDS.finditer(text):
        win = text[max(0, m.start() - 300): m.end() + 450]
        for y in _YOUTUBE.finditer(win):
            handle = y.group(1).rstrip(".,;:")
            candidates.append((4, order, f"https://www.youtube.com/{handle}"))
            order += 1
        for raw in _URL.findall(win):
            u = _normalise(raw)
            sc = _score(u, bare=not re.match(r"https?://", raw, re.I))
            if sc is not None:
                candidates.append((sc, order, u))
                order += 1
    if not candidates:
        return None
    candidates.sort(key=lambda c: (-c[0], c[1]))
    return candidates[0][2]


# ── the filing ───────────────────────────────────────────────────────────────

_ROW = re.compile(r"(?is)<tr[^>]*>(.*?)</tr>")
_HREF = re.compile(r"(?i)href=\"([^\"]+)\"")
_CELL = re.compile(r"(?is)<td[^>]*>(.*?)</td>")


def _exhibits(index_html):
    """[(type, absolute_url)] from a filing's -index.htm document table."""
    out = []
    for row in _ROW.findall(index_html):
        cells = [re.sub(r"<[^>]+>", "", c).strip() for c in _CELL.findall(row)]
        href = _HREF.search(row)
        if len(cells) < 4 or not href:
            continue
        # Inline-XBRL documents are linked through the viewer: /ix?doc=/Archives/…
        path = href.group(1).split("/ix?doc=")[-1]
        if not re.search(r"\.html?$", path, re.I):
            continue
        url = path if path.startswith("http") else "https://www.sec.gov" + path
        out.append((cells[3].upper(), url))
    return out


def _ex99_first(exhibits):
    """EX-99 exhibit URLs, the press release (EX-99.1, or a lone EX-99) first:
    a filing may list the CFO commentary or slides (EX-99.2) ahead of it."""
    ex = [(t, u) for t, u in exhibits if t.startswith("EX-99")]
    rank = {"EX-99.1": 0, "EX-99": 1, "EX-99.01": 0}
    ex.sort(key=lambda tu: rank.get(tu[0], 2))  # stable: index order within a rank
    return [u for _, u in ex]


def _filing(cik, rec, i):
    """(release_url, docs to search, filed, index_url) for row i of recent filings."""
    acc = rec["accessionNumber"][i]
    base = f"https://www.sec.gov/Archives/edgar/data/{cik}/{acc.replace('-', '')}"
    primary = rec.get("primaryDocument", [""])[i]
    index_url = f"{base}/{acc}-index.htm"
    ex99 = _ex99_first(_exhibits(_get(index_url, as_json=False)))
    cover = f"{base}/{primary}" if primary else ""
    release = ex99[0] if ex99 else (cover or index_url)
    docs = (ex99 + ([cover] if cover else []))[:_DOCS_PER_FILING]
    return release, docs, rec.get("filingDate", [""])[i], index_url


def _webcast_in(docs):
    for url in docs:
        w = find_webcast_url(_plain_text(_get(url, as_json=False, limit=_MAX_DOC_BYTES)))
        if w:
            return {"url": w, "source_url": url}
    return None


def get_earnings_links(symbol):
    """{release, webcast, note} for the symbol's newest 8-K Item 2.02 filing.

    Any request that fails makes the whole answer an {"error"} — the daemon
    does not cache those — rather than a confident "no webcast" built on a
    document that was never read.
    """
    symbol = (symbol or "").strip()
    if not symbol:
        return {"error": "no symbol"}
    out = {"symbol": symbol, "as_of": int(time.time()), "release": None, "webcast": None, "note": ""}
    try:
        cik = _cik_for(symbol)
    except Exception as e:  # noqa: BLE001
        return {"error": f"SEC refused the company list ({type(e).__name__}); "
                         "is the contact email set in Settings › Credentials?"}
    if not cik:
        out["note"] = "Not an SEC registrant — no EDGAR release to link."
        return out
    out["cik"] = cik
    try:
        sub = _get(f"https://data.sec.gov/submissions/CIK{cik:010d}.json")
        rec = (sub.get("filings") or {}).get("recent") or {}
        forms, items = rec.get("form", []), rec.get("items", [])
        hits = [i for i, f in enumerate(forms)
                if f == "8-K" and "2.02" in str(items[i] if i < len(items) else "")][:2]
        if not hits:
            out["note"] = "No 8-K earnings release (Item 2.02) on EDGAR — foreign issuers report on 6-K."
            return out

        release, docs, filed, index_url = _filing(cik, rec, hits[0])
        out["release"] = {"url": release, "filed": filed, "index_url": index_url,
                          "report_date": rec.get("reportDate", [""])[hits[0]] or ""}
        out["webcast"] = _webcast_in(docs)
        # Item 2.02 also carries pre-announcements and guidance updates, which
        # hold no call paragraph. The release before it (the actual quarterly
        # print) names the same webcast page — use it for the webcast only;
        # LAST RELEASE stays the newest filing.
        if not out["webcast"] and len(hits) > 1:
            _, prev_docs, _, _ = _filing(cik, rec, hits[1])
            out["webcast"] = _webcast_in(prev_docs)
    except _FetchFailed as e:
        return {"error": f"EDGAR didn't answer ({e})"}
    if not out["webcast"]:
        out["note"] = "The release names no webcast address."
    return out
