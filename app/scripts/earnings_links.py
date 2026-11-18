"""Links for the ER Earnings tab's NEXT REPORT header, for ONE print — the
call the tab is about (the print of the last few days if one just happened,
otherwise the next scheduled one; the app passes its date):

  ▶ LISTEN   the call's own live webcast / replay (events.q4inc.com/attendee/…,
             open-exchange.net, edge.media-server.com, a YouTube video), when
             the company has posted it — usually 2-4 weeks ahead;
  RELEASE    that print's earnings release (8-K Item 2.02, EX-99.1) on EDGAR,
             once filed — minutes after the numbers; until then "due <date>".

Where the call link comes from, best first:
  1. the IR site's upcoming-events RSS. Q4-hosted IR sites (NVDA, CRM, COST,
     V, ORCL, META, AMZN, NFLX …) all serve /rss/Event.aspx: one item per
     upcoming event, titled "M/D/YYYY : <name>", the description carrying a
     "View the live Webcast" link. Matched to the print by that date.
  2. a direct webcast link printed next to the print's date on the IR pages
     (HD's choruscall links, INTC's media-server links).
  3. a direct webcast link in the print's own release (GOOGL links the
     call's YouTube video).
Never the IR home page dressed up as the call: when none of these has it,
the result carries `ir_url` and the UI labels it as the IR site.

The IR site is the one the company's own releases name; Yahoo's `irWebsite`
was measured stale (AMZN → a dead corporate-ir.net page) and is not used.
IR sites are asked as the project (project_user_agent), never with the
user's SEC contact. Not covered: foreign private issuers (6-K, no item
codes), sites that refuse non-browser clients (TSLA, AVGO answer 403 — not
worked around), and IR pages built entirely in the browser with no feed.
"""

import html
import json
import os
import re
import threading
import time
import urllib.request
from datetime import date, datetime, timedelta
from urllib.parse import urljoin, urlparse

from finterm_identity import project_user_agent, user_agent

# A release exhibit is a few hundred KB; anything far larger is not a release.
_MAX_DOC_BYTES = 3_000_000
# Per request. SEC normally answers in well under a second; the whole action
# also stops asking IR sites once _BUDGET_SEC is spent, so it finishes inside
# the app's 60 s wait (EquityResearchService::subscribe_earnings_links).
_TIMEOUT_SEC = 10
_BUDGET_SEC = 40
# Documents read per filing: the EX-99s first, then the cover 8-K.
_DOCS_PER_FILING = 3


class _FetchFailed(Exception):
    """A request that should have answered didn't — the result is unknown,
    not empty, and must not be cached as an answer."""


def _is_sec(url):
    host = (urlparse(url).hostname or "").lower()
    return host == "sec.gov" or host.endswith(".sec.gov")


