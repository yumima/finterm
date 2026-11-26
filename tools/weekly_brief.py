#!/usr/bin/env python3
"""Weekly brief, one page of Markdown tables: the market at a glance (the call,
the past and the next week), markets and the economy, the week ahead, and stocks (insider buying, fund moves, each strategy screen's first
names).

Built from finterm's own pipeline: SEC Form 4 (sec_form4_market, open-market
purchases filed in the last 7 days, two or more insiders), SEC 13F
(sec_13f_bulk.stock_flows), the screens (screener_data) and the release
calendar, so the published rows are the ones the app shows.

Public output: SEC EDGAR data (public information; cite "U.S. SEC, EDGAR");
figures of U.S. agencies and the Federal Reserve through FRED (public domain
series only); the week's release SCHEDULE (never the feed's consensus); and
each screen's TICKERS with finterm's own backtest record and its own counts of
market breadth — no TradingView or Yahoo number is reproduced (their terms
forbid redistribution). IPOs: the IPO calendar's own figures (Nasdaq's
calendar as PRIVATE MARKET shows it — dates, price, range, shares, size; the
owner's choice, 2026-11-26) and each new listing's return against its IPO price
computed from closes (a return, never a close, as the track record does).
Lock-ups: confirmed by the final prospectus on SEC (sec_ipo.py).

Modes:
    python3 tools/weekly_brief.py [--out DIR]          one Markdown file (local)
    python3 tools/weekly_brief.py --site SITE_DIR      the website: an issue page
        (_posts/), the always-current /insider-cluster-buys/ page, and
        SITE_DIR/_data/announce.json with this week's social post texts.

Wording rules (publisher's exclusion — impersonal, bona fide, regular): the
same template every week including empty weeks, neutral verbs ("filed",
"reported"), a mechanical sort key stated in the text, no recommendation of any security —
the screen names are labelled screen output, each beside a record that shows
no screen beat the index with significance. The market stance is a fixed rule's
output, the same for every reader, printed beside the rule's own record.

SEC refuses clients that don't identify themselves: the contact email is read
from FINTERM_CONTACT_EMAIL, else from finterm's keyring entry. It is never
written to the output.
"""

import argparse
import json
import os
import re
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import date, datetime, timedelta, timezone
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent / "app" / "scripts"
sys.path.insert(0, str(SCRIPTS))

import brief_track  # noqa: E402
from datefmt import fmt as _fmt  # noqa: E402
from weekly_brief_core import (  # noqa: E402,F401 — the shared brief; re-exported for callers
    CLUSTER_HISTORY, CONTACT_KEY, DISCLAIMER, ECONOMY_SERIES, IPO_DAYS, ISSUE_DISCLAIMER, LABELS, MARKET_SERIES,
    METHOD_URL, REGIME_SERIES, SITE_URL, STANCE, WINDOW_DAYS,
    brief_body, call_head, clusters_data, clusters_section, contact_from_keyring, economy_rows, fetch_macro_rows,
    fetch_macro_series, issue_day, issue_key, issue_url, macro_coverage, macro_events, market_regime, market_rows,
    has_record, load_evidence, mine_part, mine_rows, page, regime_words, social_posts, week_ahead,
    ipo_ahead, ipo_priced, ipo_returns)


class Progress:
    """What the build has done and is doing, as a file the app shows while it
    runs: the steps, and the page as far as it is known. Without a path it
    does nothing."""

    def __init__(self, path, steps):
        self.path, self.lock, self.last = path, threading.Lock(), 0.0
        self.state = {"started": time.time(), "done": False, "body": "",
                      "steps": [{"id": i, "label": label, "state": "wait", "detail": "", "part": 0, "t0": 0, "t1": 0}
                                for i, label in steps]}
        self.write(True)

    def step(self, sid, state=None, detail=None, part=None):
        with self.lock:
            s = next((x for x in self.state["steps"] if x["id"] == sid), None)
            if s is None:
                return
            changed = state is not None and state != s["state"]
            if changed:
                s["state"] = state
                s["t0" if state == "run" else "t1"] = time.time()
            if detail is not None:
                s["detail"] = detail
            if part is not None:
                s["part"] = round(part, 3)
            self.write(changed)

    def page(self, body):
        with self.lock:
            self.state["body"] = "\n".join(body)
            self.write(True)

    def finish(self):
        with self.lock:
            self.state["done"] = True
            self.write(True)

    def write(self, now=False):
        # Details come hundreds at a time (a filing each): a few writes a second.
        if not self.path or (not now and time.time() - self.last < 0.4):
            return
        self.last = time.time()
        tmp = f"{self.path}.tmp"
        try:
            Path(tmp).write_text(json.dumps(self.state, ensure_ascii=False), encoding="utf-8")
            os.replace(tmp, self.path)
        except OSError:  # a progress view never fails a build
            pass


