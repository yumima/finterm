#!/usr/bin/env python3
"""Market-wide 5% stakes — every Schedule 13D and 13G filed in the last weeks.

A 13D is filed by anyone who crosses 5% of a company's stock with a view to
influencing it — activists, acquirers, a founder regaining control — within
five business days of crossing (two for an amendment). A 13G is the passive
version: index funds and other holders with no such intent. Either is far
fresher than 13F, which can be 135 days old; and a 13D is the ownership
signal research has found to carry the most information at filing.

WHAT IS READ, AND WHAT IS NOT
-----------------------------
Since December 2024 these schedules are filed as structured XML, so the
percentage owned, the share count, the event date and both parties are fields,
not prose. Read in full:
  * every 13D and 13D amendment (dozens a day);
  * every NEW 13G.
13G AMENDMENTS are listed from the daily index only — on a deadline day there
are over a thousand, nearly all large passive holders restating a position —
and are not opened: their percentage is shown as unknown, never guessed.
Older free-text schedules ("SC 13D") are listed with their parties from the
filing header; their percentage lives in prose and is not extracted.

Walked day by day from EDGAR's daily index like the Form 4 scan: a day is
recorded only when every filing on it was read, a refusal leaves it to be
retried, and a day with no index (SEC closed) is recorded only once it is a
few days old.
"""

import json
import os
import re
import sqlite3
import sys
from datetime import date, timedelta

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sec_http  # noqa: E402  (the one SEC client)
from sec_ownership_data import _pad_cik  # noqa: E402
from finterm_paths import market_data_path  # noqa: E402

BASE = "https://www.sec.gov/Archives"

STAKE_FORMS = ("SCHEDULE 13D", "SCHEDULE 13D/A", "SCHEDULE 13G", "SCHEDULE 13G/A",
               "SC 13D", "SC 13D/A", "SC 13G", "SC 13G/A")


def db_path():
    return market_data_path("stakes.sqlite")


def connect():
    con = sqlite3.connect(db_path())
    con.execute("PRAGMA journal_mode=WAL")
    con.execute("PRAGMA busy_timeout=15000")
    con.execute("""CREATE TABLE IF NOT EXISTS stakes (
        accession TEXT PRIMARY KEY, form TEXT, filed TEXT,
        subject_cik TEXT, subject_name TEXT, ticker TEXT,
        filer_cik TEXT, filer_name TEXT,
        pct REAL, shares REAL, event_date TEXT,
        activist INTEGER, amendment INTEGER, opened INTEGER, url TEXT)""")
    con.execute("CREATE INDEX IF NOT EXISTS ix_stakes_filed ON stakes(filed)")
    con.execute("CREATE INDEX IF NOT EXISTS ix_stakes_pair ON stakes(subject_cik, filer_cik, filed)")
    con.execute("CREATE TABLE IF NOT EXISTS scanned_days (filed TEXT PRIMARY KEY)")
    con.commit()
    return con


def norm_form(form):
    """'SCHEDULE 13D/A' and 'SC 13D/A' are the same schedule."""
    f = (form or "").upper().strip()
    return "SC " + f[len("SCHEDULE "):] if f.startswith("SCHEDULE ") else f


def is_opened(form):
    """Whether a filing is read in full (see the module note)."""
    f = norm_form(form)
    return f in ("SC 13D", "SC 13D/A", "SC 13G")


def parse_index(text):
    """{accession: (form, path)} for the stake schedules in one daily form
    index. A schedule is listed under its subject AND its filer; the dict
    keeps it once."""
    out = {}
    for line in (text or "").splitlines():
        form = next((f for f in STAKE_FORMS if line.startswith(f + " ")), None)
        if not form:
            continue
        m = re.search(r"(edgar/data/\d+/(\d{10}-\d{2}-\d{6})\.txt)", line)
        if m:
            out[m.group(2)] = (form, m.group(1))
    return out


_PARTY_RE = re.compile(r"(SUBJECT COMPANY|FILED BY):(.*?)CENTRAL INDEX KEY:\s*(\d+)", re.S)
_NAME_RE = re.compile(r"COMPANY CONFORMED NAME:\s*(.+)")


def parse_header(text):
    """(subject_cik, subject_name, filer_cik, filer_name) from the SGML header."""
    subject = filer = ("", "")
    for role, block, cik in _PARTY_RE.findall(text or ""):
        name = (_NAME_RE.search(block).group(1).strip() if _NAME_RE.search(block) else "")
        if role == "SUBJECT COMPANY" and not subject[0]:
            subject = (_pad_cik(cik), name)
        elif role == "FILED BY" and not filer[0]:
            filer = (_pad_cik(cik), name)
    return subject[0], subject[1], filer[0], filer[1]


