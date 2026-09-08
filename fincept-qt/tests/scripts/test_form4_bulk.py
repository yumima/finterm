#!/usr/bin/env python3
"""Twelve months of Form 4 — the ingest and the check, on a synthetic data set.

Runs the SHIPPED functions from sec_form4_bulk against a throwaway store,
with a zip shaped exactly like SEC's and closes planted by hand. No network.

  • Ingest keeps P and S rows only, carries the 10% flag from ANY reporting
    owner on the filing and the 10b5-1 flag from the submission, and turns a
    two-digit SEC year into no date rather than a date in the year 25.
  • Forward returns come from the close on the trade date; a window that has
    not elapsed is absent.
  • The evaluation anchors on the FILING date (the day a reader could act),
    puts each event in exactly one category, marks a cluster only when a
    DIFFERENT owner's scorable buy was already on file inside the trailing
    30 days — so the first buy of a run is a single buy — and reports excess
    over the benchmark. An amendment (4/A) is not a second trade.
"""

import io
import os
import sys
import tempfile
import zipfile
from datetime import date

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


def make_zip():
    submission = [["ACCESSION_NUMBER", "FILING_DATE", "PERIOD_OF_REPORT", "DOCUMENT_TYPE", "ISSUERCIK",
                   "ISSUERNAME", "ISSUERTRADINGSYMBOL", "AFF10B5ONE"],
                  ["acc-a", "03-MAR-2026", "01-MAR-2026", "4", "1", "Alpha Inc", "ALFA", "0"],
                  ["acc-b", "10-MAR-2026", "08-MAR-2026", "4", "1", "Alpha Inc", "ALFA", "0"],
                  ["acc-c", "12-MAR-2026", "10-MAR-2026", "4", "1", "Alpha Inc", "ALFA", "1"],
                  ["acc-d", "12-MAR-2026", "10-MAR-2026", "4", "2", "Beta Co", "BETA", "0"],
                  ["acc-e", "20-MAR-2026", "18-MAR-2026", "4", "2", "Beta Co", "BETA", "0"],
                  ["acc-f", "25-JUL-0025", "20-JUL-0025", "4", "2", "Beta Co", "BETA", "0"],
                  ["acc-3", "05-MAR-2026", "05-MAR-2026", "3", "1", "Alpha Inc", "ALFA", "0"],
                  ["acc-a2", "04-MAR-2026", "01-MAR-2026", "4/A", "1", "Alpha Inc", "ALFA", "0"]]
    owners = [["ACCESSION_NUMBER", "RPTOWNERCIK", "RPTOWNERNAME", "RPTOWNER_RELATIONSHIP", "RPTOWNER_TITLE"],
              ["acc-a", "11", "Ann", "Director", ""],
              ["acc-b", "12", "Bob", "Officer", "CEO"],
              ["acc-c", "12", "Bob", "Officer", "CEO"],
              ["acc-d", "21", "Fund LP", "TenPercentOwner", ""],
              ["acc-d", "22", "Fund GP", "Director", ""],      # joint filer: the 10% flag must win
              ["acc-e", "23", "Eve", "Director", ""],
              ["acc-f", "23", "Eve", "Director", ""],
              ["acc-a2", "11", "Ann", "Director", ""]]
    trans = [["ACCESSION_NUMBER", "NONDERIV_TRANS_SK", "SECURITY_TITLE", "TRANS_DATE", "TRANS_CODE",
              "TRANS_SHARES", "TRANS_PRICEPERSHARE", "TRANS_ACQUIRED_DISP_CD", "SHRS_OWND_FOLWNG_TRANS"],
             ["acc-a", "1", "Common", "02-MAR-2026", "P", "100", "10.0", "A", "1100"],
             ["acc-b", "2", "Common", "09-MAR-2026", "P", "200", "11.0", "A", "5200"],
             ["acc-c", "3", "Common", "10-MAR-2026", "P", "50", "11.5", "A", "5250"],
             ["acc-c", "4", "Common", "10-MAR-2026", "A", "500", "", "A", "5750"],      # a grant: not kept
             ["acc-d", "5", "Common", "10-MAR-2026", "P", "10000", "5.0", "A", "90000"],
             ["acc-e", "6", "Common", "18-MAR-2026", "S", "300", "6.0", "D", "700"],
             ["acc-f", "7", "Common", "20-JUL-0025", "P", "10", "1.0", "A", "10"],
             ["acc-a2", "8", "Common", "02-MAR-2026", "P", "100", "10.0", "A", "1100"]]
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("SUBMISSION.tsv", tsv(submission))
        z.writestr("REPORTINGOWNER.tsv", tsv(owners))
        z.writestr("NONDERIV_TRANS.tsv", tsv(trans))
    return buf.getvalue()