def analytics_python():
    """finterm's analytics Python (pandas, yfinance), or None: this script
    runs under the system Python."""
    import finterm_paths

    root = finterm_paths.data_root() / "venv-numpy2"
    py = root / ("Scripts/python.exe" if sys.platform.startswith("win") else "bin/python3")
    return py if py.exists() else None


def screener_doc(timeout=900, say=None):
    """One scan of the strategy screens, run in finterm's analytics Python
    (pandas, yfinance) — this script runs under the system Python. Returns
    (doc, error-or-None). @say(text): its progress lines."""
    import subprocess
    import tempfile

    py = analytics_python()
    if not py:
        return None, "finterm's analytics Python is not installed"
    with tempfile.TemporaryFile("w+", encoding="utf-8") as out:
        p = subprocess.Popen([str(py), str(SCRIPTS / "screener_data.py"), "scan", "{}"], stdin=subprocess.DEVNULL,
                             stdout=out, stderr=subprocess.PIPE, text=True, errors="replace")
        timed_out = threading.Event()
        timer = threading.Timer(timeout, lambda: (timed_out.set(), p.kill()))
        timer.start()
        try:
            for line in p.stderr:
                parts = line.rstrip().split(" ", 3)
                if say and parts[0] == "PROGRESS" and len(parts) == 4:
                    try:
                        say(parts[3], int(parts[1]) / max(1, int(parts[2])))
                    except ValueError:
                        say(parts[3])
            p.wait()
        finally:
            timer.cancel()
            p.stderr.close()
        if timed_out.is_set():
            return None, "the scan timed out"
        out.seek(0)
        text = out.read().strip()
    try:
        doc = json.loads(text.splitlines()[-1]) if text else {}
    except ValueError:
        doc = {}
    if not isinstance(doc, dict):  # a stray last line must not take the issue down
        doc = {}
    if not doc or doc.get("error"):
        return None, doc.get("error") or f"the scan failed (exit {p.returncode})"
    return doc, None


IPO_TOPICS = ("ipo_priced", "ipo_ahead", "lockups")
ALL_TOPICS = ("summary", "macro", "earnings", *IPO_TOPICS, "insider", "funds", "screens", "mine")


def earnings_week(today):
    """{rows, unsure, partial}: large companies expected to report in the next
    seven days, confirmed by their SEC filings (sec_earnings)."""
    import ipo_calendar
    import sec_directory
    import sec_earnings
    import whats_on_calendar

    hi = today + timedelta(days=IPO_DAYS)
    cal = whats_on_calendar.earnings_days(today, hi)
    if cal["days_total"] and not cal["days_answered"]:
        return {"error": "the calendar could not be read", "calendar": []}
    rows, unsure = sec_earnings.ahead(cal["earnings"], today, hi, ipo_calendar.Cache(), sec_directory.cik)
    return {"rows": rows, "unsure": unsure, "partial": bool(cal["days_failed"]), "calendar": cal["earnings"]}


def one_symbol(s):
    return (s or "").upper().replace("-", ".").replace("/", ".")


def spellings(symbols):
    return sorted({s.replace(".", x) for s in map(one_symbol, symbols) for x in (".", "-", "/")})