def _tag(text, name):
    m = re.search(r"<(?:\w+:)?%s>\s*(.*?)\s*</(?:\w+:)?%s>" % (name, name), text, re.S)
    return m.group(1).strip() if m else ""


def parse_structured(text):
    """(pct, shares, event_date ISO) from a structured schedule; None for
    each field it does not carry. A group filing repeats the reporting-person
    block, one per member of the group: the group's stake is the largest."""
    pcts = [float(x) for x in re.findall(r"<(?:\w+:)?percentOfClass>\s*([\d.]+)\s*<", text or "")]
    amounts = [float(x) for x in re.findall(r"<(?:\w+:)?aggregateAmountOwned>\s*([\d.]+)\s*<", text or "")]
    pct = max(pcts) if pcts else None
    shares = amounts[pcts.index(pct)] if (pcts and len(amounts) == len(pcts)) else (max(amounts) if amounts else None)
    ev = _tag(text or "", "dateOfEvent") or _tag(text or "", "eventDateRequiresFilingThisStatement")
    m = re.match(r"(\d{2})/(\d{2})/(\d{4})", ev)
    event = f"{m.group(3)}-{m.group(1)}-{m.group(2)}" if m else ""
    return pct, shares, event


def ticker_for_cik(cik):
    """The SEC's own CIK -> ticker list, from sec_directory (the app's one
    copy; a registrant's first row is its primary listing). A refusal is not
    an empty list: the row stays tickerless and the next scan fills it in."""
    import sec_directory
    try:
        rows = sec_directory.by_cik().get(int(_pad_cik(cik) or 0))
    except Exception:
        return ""
    return rows[0]["ticker"] if rows else ""


def fill_missing_tickers(con):
    """Rows stored while the ticker list could not be read get theirs now."""
    rows = con.execute("SELECT DISTINCT subject_cik FROM stakes WHERE ticker='' AND subject_cik<>''").fetchall()
    for (cik,) in rows:
        t = ticker_for_cik(cik)
        if t:
            con.execute("UPDATE stakes SET ticker=? WHERE subject_cik=? AND ticker=''", (t, cik))
    con.commit()


def _url(path):
    m = re.search(r"edgar/data/(\d+)/(\d{10})-(\d{2})-(\d{6})\.txt", path)
    if not m:
        return f"{BASE}/{path}"
    acc = f"{m.group(2)}-{m.group(3)}-{m.group(4)}"
    return f"{BASE}/edgar/data/{m.group(1)}/{acc.replace('-', '')}/{acc}-index.htm"


def business_days(n):
    d, out = date.today(), []
    while len(out) < n:
        if d.weekday() < 5:
            out.append(d)
        d -= timedelta(days=1)
    return out


def day_index(d):
    q = (d.month - 1) // 3 + 1
    r, status = sec_http.get(f"{BASE}/edgar/daily-index/{d.year}/QTR{q}/form.{d.strftime('%Y%m%d')}.idx")
    if r is None:
        return {} if status == 404 else None
    return parse_index(r.text)