def main():
    tmp = tempfile.mkdtemp(prefix="f4bulk")
    os.environ["FINCEPT_DATA_DIR"] = tmp
    import sec_form4_bulk as fb

    r = fb.ingest_dataset("2026q1", "unused", data=make_zip())
    check("ingest: P and S rows kept; the grant, the Form 3 and the 4/A amendment dropped", r.get("rows") == 6, str(r))
    con = fb.connect()
    rows = {a: (sym, td, ten, plan, code, acq) for a, sym, td, ten, plan, code, acq in con.execute(
        "SELECT accession, symbol, trans_date, ten_pct, plan, code, acquired FROM tx")}
    check("ingest: dates converted to ISO", rows["acc-a"][1] == "2026-03-02", str(rows["acc-a"]))
    check("ingest: 10% flag set when ANY owner on the filing is one", rows["acc-d"][2] == 1, str(rows["acc-d"]))
    check("ingest: 10b5-1 flag from the submission", rows["acc-c"][3] == 1 and rows["acc-b"][3] == 0, str(rows["acc-c"]))
    check("ingest: a two-digit SEC year becomes no date", rows["acc-f"][1] == "", str(rows["acc-f"]))
    check("ingest: a second run is skipped", fb.ingest_dataset("2026q1", "unused", data=make_zip()).get("skipped") is True)

    # ── forward returns from planted closes ────────────────────────────────
    series = [("2026-03-02", 10.0), ("2026-03-09", 11.0), ("2026-03-10", 12.0), ("2026-04-01", 15.0),
              ("2026-04-08", 16.0), ("2026-06-01", 20.0), ("2026-06-10", 24.0)]
    today = date(2026, 6, 15)
    fr = fb.forward_returns(series, "2026-03-02", today)
    check("returns: 1w from the trade-date close (10 -> 11 on 9 Mar)", abs(fr["1w"] - 0.10) < 1e-9, str(fr))
    check("returns: 1m uses the last close on or before 1 Apr (15)", abs(fr["1m"] - 0.50) < 1e-9, str(fr))
    check("returns: 3m (1 Jun, 20)", abs(fr["3m"] - 1.0) < 1e-9, str(fr))
    fr2 = fb.forward_returns(series, "2026-06-10", today)
    check("returns: a window not yet elapsed is absent", "1w" not in fr2 and "1m" not in fr2, str(fr2))
    check("returns: a trade before any close has no base", fb.forward_returns(series, "2026-01-01", today) == {})

    # ── the evaluation, with the benchmark flat ────────────────────────────
    bench = [(d, 100.0) for d, _ in series]
    con.executemany("INSERT OR REPLACE INTO closes VALUES (?,?,?)",
                    [("ALFA", d, c) for d, c in series] + [("BETA", d, c) for d, c in series] +
                    [("SPY", d, c) for d, c in bench])
    for s in ("ALFA", "BETA", "SPY"):
        con.execute("INSERT OR REPLACE INTO price_runs VALUES (?,?,?,?)", (s, "2025-01-01", "2027-01-01", "now"))
    con.commit()
    rep = fb.evaluate(months=12, benchmark="SPY", today=today, con=con)
    cats = rep["categories"]
    # Ann files 3 Mar with nobody before her: a single buy. Bob files 10 Mar
    # with Ann's buy already on file: a cluster. Point in time, not hindsight.
    check("evaluate: the first buy of a run is a single buy, the second is the cluster",
          cats["cluster_buy"]["events"] == 1 and cats["single_buy"]["events"] == 1, str({k: v["events"] for k, v in cats.items()}))
    check("evaluate: the plan buy and the 10% owner buy sit in their own categories",
          cats["plan_buy"]["events"] == 1 and cats["ten_pct_buy"]["events"] == 1, str({k: v["events"] for k, v in cats.items()}))
    check("evaluate: the sell is counted, the undated row is not", cats["sell"]["events"] == 1 and rep["events"] == 5, str(rep["events"]))
    ex = cats["buy"]["excess"]["1m"]
    # Anchored on the FILING date. Ann filed 3 Mar: base is the last close on
    # or before it (10, on 2 Mar), one month lands on 2 Apr (15). Bob filed
    # 10 Mar: base 12, one month lands on 9 Apr (16). A flat benchmark leaves
    # the raw returns as the excess.
    check("evaluate: returns run from the filing date, excess over a flat benchmark",
          abs(ex["mean"] - ((15 / 10 - 1) + (16 / 12 - 1)) / 2) < 1e-9 and ex["n"] == 2, str(ex))
    check("evaluate: hit rate", ex["hit_rate"] == 1.0, str(ex))
    con.close()

    print()
    if FAILURES:
        print(f"{len(FAILURES)} FAILED: " + ", ".join(FAILURES))
        sys.exit(1)
    print("all passed")


if __name__ == "__main__":
    main()