def holdings_week(symbols, today, flows, doc, earnings, ipo):
    """The private block's data: this week on the owner's @symbols."""
    import sec_form4_market as f4

    # A share class is BRK.B, BRK-B or BRK/B, by the source.
    mine = {one_symbol(s) for s in symbols}

    def insiders(direction):
        res = f4.recent(days=WINDOW_DAYS, min_insiders=1, min_value=0, symbols=spellings(symbols), direction=direction,
                        limit=500)
        return {one_symbol(r["symbol"]): r for r in res["rows"]}

    ahead = {}
    for r in (earnings or {}).get("calendar") or []:
        if one_symbol(r["symbol"]) in mine:
            ahead.setdefault(one_symbol(r["symbol"]), []).append(f"earnings {_fmt(date.fromisoformat(r['date']), '%a %b %-d')}")
    for r in (ipo or {}).get("lockups") or []:
        if one_symbol(r["ticker"]) in mine:
            ahead.setdefault(one_symbol(r["ticker"]), []).append(f"lock-up ends {_fmt(r['ends'], '%a %b %-d')}")
    by = {one_symbol(k): v for k, v in ((flows or {}).get("by") or {}).items()}
    doc = {"strategies": [{**st, "symbols": [one_symbol(s) for s in st.get("symbols") or []]}
                          for st in (doc or {}).get("strategies") or []]}
    unread = [w for w, missing in (("funds", not by and not (flows or {}).get("quarter")), ("screens", not doc["strategies"]),
                                   ("earnings", not (earnings or {}).get("calendar"))) if missing]
    return {"symbols": len(mine), "unread": unread, "rows": mine_rows(sorted(mine), insiders("buy"), insiders("sell"), by, doc, ahead)}


def serious(sid, warn):
    """What the published issue names as a problem, and the Monday check mails
    about: a part that failed — not a season, nor a calendar with a gap. Never
    anything about the owner's holdings: issue.json is public."""
    return sid != "holdings" and not re.match(r"13F season|partly read|\d series not read$|the calendar for that week", warn)


