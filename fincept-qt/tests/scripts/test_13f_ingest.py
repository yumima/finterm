#!/usr/bin/env python3
"""Bulk 13F ingest — the SQL that turns SEC's data sets into the local index.

Builds a synthetic data set with the exact shape SEC ships and runs the SHIPPED
ingest against a throwaway database. No network.

Every case here is a bug that actually shipped and was caught by running against
real filings, not by reading the schema:

  • A filer reports the SAME CUSIP on several rows, one per internal manager or
    discretion type. Un-aggregated, AAPL showed 8,404 "holders" across 6,005
    filings, and the prior-quarter join multiplied that to 74,973.

  • A filer's 13F-HR and its 13F-HR/A both landed, double-counting the book.

  • Options live in the same table, marked only by PUTCALL. A put is a BEARISH
    position; counting it as stock reports a manager as long a name they may be
    short.

  • SEC moved VALUE from thousands to whole dollars in 2023. Misread, every
    weight is off by 1000x with nothing on screen to show it.

  • An empty CIK joined to another empty CIK matched every such filer.
"""

import io
import os
import shutil
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


def tsv(rows):
    return "\n".join("\t".join(str(c) for c in r) for r in rows) + "\n"


def make_dataset(period="31-MAR-2026", value_scale=1):
    """A data set with every trap in it, shaped exactly like SEC's."""
    submission = [["ACCESSION_NUMBER", "FILING_DATE", "SUBMISSIONTYPE", "CIK", "PERIODOFREPORT"],
                  ["acc-orig", "15-MAY-2026", "13F-HR", "0000000001", period],
                  ["acc-amend", "20-MAY-2026", "13F-HR/A", "0000000001", period],
                  ["acc-other", "15-MAY-2026", "13F-HR", "0000000002", period],
                  ["acc-nocik", "15-MAY-2026", "13F-HR", "", period],
                  ["acc-nocik2", "15-MAY-2026", "13F-HR", "", period]]

    cover_head = ["ACCESSION_NUMBER", "REPORTCALENDARORQUARTER", "ISAMENDMENT", "AMENDMENTNO",
                  "AMENDMENTTYPE", "CONFDENIEDEXPIRED", "DATEDENIEDEXPIRED", "DATEREPORTED",
                  "REASONFORNONCONFIDENTIALITY", "FILINGMANAGER_NAME"]
    cover = [cover_head,
             ["acc-orig", period, "N", "", "", "", "", "", "", "Original Capital LP"],
             ["acc-amend", period, "Y", "1", "RESTATEMENT", "", "", "", "", "Original Capital LP"],
             ["acc-other", period, "N", "", "", "", "", "", "", "Other Advisors LLC"],
             ["acc-nocik", period, "N", "", "", "", "", "", "", "No CIK One"],
             ["acc-nocik2", period, "N", "", "", "", "", "", "", "No CIK Two"]]

    info_head = ["ACCESSION_NUMBER", "INFOTABLE_SK", "NAMEOFISSUER", "TITLEOFCLASS", "CUSIP",
                 "FIGI", "VALUE", "SSHPRNAMT", "SSHPRNAMTTYPE", "PUTCALL",
                 "INVESTMENTDISCRETION", "OTHERMANAGER", "VOTING_AUTH_SOLE",
                 "VOTING_AUTH_SHARED", "VOTING_AUTH_NONE"]
    # Base values are large enough that the thousands case (x0.001) still
    # yields non-zero integers — an all-zero book has no implied price and
    # would test nothing.
    v = lambda x: int(x * 1000 * value_scale)  # noqa: E731
    info = [info_head,
            # Same CUSIP three times in ONE filing — the aggregation trap.
            ["acc-orig", 1, "WIDGET CO", "COM", "111111111", "", v(600), 60000, "SH", "",
             "SOLE", "", 0, 0, 0],
            ["acc-orig", 2, "WIDGET CO", "COM", "111111111", "", v(300), 30000, "SH", "",
             "DFND", "2", 0, 0, 0],
            ["acc-orig", 3, "WIDGET CO", "COM", "111111111", "", v(100), 10000, "SH", "",
             "DFND", "4", 0, 0, 0],
            # A PUT on the same issuer — must not join the share count.
            ["acc-orig", 4, "WIDGET CO", "COM", "111111111", "", v(9999), 777000, "SH", "Put",
             "SOLE", "", 0, 0, 0],
            # A bond line — a principal amount is not a share count.
            ["acc-orig", 5, "BOND ISSUER", "NOTE", "999999999", "", v(500), 1000000, "PRN", "",
             "SOLE", "", 0, 0, 0],
            # The amendment restates the same book; counting both doubles it.
            ["acc-amend", 6, "WIDGET CO", "COM", "111111111", "", v(1000), 100000, "SH", "",
             "SOLE", "", 0, 0, 0],
            ["acc-other", 7, "WIDGET CO", "COM", "111111111", "", v(2000), 200000, "SH", "",
             "SOLE", "", 0, 0, 0],
            ["acc-nocik", 8, "WIDGET CO", "COM", "111111111", "", v(50), 5000, "SH", "",
             "SOLE", "", 0, 0, 0],
            ["acc-nocik2", 9, "WIDGET CO", "COM", "111111111", "", v(70), 7000, "SH", "",
             "SOLE", "", 0, 0, 0]]

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("SUBMISSION.tsv", tsv(submission))
        z.writestr("COVERPAGE.tsv", tsv(cover))
        z.writestr("INFOTABLE.tsv", tsv(info))
    return buf.getvalue()