def scan(days=30, max_new_days=None):
    """Read the stake schedules of the last `days` business days into the
    store, oldest unread day first; `max_new_days` bounds one pass."""
    con = connect()
    try:
        fill_missing_tickers(con)
        done = {r[0] for r in con.execute("SELECT filed FROM scanned_days")}
        not_yet = set()  # weekdays with no index yet (a holiday, or not published): not "remaining"
        wanted = sorted(d for d in business_days(int(days)) if d.isoformat() not in done)
        budget = int(max_new_days) if max_new_days else len(wanted)
        read = failed = listed = opened = 0
        failed_days = []
        for d in wanted:
            if budget <= 0:
                break
            filed = d.isoformat()
            idx = day_index(d)
            if idx is None:
                failed_days.append(filed)
                break
            if not idx:
                if (date.today() - d).days >= 3:
                    con.execute("INSERT OR REPLACE INTO scanned_days VALUES (?)", (filed,))
                    con.commit()
                else:
                    not_yet.add(filed)
                continue
            budget -= 1
            have = {a for (a,) in con.execute("SELECT accession FROM stakes WHERE filed=?", (filed,))}
            day_failed = 0
            for n, (acc, (form, path)) in enumerate(idx.items(), 1):
                if acc in have:
                    continue
                nf = norm_form(form)
                activist = nf.startswith("SC 13D")
                amendment = nf.endswith("/A")
                url = _url(path)
                if not is_opened(form):
                    # Listed from the index alone: parties come from the
                    # index line's company, which may be either side — so
                    # these rows are kept only for the subject's own lookups.
                    con.execute("INSERT OR REPLACE INTO stakes VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                                (acc, nf, filed, "", "", "", "", "", None, None, "",
                                 1 if activist else 0, 1 if amendment else 0, 0, url))
                    listed += 1
                    continue
                r, status = sec_http.get(f"{BASE}/{path}")
                read += 1
                if r is None and status == 404:
                    con.execute("INSERT OR REPLACE INTO stakes VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                                (acc, nf, filed, "", "", "", "", "", None, None, "",
                                 1 if activist else 0, 1 if amendment else 0, 0, url))
                    continue
                if r is None:
                    day_failed += 1  # refused or offline: read again next pass
                    continue
                text = r.text
                s_cik, s_name, f_cik, f_name = parse_header(text)
                pct, shares, event = parse_structured(text) if form.startswith("SCHEDULE") else (None, None, "")
                con.execute("INSERT OR REPLACE INTO stakes VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                            (acc, nf, filed, s_cik, s_name, ticker_for_cik(s_cik) if s_cik else "",
                             f_cik, f_name, pct, shares, event,
                             1 if activist else 0, 1 if amendment else 0, 1, url))
                opened += 1
                if n % 25 == 0:
                    con.commit()
            if day_failed:
                failed += day_failed
                failed_days.append(filed)
            else:
                con.execute("INSERT OR REPLACE INTO scanned_days VALUES (?)", (filed,))
            con.commit()
        recorded = {r[0] for r in con.execute("SELECT filed FROM scanned_days")}
        out = {"read": read, "opened": opened, "listed_only": listed,
               # A missing index of the last few days is not work left to do:
               # counting it rescheduled the scan every two minutes over a holiday.
               "days_remaining": sum(1 for d in wanted if d.isoformat() not in recorded
                                     and d.isoformat() not in not_yet and d < date.today()),
               "failed_days": sorted(set(failed_days)), "failed_filings": failed}
        if failed_days:
            out["error"] = (f"EDGAR did not answer for {len(set(failed_days))} day(s) — "
                            "they are retried, not skipped")
            out["transient"] = True
        return out
    except sqlite3.OperationalError as e:
        if "locked" not in str(e).lower():
            raise
        return {"error": "the stakes store is locked by another scan — will retry", "transient": True}
    finally:
        con.close()


def recent(days=30, kind="all", new_only=False, limit=300, symbols=None):
    """Opened stake filings of the last `days` calendar days, newest first,
    each with the same filer's previous percentage in the same company (so an
    amendment shows what changed)."""
    con = connect()
    try:
        since = (date.today() - timedelta(days=int(days))).isoformat()
        where = ["filed >= ?", "opened = 1", "subject_cik <> ''"]
        args = [since]
        if kind == "13d":
            where.append("activist = 1")
        elif kind == "13g":
            where.append("activist = 0")
        if new_only:
            where.append("amendment = 0")
        if symbols:
            syms = [str(s).upper() for s in symbols if s]
            where.append(f"ticker IN ({','.join('?' * len(syms))})")
            args += syms
        rows = con.execute(f"""
            SELECT accession, form, filed, subject_cik, subject_name, ticker, filer_cik, filer_name,
                   pct, shares, event_date, activist, amendment, url
              FROM stakes WHERE {' AND '.join(where)}
             ORDER BY filed DESC, activist DESC, pct DESC LIMIT ?""", args + [int(limit)]).fetchall()
        out = []
        for r in rows:
            prev = con.execute("""
                SELECT pct, filed FROM stakes
                 WHERE subject_cik=? AND filer_cik=? AND filed < ? AND pct IS NOT NULL
                 ORDER BY filed DESC LIMIT 1""", (r[3], r[6], r[2])).fetchone()
            out.append({"accession": r[0], "form": r[1], "filed": r[2], "subject_cik": r[3],
                        "company": r[4], "ticker": r[5], "filer_cik": r[6], "filer": r[7],
                        "pct": r[8], "shares": r[9], "event_date": r[10],
                        "activist": bool(r[11]), "amendment": bool(r[12]), "url": r[13],
                        "prev_pct": prev[0] if prev else None, "prev_filed": prev[1] if prev else ""})
        newest = con.execute("SELECT MAX(filed) FROM scanned_days").fetchone()[0]
        return {"rows": out, "days": int(days), "kind": kind, "new_only": bool(new_only),
                "read_through": newest or ""}
    finally:
        con.close()


def handle_action(action, p):
    if action == "scan":
        return scan(p.get("days") or 30, p.get("max_new_days"))
    if action == "recent":
        return recent(p.get("days") or 30, p.get("kind") or "all", bool(p.get("new_only")),
                      p.get("limit") or 300, p.get("symbols"))
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    act = sys.argv[1] if len(sys.argv) > 1 else "recent"
    payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    print(json.dumps(handle_action(act, payload), indent=2, default=str))