def build_brief(today, topics=ALL_TOPICS, detail="standard", scan=True, extras=True, progress_path=None,
                symbols=()):
    """The issue's one page and what it was built from: {body, rows, notes,
    complete, series, flows, screens}. Everything is read at once, and each
    part of the page is there as soon as what it needs is. Anything that
    cannot be read is said on the page; none of it holds the issue back.
    Without @extras only the insider section, and nothing is fetched for the
    others."""
    if not extras:
        topics = [t for t in topics if t == "insider"]
    mon, fri = week_ahead(today)
    got = {"series": {}, "doc": None, "scr_err": None, "flows": None, "events": [], "macro_err": None,
           "rows": [], "notes": [], "complete": True, "ipo": None, "earnings": None, "mine": None, "problems": []}

    def economy(say):
        got["series"] = fetch_macro_series(today)
        missing = len(MARKET_SERIES) + len(ECONOMY_SERIES) + len(REGIME_SERIES) - len(got["series"])
        return f"{missing} series not read" if missing else None

    def calendar(say):
        rows, failed = fetch_macro_rows((mon, fri), say=say)
        got["macro_err"] = macro_coverage(rows, mon, fri, failed)
        got["events"] = macro_events(rows, mon, fri)
        return got["macro_err"]

    def screens(say):
        got["doc"], got["scr_err"] = screener_doc(say=say)
        return got["scr_err"]

    def filings(say):
        if scan:  # what is already read, at once: the page fills in from it
            got["rows"], _, _ = clusters_data(False)
            progress.page(page(pending))
        got["rows"], got["notes"], got["complete"] = clusters_data(
            scan, lambda day, n, of, left: say(f"{_fmt(day, '%a %b %-d')}: {n:,} of {of:,}", n / max(1, of)),
            waiting=lambda: say("another scan is reading; waiting for it"))
        say(f"{len(got['rows'])} companies")
        return got["notes"][0] if got["notes"] and not got["complete"] else None

    def funds(say):
        import sec_13f_bulk

        got["flows"] = sec_13f_bulk.stock_flows(symbols=spellings(symbols))
        return got["flows"].get("error")

    def ipos(say):
        got["ipo"] = ipo_week(today)
        if got["ipo"].get("error"):
            return got["ipo"]["error"]
        return "partly read" if got["ipo"].get("partial") else None

    def earnings(say):
        got["earnings"] = earnings_week(today)
        return got["earnings"].get("error") or ("partly read" if got["earnings"]["partial"] else None)

    def holdings(say):
        got["mine"] = holdings_week(symbols, today, got["flows"], got["doc"], got["earnings"], got["ipo"])

    # step, its label, what of the page it feeds, the work
    steps = [s for s, wanted in (
        (("economy", "Economy and rates", "series", economy), {"macro", "summary"} & set(topics)),
        (("calendar", "Release calendar", "events", calendar), {"macro", "summary"} & set(topics)),
        (("screens", "Stock screens", "doc", screens), {"screens", "summary"} & set(topics)),
        (("filings", "SEC Form 4 filings", "rows", filings), True),  # also the evergreen insider page's
        (("funds", "13F fund moves", "flows", funds), "funds" in topics),
        (("earnings", "Earnings ahead", "earnings", earnings), "earnings" in topics),
        (("ipos", "IPOs and lock-ups", "ipo", ipos), bool(set(IPO_TOPICS) & set(topics)))) if wanted]
    symbols = sorted(set(symbols)) if "mine" in topics else []
    # Reads what the others found, so it comes after them.
    last = [("holdings", "Your holdings", "mine", holdings)] if symbols else []
    pending = {feeds for _, _, feeds, _ in steps + last}
    progress = Progress(progress_path, [(sid, label) for sid, label, _, _ in steps + last] + [("page", "Write the page")])

    def page(pending):
        return brief_body(topics, detail, got["series"], got["doc"], got["scr_err"], got["rows"], got["notes"],
                          got["flows"], got["events"], mon, fri, got["macro_err"], today, frozenset(pending), got["ipo"], got["earnings"])

    def run(sid, work):
        progress.step(sid, "run")
        try:
            return work(lambda text, part=None: progress.step(sid, detail=text, part=part))
        except Exception as e:  # noqa: BLE001 — one part never takes the issue with it
            why = type(e).__name__
            print(f"warning: {sid}: {why}: {e}", file=sys.stderr)
            if sid == "filings":
                got.update(complete=False, notes=[f"The SEC filings could not be read this week ({why})."])
            elif sid == "screens":
                got["scr_err"] = why
            elif sid == "funds":
                got["flows"] = {"error": why}
            elif sid == "calendar":
                got["macro_err"] = f"the release calendar could not be read ({why})"
            elif sid == "earnings":
                got["earnings"] = {"error": f"could not be read ({why})", "calendar": []}
            elif sid == "holdings":
                got["mine"] = {"symbols": len(symbols), "rows": [], "unread": [f"all of it ({why})"]}
            elif sid == "ipos":
                got["ipo"] = {"error": f"could not be read ({why})"}
            return why

    def end(sid, feeds, warn):
        if warn:
            print(f"warning: {sid}: {warn}", file=sys.stderr)
            if serious(sid, warn):
                got["problems"].append(f"{sid}: {warn}")
        pending.discard(feeds)
        progress.step(sid, "warn" if warn else "done", warn)
        # (The holdings are shown as the build's, never written into the page.)
        progress.page(page(pending) + (mine_part(got["mine"]) if got["mine"] else []))

    progress.page(page(pending))
    with ThreadPoolExecutor(max_workers=len(steps)) as pool:
        jobs = {pool.submit(run, sid, work): (sid, feeds) for sid, _, feeds, work in steps}
        for job in as_completed(jobs):
            end(*jobs[job], job.result())
    if (got["doc"] or {}).get("fund_day"):
        got["problems"].append(f"screens: the fundamentals are kept ones, of {got['doc']['fund_day']}")
    for sid, _, feeds, work in last:
        end(sid, feeds, run(sid, work))
    progress.step("page", "run")
    regime = market_regime(got["series"], today) if "summary" in topics and got["series"] else None
    if regime and not has_record(load_evidence(), regime["state"]):
        regime = None  # the page states no stance without its record, so there is none to keep
    return {"body": page(set()), **got, "regime": regime, "screens": bool(got["doc"]), "progress": progress}