class FakeResponse:
    def __init__(self, content):
        self.content = content


def run_ingest(bulk, payload):
    """Run the shipped ingest against a synthetic data set."""
    real_get = bulk._get
    bulk._get = lambda url, timeout=300: FakeResponse(payload)
    try:
        return bulk.ingest("http://example.invalid/set.zip")
    finally:
        bulk._get = real_get


def main():
    tmp = tempfile.mkdtemp(prefix="f13test")
    os.environ["FINCEPT_DATA_DIR"] = tmp
    try:
        import sec_13f_bulk as bulk

        res = run_ingest(bulk, make_dataset())
        check("ingest: succeeded", not res.get("error"), str(res.get("error")))
        check("ingest: quarter parsed to ISO", res.get("quarter") == "2026-03-31",
              str(res.get("quarter")))

        con = bulk.connect()
        try:
            # ── amendment dedup ────────────────────────────────────────────
            accs = {r[0] for r in con.execute(
                "SELECT accession FROM filings WHERE cik='0000000001'").fetchall()}
            check("amendment: one filing kept per filer-quarter", len(accs) == 1, str(accs))
            check("amendment: the ORIGINAL is kept, not the /A",
                  accs == {"acc-orig"}, str(accs))

            # ── CUSIP aggregation within a filing ──────────────────────────
            rows = con.execute(
                "SELECT shares, value FROM holdings "
                "WHERE accession='acc-orig' AND cusip='111111111' AND put_call=''").fetchall()
            check("aggregate: one stock row per security per filing",
                  len(rows) == 1, str(rows))
            check("aggregate: shares summed across the three rows",
                  rows and rows[0][0] == 100000.0, str(rows))
            check("aggregate: value summed across the three rows",
                  rows and rows[0][1] == 1000000.0, str(rows))

            # ── options kept apart ─────────────────────────────────────────
            puts = con.execute(
                "SELECT shares FROM holdings WHERE accession='acc-orig' "
                "AND cusip='111111111' AND put_call='PUT'").fetchall()
            check("options: the put survives as its own row", len(puts) == 1, str(puts))
            check("options: put shares did NOT inflate the stock line",
                  rows and rows[0][0] == 100000.0)

            # ── bonds excluded ─────────────────────────────────────────────
            bonds = con.execute(
                "SELECT COUNT(*) FROM holdings WHERE cusip='999999999'").fetchone()[0]
            check("bonds: principal-amount row dropped", bonds == 0, str(bonds))

            # ── book totals exclude options ────────────────────────────────
            bk = con.execute(
                "SELECT stock_value, stock_count FROM books "
                "WHERE accession='acc-orig'").fetchone()
            check("book: value excludes the put", bk and bk[0] == 1000000.0, str(bk))
            check("book: count excludes the put and the bond", bk and bk[1] == 1, str(bk))

            # ── holders query ──────────────────────────────────────────────
            h = bulk.holders(cusip="111111111", min_book=0, min_positions=0)
            names = sorted(x["manager"] for x in h["holders"] if not x["is_derivative"])
            check("holders: one row per filer, amendment not double-counted",
                  names == ["No CIK One", "No CIK Two", "Original Capital LP",
                            "Other Advisors LLC"], str(names))
            check("holders: total shares summed correctly",
                  h["total_shares_held"] == 100000 + 200000 + 5000 + 7000,
                  str(h["total_shares_held"]))
            check("holders: empty CIKs did not join to each other",
                  h["holder_count"] == 4, str(h["holder_count"]))
        finally:
            con.close()

        # ── value units ────────────────────────────────────────────────────
        # The same book with values in THOUSANDS: 100 shares reported as 1
        # implies $0.01/share, which no equity trades at.
        tmp2 = tempfile.mkdtemp(prefix="f13units")
        os.environ["FINCEPT_DATA_DIR"] = tmp2
        import importlib
        importlib.reload(bulk)
        res2 = run_ingest(bulk, make_dataset(value_scale=0.001))
        check("units: thousands detected and scaled",
              res2.get("value_basis", "").startswith("thousands"), str(res2.get("value_basis")))
        con2 = bulk.connect()
        try:
            v = con2.execute("SELECT value FROM holdings WHERE accession='acc-orig' "
                             "AND cusip='111111111' AND put_call=''").fetchone()
            check("units: value restored to whole dollars", v and abs(v[0] - 1000000.0) < 1.0,
                  str(v))
        finally:
            con2.close()
        shutil.rmtree(tmp2, ignore_errors=True)

    except Exception:
        import traceback
        traceback.print_exc()
        FAILURES.append("exception")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print()
    if FAILURES:
        print(f"{len(FAILURES)} failure(s): {', '.join(FAILURES)}")
        return 1
    print("all 13F ingest tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
