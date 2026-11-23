"""Valuation series the Markets screens read that no quote feed carries.

  cape          Robert Shiller's cyclically-adjusted P/E (CAPE), monthly, from
                his own data file (shillerdata.com — the Yale ie_data.xls
                stopped updating in 2023). The page is read for the file's
                current link (it is a versioned blob URL), then the "Data"
                sheet's Date and CAPE columns.
                -> {"points": [["YYYY-MM-01", 39.37], ...], "source": ...}

  mstr_btc      Strategy's (MSTR) bitcoin holdings, from its weekly 8-K
                "BTC Update" (SEC EDGAR full-text search, newest filing first).
                -> {"btc": 850849, "as_of": "2026-11-15", "filed": "2026-11-16",
                    "url": ...}

Every failure is {"error": ...} — never an empty or made-up series. Values are
read from the documents as published; nothing is estimated.

Usage:  python market_valuation_data.py cape | mstr_btc
"""

import io
import json
import re
import sys
from datetime import date
from html import unescape

import requests

from finterm_identity import project_user_agent

SHILLER_PAGE = "https://shillerdata.com/"
MSTR_CIK = "0001050446"
EFTS = "https://efts.sec.gov/LATEST/search-index"


def _shiller_link(page_html: str):
    m = re.search(r'(?:https?:)?//[^"\']+/ie_data\.xls[^"\']*', page_html)
    if not m:
        return None
    url = m.group(0)
    return "https:" + url if url.startswith("//") else url


def parse_cape_rows(rows):
    """Rows of the Data sheet → [[YYYY-MM-01, cape]], oldest first. The Date
    column is a float year.month (2026.1 = October, 2026.01 = January); a
    blank / "NA" CAPE is skipped (absent, never 0)."""
    header = next((i for i, r in enumerate(rows) if r and str(r[0]).strip() == "Date"), None)
    if header is None:
        return []
    names = [str(c).strip() for c in rows[header]]
    if "CAPE" not in names:
        return []
    ci = names.index("CAPE")
    out = []
    for r in rows[header + 1:]:
        if len(r) <= ci or not isinstance(r[0], float) or r[0] < 1800:
            continue
        year = int(r[0])
        month = int(round((r[0] - year) * 100))
        v = r[ci]
        if not (1 <= month <= 12) or not isinstance(v, (int, float)) or v != v or v <= 0:
            continue
        out.append([date(year, month, 1).isoformat(), round(float(v), 4)])
    out.sort(key=lambda p: p[0])
    return out


def cape():
    s = requests.Session()
    s.headers["User-Agent"] = project_user_agent()
    try:
        page = s.get(SHILLER_PAGE, timeout=30)
        page.raise_for_status()
    except requests.exceptions.RequestException as e:
        return {"error": f"shillerdata.com did not answer: {e}"}
    link = _shiller_link(page.text)
    if not link:
        return {"error": "shillerdata.com no longer links ie_data.xls"}
    try:
        f = s.get(link, timeout=60)
        f.raise_for_status()
    except requests.exceptions.RequestException as e:
        return {"error": f"Shiller data file did not download: {e}"}
    try:
        import xlrd
        book = xlrd.open_workbook(file_contents=f.content)
        sheet = book.sheet_by_name("Data")
        rows = [sheet.row_values(i) for i in range(sheet.nrows)]
    except Exception as e:  # xlrd missing, or not the workbook we expect
        return {"error": f"Shiller data file not readable: {e}"}
    points = parse_cape_rows(rows)
    if not points:
        return {"error": "no CAPE column / values in the Shiller data file"}
    return {"points": points, "source": "Robert Shiller, shillerdata.com (ie_data.xls)", "url": link}


_BTC_UPDATE = re.compile(
    r"As of (?P<asof>[A-Z][a-z]+ \d{1,2}, \d{4}).{0,400}?Aggregate BTC Holdings.{0,300}?"
    r"Average Purchase Price(?: \(\d\))?\s+(?P<nums>(?:\$?\s*[\d,]+(?:\.\d+)?\s+){3,6})",
    re.S)


def parse_btc_update(text: str):
    """The "BTC Update" table of a Strategy 8-K → (holdings, as_of ISO) or
    None. Layout (Nov 2026): purchased, $ price (m), $ avg, AGGREGATE HOLDINGS,
    $ aggregate (bn), $ avg — the fourth number is the holding."""
    t = re.sub(r"\s+", " ", unescape(re.sub(r"<[^>]+>", " ", text)))
    m = _BTC_UPDATE.search(t)
    if not m:
        return None
    nums = re.findall(r"[\d,]+(?:\.\d+)?", m.group("nums"))
    if len(nums) < 4:
        return None
    try:
        held = int(nums[3].replace(",", ""))
        from datetime import datetime
        as_of = datetime.strptime(m.group("asof"), "%B %d, %Y").date().isoformat()
    except ValueError:
        return None
    # A holding is a whole number of coins in a plausible range; anything else
    # is a misread of the table, not a figure.
    if not (10_000 <= held <= 5_000_000) or "." in nums[3]:
        return None
    return held, as_of


def mstr_btc():
    import sec_http
    resp, status = sec_http.get(EFTS, timeout=30, params={
        "q": '"BTC Update"', "forms": "8-K", "ciks": MSTR_CIK,
        "dateRange": "custom", "startdt": date.fromordinal(date.today().toordinal() - 60).isoformat(),
    })
    if resp is None:
        return {"error": f"SEC full-text search refused or did not answer (status {status})"}
    hits = resp.json().get("hits", {}).get("hits", [])
    hits.sort(key=lambda h: h.get("_source", {}).get("file_date", ""), reverse=True)
    for h in hits[:6]:
        adsh, _, doc = h.get("_id", "").partition(":")
        if not doc or "ex" in doc.lower():
            continue
        url = f"https://www.sec.gov/Archives/edgar/data/{int(MSTR_CIK)}/{adsh.replace('-', '')}/{doc}"
        r, st = sec_http.get(url, timeout=30)
        if r is None:
            continue
        got = parse_btc_update(r.text)
        if got:
            return {"btc": got[0], "as_of": got[1], "filed": h["_source"].get("file_date"), "url": url,
                    "source": "Strategy 8-K BTC Update (SEC EDGAR)"}
    return {"error": "no Strategy 8-K with a readable BTC Update in the last 60 days"}


def main(args=None):
    args = sys.argv[1:] if args is None else args
    cmd = args[0] if args else ""
    if cmd == "cape":
        out = cape()
    elif cmd == "mstr_btc":
        out = mstr_btc()
    else:
        out = {"error": f"unknown command: {cmd!r}"}
    print(json.dumps(out))


if __name__ == "__main__":
    main()