def ipo_closes(symbols, start, end):
    """{ticker: [(date, close)]} from finterm's analytics Python (yfinance's
    batch_closes); {} when nothing was asked, None when the prices could not
    be read (not installed, or it did not answer)."""
    import subprocess

    if not symbols:
        return {}
    py = analytics_python()
    if not py:
        return None
    code = ("import json, sys; sys.path.insert(0, sys.argv[1]); import yfinance_data as y; "
            "print(json.dumps(y.batch_closes(json.loads(sys.argv[2]), sys.argv[3], sys.argv[4])))")
    try:
        r = subprocess.run([str(py), "-c", code, str(SCRIPTS), json.dumps(sorted(symbols)), start.isoformat(),
                            end.isoformat()], capture_output=True, text=True, timeout=180, stdin=subprocess.DEVNULL)
        doc = json.loads(r.stdout.strip().splitlines()[-1]) if r.stdout.strip() else {}
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return None
    if not isinstance(doc, dict) or doc.get("error"):
        return None
    return {t: [(date.fromisoformat(d), float(c)) for d, c in rows]
            for t, rows in ((doc or {}).get("closes") or {}).items()}


def ipo_week(today):
    """{priced, ahead, lockups, unread}: the past seven days' IPOs with their
    returns, the next seven days' IPOs, and lock-up ends. The IPOs are finterm's
    calendar (PRIVATE MARKET's: Nasdaq's merged per deal, staged on EDGAR); the
    lock-ups are confirmed by the final prospectus (sec_ipo)."""
    import ipo_calendar
    import sec_ipo

    data, cache = ipo_calendar.build(today=today, enrich=True), ipo_calendar.Cache()
    if not data.get("deals"):
        return {"error": "the calendar could not be read"}
    hi = today + timedelta(days=IPO_DAYS)
    lockups, lockups_unread = sec_ipo.lockups(data.get("lockups") or [], today, hi, cache)
    # through today: a deal that priced on the day the brief is made is in it
    priced = ipo_priced(data["deals"], today - timedelta(days=IPO_DAYS), today + timedelta(days=1))
    closes = ipo_closes({p["ticker"] for p in priced if p["ticker"]}, today - timedelta(days=IPO_DAYS + 3),
                        today + timedelta(days=1))
    for p in priced:
        ipo_returns(p, (closes or {}).get(p["ticker"]) or [])
    return {"priced": priced, "ahead": ipo_ahead(data["deals"], today, hi), "lockups": lockups,
            "unread": lockups_unread, "closes_read": closes is not None,
            # a month of the calendar not read at all: "none" is not known (an
            # EDGAR gap or a month served from its cached copy is not that)
            "partial": bool(data.get("months_failed"))}


SECTION_WORDS = {"summary": "the market at a glance", "macro": "markets, economy and the week ahead",
                 "earnings": "earnings ahead", "ipo_priced": "IPOs priced", "ipo_ahead": "IPOs ahead", "lockups": "lock-ups", "insider": "insider buying", "funds": "fund moves",
                 "screens": "screen leaders"}


def write_card(site_dir, today, data):
    """The picture a post of this issue's link shows; its path on the site, or
    None when there is no stance or nothing to draw it with."""
    import brief_card

    r = data.get("regime")
    if not r:
        return None
    m = {x["name"]: x for x in market_rows(data["series"])}
    e = {x["name"]: x for x in economy_rows(data["series"])}
    cells = [(label, m[name]["value"], f"{m[name]['change']} 1w") for label, name in (
        ("10-year Treasury", "10-year Treasury"), ("2-year Treasury", "2-year Treasury"), ("Broad dollar", "Broad dollar index"))
        if m.get(name, {}).get("value", "—") != "—"]
    cells += [(label, e[name]["value"], e[name]["asof"].split(" (")[0]) for label, name in (
        ("Real GDP", "Real GDP, q/q annualised"), ("Core PCE, y/y", "Core PCE, y/y"), ("Unemployment", "Unemployment rate"))
        if e.get(name, {}).get("value", "—") != "—"]
    rel = f"assets/brief/{issue_day(today).isoformat()}.png"
    stance = "full risk" if STANCE[r["state"]] == 1 else "half risk"
    words = regime_words(r)
    ok = brief_card.make(site_dir / rel, f"Weekly brief · {_fmt(today, '%B %-d, %Y')}", r["state"],
                         f"{LABELS[r['state']]}: {stance}".upper(), f"{words} ({_fmt(r['date'], '%b %-d')} close)", cells)
    return rel if ok else None