class _ContactStaysOnSec(urllib.request.HTTPRedirectHandler):
    """urllib copies the request's headers onto a redirect: a sec.gov URL
    that redirects elsewhere would carry the user's contact there. The hop
    off SEC is made as the project instead."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is not None and not _is_sec(newurl):
            new.add_header("User-Agent", project_user_agent())
        return new


_OPENER = urllib.request.build_opener(_ContactStaysOnSec)


def _get(url, as_json=True, limit=None, sec=True, final_url=False):
    """GET `url`. SEC requests carry the user's declared contact; anything
    else (IR sites) the project's identity. `final_url` also returns where
    redirects ended (amazon.com/ir → ir.aboutamazon.com)."""
    # The contact is declared to SEC only: decided by the host itself, so an
    # exhibit link parsed out of a filing that points elsewhere never gets it.
    ua = user_agent() if sec and _is_sec(url) else project_user_agent()
    req = urllib.request.Request(url, headers={"User-Agent": ua, "Accept-Encoding": "identity"})
    try:
        with _OPENER.open(req, timeout=_TIMEOUT_SEC) as r:
            raw = r.read(limit) if limit else r.read()
            where = r.geturl()
    except Exception as e:  # noqa: BLE001
        tail = url.rstrip("/").rsplit("/", 1)[-1] if sec else urlparse(url).netloc
        raise _FetchFailed(f"{type(e).__name__} on {tail}") from None
    text = raw.decode("utf-8", "replace")
    body = json.loads(text) if as_json else text
    return (body, where) if final_url else body


def _cik_for(symbol):
    # The app's one ticker → CIK lookup (sec_directory: BRK.B/BRK-B
    # normalised) — not another private copy of company_tickers.json.
    import sec_directory

    c = sec_directory.cik(symbol)
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


def find_webcast_url(text, sites_only=False):
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
        for y in ([] if sites_only else _YOUTUBE.finditer(win)):
            handle = y.group(1).rstrip(".,;:")
            candidates.append((4, order, f"https://www.youtube.com/{handle}"))
            order += 1
        for raw in _URL.findall(win):
            u = _normalise(raw)
            if sites_only and _CALL_URL.match(u):
                continue  # a call's own webcast is not where the NEXT call is listed
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
    """(release_url, docs to search, filed) for row i of recent filings."""
    acc = rec["accessionNumber"][i]
    base = f"https://www.sec.gov/Archives/edgar/data/{cik}/{acc.replace('-', '')}"
    primary = rec.get("primaryDocument", [""])[i]
    ex99 = _ex99_first(_exhibits(_get(f"{base}/{acc}-index.htm", as_json=False)))
    cover = f"{base}/{primary}" if primary else ""
    release = ex99[0] if ex99 else cover
    docs = (ex99 + ([cover] if cover else []))[:_DOCS_PER_FILING]
    return release, docs, rec.get("filingDate", [""])[i]


def _addresses_in(docs):
    """(best address of any kind, best SITE address) the release documents
    name for the call — the first is the call's own webcast when the release
    links it (GOOGL's video), the second is where calls are listed."""
    best = site = None
    for url in docs:
        text = _plain_text(_get(url, as_json=False, limit=_MAX_DOC_BYTES))
        best = best or find_webcast_url(text)
        site = site or find_webcast_url(text, sites_only=True)
        if best and site:
            break
    return best, site


# ── the call itself ──────────────────────────────────────────────────────────

# A link that IS a call's webcast/replay, not a site, a channel or a login.
_CALL_URL = re.compile(
    r"https?://(?:"
    r"events\.q4inc\.com/attendee/\d+"
    r"|[a-z0-9-]+\.open-exchange\.net/?[^\s\"'<>]*"
    r"|edge\.media-server\.com/mmc/p/[a-z0-9]+/?"
    r"|event\.webcasts\.com/starthere\.jsp\?ei=\d+[^\s\"'<>]*"
    r"|event\.choruscall\.com/mediaframe/webcast\.html\?webcastid=\w+"
    r"|[a-z0-9.-]*onlinexperiences\.com/[^\s\"'<>]+"
    r"|event\.on24\.com/[^\s\"'<>]+"
    r"|(?:www\.)?youtube\.com/(?:watch\?v=|live/)[\w-]+"
    r"|youtu\.be/[\w-]+"
    r")", re.I)
# "Financial Results" / "Q3 FY27" / "Earnings Call" — not "November Sales
# Results" (COST posts monthly sales as events on its feed).
# A file a media player can open as-is — no registration page in front.
_MEDIA_URL = re.compile(r"https?://[^\s\"'<>]+?\.(?:mp3|m4a|aac|m3u8|mp4)(?:\?[^\s\"'<>]*)?", re.I)
_YOUTUBE_PLAYABLE = re.compile(r"https?://(?:(?:www\.)?youtube\.com/(?:watch\?v=|live/)|youtu\.be/)[\w-]+", re.I)
_YOUTUBE_CHANNEL = re.compile(r"^https?://(?:www\.)?youtube\.com/(@?[\w.-]+)/?$", re.I)
_CALLISH = re.compile(r"earning|financial results|quarter|\bq[1-4]\b|conference call|webcast", re.I)
_RSS_ITEM = re.compile(r"(?s)<item>(.*?)</item>")
_RSS_TITLE = re.compile(r"(?s)<title>(.*?)</title>")
_RSS_LINK = re.compile(r"(?s)<link>(.*?)</link>")
_RSS_DATE = re.compile(r"^\s*(\d{1,2})/(\d{1,2})/(\d{4})\s*:")


def _date_patterns(d):
    """The ways an IR page prints a date, for d−1 … d+1 (a call can sit a
    day off the date Yahoo carries for the print)."""
    out = []
    for k in (-1, 0, 1):
        x = d + timedelta(days=k)
        M, m, day, y = x.strftime("%B"), x.strftime("%b"), x.day, x.year
        # (?<!\d) / (?!\d): day 7 must not match inside "17 November".
        out += [rf"{M}\.?\s+{day}(?!\d),?\s+{y}", rf"{m}\.?\s+{day}(?!\d),?\s+{y}",
                rf"(?<!\d){day}\s+{M},?\s+{y}", rf"(?<!\d){day}\s+{m}\.?,?\s+{y}",
                rf"(?<!\d)0?{x.month}/0?{day}/{y}(?!\d)", x.isoformat()]
    return re.compile("|".join(out), re.I)


def call_from_rss(xml, d):
    """(webcast_url, event_page, media_url) — any may be None — for the feed
    item dated at the print (±1 day) whose title reads as the earnings call.
    An item with a webcast link beats one without (COST posts "Earnings
    Results" and "Earnings Call" as two items on the same day)."""
    best = None
    for it in _RSS_ITEM.findall(xml):
        t = _RSS_TITLE.search(it)
        title = html.unescape(t.group(1) if t else "").strip()
        m = _RSS_DATE.match(title)
        if not m or not _CALLISH.search(title):
            continue
        try:
            when = date(int(m.group(3)), int(m.group(1)), int(m.group(2)))
        except ValueError:
            continue
        if abs((when - d).days) > 1:
            continue
        body = html.unescape(it)
        call = _CALL_URL.search(body)
        lk = _RSS_LINK.search(it)
        link = html.unescape(lk.group(1) if lk else "").strip() or None
        media = _MEDIA_URL.search(body)
        # Rank by what the item CARRIES (a webcast, then a page, then a file),
        # never by comparing the values: None and str don't order, and an
        # empty string would rank a missing link first.
        rank = (0 if call else 1, 0 if link else 1, 0 if media else 1)
        if best is None or rank < best[0]:
            best = (rank, call.group(0) if call else None, link, media.group(0) if media else None)
    if not best:
        return None, None, None
    return best[1], best[2], best[3]


def call_near_date(text, d, reach=600, pattern=None):
    """A call link (or, with `pattern`, any link of that kind) printed within
    `reach` characters of the print's date in a page's text, the closest one;
    None if no such pairing."""
    dates = [m.start() for m in _date_patterns(d).finditer(text)]
    if not dates:
        return None
    best = None
    for m in (pattern or _CALL_URL).finditer(text):
        dist = min(abs(m.start() - x) for x in dates)
        if dist <= reach and (best is None or dist < best[0]):
            best = (dist, m.group(0))
    return best[1] if best else None


def _find_call(ir_url, d, deadline):
    """{url, page, media, source, ir_url} for the call on date d from the IR
    site; {ir_error} when the site would not answer."""
    found = {}
    try:
        page, where = _get(ir_url, as_json=False, limit=_MAX_DOC_BYTES, sec=False, final_url=True)
    except _FetchFailed as e:
        return {"ir_error": str(e)}
    where = re.sub(r"^(https://[^/:]+):443(?=/|$)", r"\1", where)  # redirects keep the default port
    host = urlparse(where).netloc
    found["ir_url"] = where
    # Feeds: the Q4 platform's standard one, plus any events feed the page
    # itself advertises.
    feeds = [f"https://{host}/rss/Event.aspx?LanguageId=1"]
    for h in re.findall(r'href="([^"]*rss[^"]*)"', page, re.I):
        if re.search(r"event", h, re.I):
            feeds.append(urljoin(where, html.unescape(h)))
    for f in dict.fromkeys(feeds):
        if time.monotonic() > deadline:
            return found
        try:
            xml = _get(f, as_json=False, limit=_MAX_DOC_BYTES, sec=False)
        except _FetchFailed:
            continue
        if "<rss" not in xml[:2000] and "<item>" not in xml:
            continue
        url, event_page, media = call_from_rss(xml, d)
        if url or event_page:
            found.update({"url": url, "page": event_page, "media": media, "source": f})
            if url:
                return found
    # Pages: the IR page and up to two events pages it links to.
    pages = [(where, page)]
    for h in dict.fromkeys(urljoin(where, h) for h in re.findall(r'href="([^"]+)"', page)
                           if re.search(r"event", h, re.I) and not re.search(r"rss|\.ics|mailto:", h, re.I)):
        if len(pages) >= 3 or time.monotonic() > deadline:
            break
        try:
            pages.append((h, _get(h, as_json=False, limit=_MAX_DOC_BYTES, sec=False)))
        except _FetchFailed:
            continue
    for u, p in pages:
        text = _plain_text(p)
        media = call_near_date(text, d, pattern=_MEDIA_URL)
        if media and not found.get("media"):
            found["media"] = media
        url = call_near_date(text, d)
        if url and not found.get("url"):
            found.update({"url": url, "source": u})
    return found


def playable_audio(call_url, media_url, site_url, d, today):
    """{url, kind} that finterm can play itself, or None.

    Only sources with no registration in front: a media file, or YouTube (a
    call's video, or — on the print's day — a channel's live stream, which is
    how NFLX hosts its interview). Q4 / Choruscall / open-exchange / media-
    server webcasts sit behind a sign-up form or the platform's own player
    scripts; they open in the browser instead and are never scraped.
    """
    if media_url:
        return {"url": media_url, "kind": "media"}
    if call_url and _YOUTUBE_PLAYABLE.match(call_url):
        return {"url": call_url, "kind": "youtube"}
    ch = _YOUTUBE_CHANNEL.match(site_url or "")
    if ch and d and abs((d - today).days) <= 1:
        return {"url": f"https://www.youtube.com/{ch.group(1)}/live", "kind": "youtube_live"}
    return None


# ── calls, remembered ────────────────────────────────────────────────────────
# Q4's events feed lists UPCOMING events only: the moment a call has taken
# place it drops off, and with it the link to its page and replay (NVDA,
# 17 Nov 2026 — the link was there before the call and gone an hour after).
# So a call link, once found, is kept for its print — the ONE place this is
# remembered (the app does not keep a second copy). Public URLs only, kept
# under market_data/ (locally built public data), not cache/: it can't be
# regenerated once the host's feed has moved on.
_CALLS_KEEP_DAYS = 400
_calls_lock = threading.Lock()


def _calls_path():
    import finterm_paths

    return finterm_paths.market_data_path("earnings_calls.json")


def _calls_load():
    """{key: {url, page, media}}; anything malformed is dropped, never raised."""
    try:
        with open(_calls_path(), encoding="utf-8") as f:
            data = json.load(f)
    except Exception:  # noqa: BLE001 — missing, unreadable, unparsable, no paths module
        return {}
    if not isinstance(data, dict):
        return {}
    return {k: v for k, v in data.items() if isinstance(k, str) and isinstance(v, dict)}


def remembered_call(symbol, d):
    with _calls_lock:
        return _calls_load().get(f"{symbol.upper()}|{d.isoformat()}")


def remember_call(symbol, d, call, today=None):
    """Keep `call` ({url, page, media}) for the print on d — merged with what
    is already kept (a media file found once is not forgotten by a later
    fetch that missed it), written only when something changed, and best
    effort: a store that can't be written never breaks the links answer."""
    today = today or date.today()
    key = f"{symbol.upper()}|{d.isoformat()}"
    try:
        with _calls_lock:
            data = _calls_load()
            old = data.get(key) or {}
            merged = {f: call.get(f) or old.get(f) for f in ("url", "page", "media")}
            cutoff = (today - timedelta(days=_CALLS_KEEP_DAYS)).isoformat()
            pruned = {k: v for k, v in data.items() if k.split("|", 1)[-1] >= cutoff}
            if merged == old and len(pruned) == len(data):
                return
            pruned[key] = merged
            path = _calls_path()
            os.makedirs(os.path.dirname(path), exist_ok=True)
            tmp = f"{path}.{os.getpid()}.{threading.get_ident()}.tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(pruned, f)
            os.replace(tmp, path)
    except Exception:  # noqa: BLE001 — best effort
        pass


def _today_et():
    try:
        from zoneinfo import ZoneInfo

        return datetime.now(ZoneInfo("America/New_York")).date()
    except Exception:  # noqa: BLE001
        return date.today()


def _earnings_8ks(cik):
    """(recent-filings table, indices of its Item 2.02 8-Ks, newest first)."""
    sub = _get(f"https://data.sec.gov/submissions/CIK{cik:010d}.json")
    rec = (sub.get("filings") or {}).get("recent") or {}
    forms, items = rec.get("form", []), rec.get("items", [])
    hits = [i for i, f in enumerate(forms) if f == "8-K" and "2.02" in str(items[i] if i < len(items) else "")]
    return rec, hits


def _release_for(rec, hits, d):
    """Index of the Item 2.02 8-K FOR the print on d, or None.

    Filed on d or up to three days after: an evening filing accepted after
    EDGAR's 17:30 cutoff carries the NEXT business day's date, so a Friday
    after-close print is filed "Monday". Never the day before — a guidance or
    pre-announcement 8-K (also Item 2.02) furnished the day before the print
    is not its release, and counting it would declare the release out and
    stop the watch early."""
    for i in hits:
        try:
            filed = date.fromisoformat(rec["filingDate"][i])
        except (KeyError, ValueError, IndexError):
            continue
        if timedelta(days=0) <= filed - d <= timedelta(days=3):
            return i
    return None


def release_filed(symbol, report_date):
    """Cheap check — ONE SEC request — for whether the print on report_date
    has its earnings 8-K on EDGAR yet. Polled each minute in the release
    window instead of the full links/analysis fetch. {filed, sec}."""
    try:
        d = date.fromisoformat(report_date)
    except (TypeError, ValueError):
        return {"error": f"bad report_date {report_date!r}"}
    try:
        cik = _cik_for(symbol)
        if not cik:
            return {"filed": False, "sec": False}
        rec, hits = _earnings_8ks(cik)
    except Exception as e:  # noqa: BLE001
        return {"error": f"EDGAR didn't answer ({type(e).__name__})"}
    return {"filed": _release_for(rec, hits, d) is not None, "sec": bool(hits)}


def get_earnings_links(symbol, report_date=None):
    """Links for the print on `report_date` (ISO, ET): its call (page and, if
    public, playable audio) and its release.

    EDGAR failures make the answer an {"error"} — the daemon does not cache
    those — rather than a confident "no release" built on a filing list that
    was never read. An IR site that refuses or times out is not an error
    (TSLA refuses every non-browser client, permanently); it is reported as
    `ir_error` and the app asks again at its next scheduled refresh.
    """
    symbol = (symbol or "").strip()
    if not symbol:
        return {"error": "no symbol"}
    t0 = time.monotonic()
    deadline = t0 + _BUDGET_SEC
    try:
        d = date.fromisoformat(report_date) if report_date else None
    except ValueError:
        return {"error": f"bad report_date {report_date!r}"}
    today = _today_et()
    out = {"symbol": symbol, "as_of": int(time.time()), "report_date": report_date or "",
           "release": None, "release_due": "", "call": None, "audio": None, "ir_url": "", "note": ""}
    try:
        cik = _cik_for(symbol)
    except Exception as e:  # noqa: BLE001
        return {"error": f"SEC refused the company list ({type(e).__name__}); "
                         "is the contact email set in Settings › Credentials?"}
    if not cik:
        out["note"] = "Not an SEC registrant — no EDGAR release to link."
        return out
    out["cik"] = cik
    site = None
    try:
        rec, hits = _earnings_8ks(cik)
        if not hits:
            out["note"] = "No 8-K earnings release (Item 2.02) on EDGAR — foreign issuers report on 6-K."
            return out
        target = _release_for(rec, hits, d) if d else None
        # Read this print's filing (wherever it sits — a later pre-announcement
        # can be newer), then the newest two for the IR site. At most three.
        order = list(dict.fromkeys(([target] if target is not None else []) + hits[:2]))
        for i in order:
            if time.monotonic() > deadline:
                break
            release, docs, filed = _filing(cik, rec, i)
            best, named_site = _addresses_in(docs)
            if i == target:
                out["release"] = {"url": release, "filed": filed}
                if best and _CALL_URL.match(best):
                    out["call"] = {"url": best, "page": None, "source": release}
            site = site or named_site
            if site and (target is None or out["release"]):
                break
        if d and out["release"] is None and d >= today - timedelta(days=1):
            out["release_due"] = d.isoformat()
    except _FetchFailed as e:
        return {"error": f"EDGAR didn't answer ({e})"}

    media = None
    if d and site and not (out["call"] or {}).get("url"):
        found = _find_call(site, d, deadline)
        out["ir_url"] = found.get("ir_url") or site
        if found.get("url") or found.get("page"):
            out["call"] = {"url": found.get("url"), "page": found.get("page"), "source": found.get("source")}
        media = found.get("media")
        if found.get("ir_error"):
            out["ir_error"] = found["ir_error"]
    elif site:
        out["ir_url"] = site
    if d:
        call = out["call"] or {}
        if call.get("url") or call.get("page"):
            # Found now: keep it, so the call's page (its replay, afterwards)
            # stays reachable once the host's feed has moved on.
            remember_call(symbol, d, {"url": call.get("url"), "page": call.get("page"), "media": media}, today)
        kept = remembered_call(symbol, d) or {}
        if not (call.get("url") or call.get("page")) and (kept.get("url") or kept.get("page")):
            out["call"] = {"url": kept.get("url"), "page": kept.get("page"), "source": "remembered"}
        media = media or kept.get("media")
        out["audio"] = playable_audio((out["call"] or {}).get("url"), media, out["ir_url"], d, today)
    if not out["call"]:
        out["note"] = ("The company hasn't posted this call's webcast on its IR site yet."
                       if out["ir_url"] else "The company's releases name no IR site.")
    out["elapsed_s"] = round(time.monotonic() - t0, 1)
    return out