def write_site(site_dir, rows, notes, today, body, complete=True, topics=ALL_TOPICS, data=None):
    key, url = issue_key(today), issue_url(today)
    posts = site_dir / "_posts"
    posts.mkdir(parents=True, exist_ok=True)
    # A rerun this week replaces the week's issue rather than adding a second.
    for old in posts.glob("*-insider-buying.md"):
        try:
            # file names carry issue_day (the week's Monday), not a run day
            if old.name[:10] == issue_day(today).isoformat():
                old.unlink()
        except ValueError:
            pass

    n = len(rows)
    card = write_card(site_dir, today, data or {}) if topics else None
    # A per-run stamp in the page lets the publisher wait for THIS version to
    # be live, not an earlier deploy of the same URL.
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    # {% raw %}: EDGAR text is data — a "{{" or "{%" in a filer's name must
    # never reach Liquid (it would break the build).
    raw = lambda lines: ["{% raw %}"] + list(lines) + ["{% endraw %}", ""]
    mark = [f"<!-- issue {key} stamp {stamp} -->", ""]
    # Title and description name only the sections the issue holds.
    what = ", ".join(w for t, w in SECTION_WORDS.items() if t in topics)
    # The file name stays "-insider-buying": it is how a rerun finds the
    # week's issue, and the announced URL does not depend on it.
    post = posts / f"{issue_day(today).isoformat()}-insider-buying.md"
    post.write_text(page({
        "title": f"Weekly brief, {_fmt(today, '%B %-d, %Y')}",
        "description": f"{what[0].upper()}{what[1:]} — the week to {_fmt(today, '%B %-d, %Y')}, on one page.",
        # The week's Monday, date only: Jekyll reads it as midnight UTC, so it
        # is never in the future; the post layout shows `updated`.
        "date": issue_day(today),
        "updated": today,
        "compact": True,  # the post layout's one-page style
        **({"image": f"/{card}"} if card else {}),
    }, mark + raw(body), ISSUE_DISCLAIMER), encoding="utf-8")

    # The evergreen insider page: the full list, which the issue links to.
    (site_dir / "insider-cluster-buys.md").write_text(page({
        "title": "Insider buying clusters this week (SEC Form 4)",
        "description": "Companies where two or more insiders reported open-market purchases in the last 7 days "
                       "of SEC Form 4 filings. Updated weekly.",
        "permalink": "/insider-cluster-buys/",
    }, [f"_Updated {_fmt(today, '%B %-d, %Y')} · [this week's issue]({url}) · [all issues]({SITE_URL}/brief/)_", ""]
        + mark + raw(clusters_section(rows, notes, max_rows=len(rows)))), encoding="utf-8")

    # complete/notes: the Social screen's draft review shows them and asks
    # before publishing an incomplete week (--allow-incomplete builds one).
    issue = {"issue": key, "date": today.isoformat(), "stamp": stamp, "count": n,
             "complete": bool(complete), "notes": list(notes),
             "rows": rows[:25], "problems": (data or {}).get("problems", []),
             **({"call": {**brief_track.call_of(data["regime"], STANCE[data["regime"]["state"]],
                                              LABELS[data["regime"]["state"]]),
                         "line": call_head(data["regime"])}} if (data or {}).get("regime") else {}),
             **social_posts(rows, today, url, topics, (data or {}).get("series"), (data or {}).get("flows"),
                            (data or {}).get("screens", False))}
    out = site_dir / "_data"
    out.mkdir(parents=True, exist_ok=True)
    (out / "announce.json").write_text(json.dumps(issue, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    # The same, published beside the pages: the app's Social screen shows and
    # posts exactly this, so a post always matches the page it links to.
    (site_dir / "issue.json").write_text(json.dumps(issue, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return post


def read_symbols(path):
    try:
        got = json.loads(Path(path).read_text(encoding="utf-8")) if path else []
        return [s for s in got if isinstance(s, str) and s] if isinstance(got, list) else []
    except (OSError, ValueError):
        return []


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", help="local mode: output folder (default: finterm's files folder, weekly/)")
    ap.add_argument("--site", help="site mode: the website folder to write into (public: EDGAR data only)")
    ap.add_argument("--symbols", help="a file with the owner's symbols (JSON list), for private.md")
    ap.add_argument("--progress", help="a file to keep the build's progress in, for the app to show")
    ap.add_argument("--no-scan", action="store_true", help="use the Form 4 data already scanned; don't fetch")
    ap.add_argument("--no-extras", action="store_true",
                    help="the insider section only (the others fetch: the screener scan takes minutes) — "
                         "e.g. for a quick offline rebuild with --no-scan")
    ap.add_argument("--allow-incomplete", action="store_true",
                    help="site mode: publish even when some days of filings could not be read")
    ap.add_argument("--topics", default=",".join(ALL_TOPICS),
                    help="comma-separated sections: summary, macro, earnings, ipo_priced, ipo_ahead, lockups, insider, "
                         "funds, screens, mine (drafts); ipo = the three IPO tables")
    ap.add_argument("--detail", default="standard", choices=("full", "standard", "short"),
                    help="rows a table holds: 10, 5 or 3")
    ap.add_argument("--draft", action="store_true", help="site mode, for review in the app: writes private.md beside the site, the owner's holdings "
                         "this week, never part of it")
    args = ap.parse_args()
    topics = [t.strip() for t in args.topics.split(",") if t.strip()]
    # the name before the IPO tables had a box each (a schedule may still pass it)
    topics = [x for t in topics for x in (IPO_TOPICS if t == "ipo" else (t,))]
    bad = [t for t in topics if t not in ALL_TOPICS]
    if bad:
        ap.error(f"unknown topic(s): {', '.join(bad)}")
    if args.no_extras:
        topics = [t for t in topics if t == "insider"]
    if not [t for t in topics if t in SECTION_WORDS and t != "summary"]:
        ap.error("a brief with nothing in it is not written: name a section"
                 + (" (--no-extras leaves only insider)" if args.no_extras else ""))

    if not os.environ.get(CONTACT_KEY):
        c = contact_from_keyring()
        if c:
            os.environ[CONTACT_KEY] = c
    if not os.environ.get(CONTACT_KEY):
        print("note: no SEC contact email found (Settings › Credentials › Contact email); "
              "SEC may refuse the requests.", file=sys.stderr)

    today = date.today()
    week_of = today - timedelta(days=today.weekday())
    if args.site:
        b = build_brief(today, topics, args.detail, scan=not args.no_scan,
                        extras=not args.no_extras, progress_path=args.progress,
                        symbols=read_symbols(args.symbols) if args.draft else [])
        if not b["complete"] and not args.allow_incomplete:
            # A partial week published and announced reads as a quiet week.
            # Fail loudly; the next run resumes where this one stopped.
            for n in b["notes"]:
                print(f"error: {n}", file=sys.stderr)
            print("error: not publishing an incomplete issue (--allow-incomplete overrides)", file=sys.stderr)
            sys.exit(2)
        if b["mine"]:  # beside the site, never in it
            (Path(args.site).parent / "private.md").write_text("\n".join(mine_part(b["mine"])), encoding="utf-8")
        print(write_site(Path(args.site), b["rows"], b["notes"], today, b["body"], complete=b["complete"], topics=topics,
                         data=b))
        b["progress"].step("page", "done")
        b["progress"].finish()
        return

    import finterm_paths

    out_dir = Path(args.out) if args.out else finterm_paths.user_root() / "files" / "weekly"
    out_dir.mkdir(parents=True, exist_ok=True)
    lines = [f"# Weekly brief, {_fmt(today, '%B %-d, %Y')}", ""]
    lines += build_brief(today, topics, args.detail, scan=not args.no_scan, extras=not args.no_extras)["body"]
    lines += ["---", "", f"_{ISSUE_DISCLAIMER}_", ""]
    path = out_dir / f"weekly-{week_of.isoformat()}.md"
    path.write_text("\n".join(lines), encoding="utf-8")
    print(path)


if __name__ == "__main__":
    main()
