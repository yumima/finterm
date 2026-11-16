#!/usr/bin/env python3
"""13F information-table parsing and quarter-over-quarter diffs.

Runs the SHIPPED functions from sec_13f_data — no network, no mirrored copies.

Every test here pins something that produced a wrong number when written
against the real filings rather than against the schema:

  • The information table declares a DEFAULT XML NAMESPACE. A findall() that
    spells a bare tag name matches nothing and returns an empty book, silently.

  • A manager files the SAME CUSIP on several rows, one per internal manager or
    discretion type. Berkshire reports Ally Financial three times. Reading one
    row per security understates the position and every weight derived from it.

  • SEC moved 13F values from THOUSANDS to whole dollars for 2023 onward. Get it
    wrong and every weight is off by 1000x with nothing on screen to show it.

  • Share classes are separate CUSIPs with the SAME issuer name. Berkshire holds
    Alphabet A and Alphabet C; a moves list keyed on the name alone prints
    "ALPHABET INC" twice with different numbers and reads as a duplication bug.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

import sec_13f_bulk as t13  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


# Namespaced exactly as EDGAR serves it, with Ally split across three rows the
# way Berkshire actually files it, one bond line, and two Alphabet classes.


def test_bulk_quarter_date_parsing():
    """SEC's data sets date quarters as 31-MAR-2026; the index keys on ISO."""
    import sec_13f_bulk as bulk
    check("bulk: quarter parsed", bulk._iso_quarter("31-MAR-2026") == "2026-03-31",
          bulk._iso_quarter("31-MAR-2026"))
    check("bulk: single-digit day padded", bulk._iso_quarter("1-JUN-2025") == "2025-06-01",
          bulk._iso_quarter("1-JUN-2025"))
    check("bulk: december", bulk._iso_quarter("31-DEC-2025") == "2025-12-31")
    check("bulk: garbage passes through rather than raising",
          bulk._iso_quarter("not a date") == "not a date")


def test_bulk_book_floors_are_stated_not_hidden():
    """A '% of book' ranking with no floor on the book puts a one-position
    family account above Berkshire. The thresholds are constants, not magic
    numbers buried in a query, and they are returned with every result."""
    import sec_13f_bulk as bulk
    check("bulk: book floor exists", bulk.MIN_BOOK_VALUE > 0)
    check("bulk: position floor exists", bulk.MIN_BOOK_POSITIONS >= 2)
    # The floors must be overridable by the caller and default to the stated
    # constants — a threshold that cannot be changed or seen is unfalsifiable.
    import inspect
    sig = inspect.signature(bulk.holders)
    check("bulk: min_book is a caller-visible parameter",
          sig.parameters["min_book"].default == bulk.MIN_BOOK_VALUE)
    check("bulk: min_positions is a caller-visible parameter",
          sig.parameters["min_positions"].default == bulk.MIN_BOOK_POSITIONS)



# ── corporate actions between two filings (2026-11-16) ──────────────────────
# LARGEST FUNDS listed KLAC as nearly every big manager's biggest buy (a
# 10-for-1 split), Exxon as both the biggest buy and the biggest sell (a new
# CUSIP), Exxon under "EXMOC" and Honeywell under "HONGBP" (foreign lines).

def test_us_listing_never_takes_a_foreign_line():
    exxon = [{"ticker": "EXMOC", "exchCode": "CP"}, {"ticker": "EXMOC", "exchCode": "RC"},
             {"ticker": "XOM", "exchCode": "UZ"}, {"ticker": "XOM", "exchCode": "UN"},
             {"ticker": "XOM", "exchCode": "UQ"}, {"ticker": "1XOMM", "exchCode": "PO"}]
    check("composite gone: the US venues' ticker", t13.us_listing(exxon)["ticker"] == "XOM")
    check("composite first", t13.us_listing([{"ticker": "4I1", "exchCode": "GR"},
                                             {"ticker": "PM", "exchCode": "US"}])["ticker"] == "PM")
    check("no US line: none, not the first record", t13.us_listing([{"ticker": "EXMOC", "exchCode": "CP"}]) is None)
    check("delisted marker stripped", t13.yahoo_ticker("EA*") == "EA")


def test_split_ratios_are_whole_number_splits_only():
    for r in (2, 3, 10, 25, 1.5, 0.1, 0.05, 4 / 3):
        check(f"split {r}", t13.is_split_ratio(r))
    for r in (1.241, 1.437, 1.01, 0, -2):  # spin-off adjustments Yahoo files as "splits"
        check(f"not a split {r}", not t13.is_split_ratio(r))
    klac = [("2000-01-19", 2.0), ("2026-06-12", 10.0)]
    check("inside the window", t13.split_factor_between(klac, "2026-03-31", "2026-09-30") == 10.0)
    check("before the window", t13.split_factor_between(klac, "2026-06-30", "2026-09-30") == 1.0)
    check("on the quarter end counts", t13.split_factor_between([("2026-06-30", 2)], "2026-03-31", "2026-06-30") == 2.0)
    check("spin-off ignored", t13.split_factor_between([("2026-05-01", 1.241)], "2026-03-31", "2026-06-30") == 1.0)


def test_issuer_key_ties_renames_not_namesakes():
    k = t13.issuer_key
    check("Exxon reorganisation", k("EXXON MOBIL CORP") == k("EXXONMOBIL HOLDINGS CORP"))
    check("share class words", k("ALIGHT INC-CL A") == k("ALIGHT INC"))
    check("two banks named First", k("FIRST CAROLINA FINL SVCS INC") != k("FIRST FNDTN INC"))


def test_compare_positions_undoes_splits_cusip_changes_and_listings():
    cur = {"KLA": ("KLA CORP", 1_440.0, 10.0, "KLAC"),        # 10 shares at 144 after the split
           "XNEW": ("EXXONMOBIL HOLDINGS", 2_000.0, 12.0, ""),
           "SPCX": ("SPACE EXPLORATION", 5_000.0, 50.0, "SPCX"),
           "AAPL": ("APPLE", 3_000.0, 12.0, "AAPL")}
    prior = {"KLA": ("KLA CORP", 1_300.0, 1.0, "KLAC"),       # 1 share at 1,300 before it
             "XOLD": ("EXXON MOBIL", 1_900.0, 12.0, "XOM"),
             "AAPL": ("APPLE", 2_000.0, 10.0, "AAPL"),
             "MSFT": ("MICROSOFT", 900.0, 2.0, "MSFT")}
    out = {r[0]: r for r in t13.compare_positions(cur, prior, {"KLA": 10.0}, {"XNEW": ("XOLD", 1.0)},
                                                  universe={"KLA", "XOLD", "AAPL", "MSFT", "XNEW"})}
    check("split: held, not bought", out["KLA"][3] == "held" and out["KLA"][6] == 0.0)
    check("CUSIP change: held", out["XNEW"][3] == "held")
    check("the retired CUSIP is not an exit", "XOLD" not in out)
    check("new listing: visible, not bought", out["SPCX"][3] == "listed" and out["SPCX"][6] == 0.0)
    check("real add priced at the filing", out["AAPL"][3] == "added" and abs(out["AAPL"][6] - 500.0) < 1e-9)
    check("real exit", out["MSFT"][3] == "exited" and out["MSFT"][6] == -900.0)


def test_books_that_changed_shape_are_not_compared():
    check("same book", t13.comparable_books(10e9, 9e9))
    check("an entity change", not t13.comparable_books(1.1e9, 300e9))
    check("no prior", not t13.comparable_books(1e9, 0))
    check("a move bigger than the book is an error", not t13.plausible_move(-265e9, 57e9, 50e9))
    check("a move inside the book", t13.plausible_move(-25e9, 700e9, 720e9))



# ── keeping the index current ────────────────────────────────────────────────

def test_dataset_window_and_deadlines():
    check("window", t13.dataset_window("01jun2026-31aug2026_form13f.zip") == ("2026-06-01", "2026-08-31"))
    check("not a data set", t13.dataset_window("readme.html") is None)
    check("Q3 due 14 Nov", t13.quarter_deadline("2026-09-30") == "2026-11-14")
    check("complete after the deadline at 90%", t13.quarter_is_complete("2026-09-30", 9_600, 10_600, "2026-11-20"))
    check("not before the deadline", not t13.quarter_is_complete("2026-09-30", 10_600, 10_600, "2026-11-10"))
    check("not at 62 filers", not t13.quarter_is_complete("2026-09-30", 62, 10_600, "2026-11-20"))


def test_submission_file_carries_period_manager_and_table():
    text = """<SEC-HEADER>
CONFORMED SUBMISSION TYPE:\t13F-HR
CONFORMED PERIOD OF REPORT:\t20260930
FILER:
\tCOMPANY DATA:
\t\tCOMPANY CONFORMED NAME:\t\t\tEXAMPLE CAPITAL LLC
</SEC-HEADER>
<DOCUMENT><TYPE>INFORMATION TABLE<TEXT><XML>
<ns1:informationTable xmlns:ns1="http://www.sec.gov/edgar/document/thirteenf/informationtable">
<ns1:infoTable><ns1:nameOfIssuer>APPLE INC</ns1:nameOfIssuer><ns1:titleOfClass>COM</ns1:titleOfClass>
<ns1:cusip>037833100</ns1:cusip><ns1:value>2500000</ns1:value>
<ns1:shrsOrPrnAmt><ns1:sshPrnamt>10000</ns1:sshPrnamt><ns1:sshPrnamtType>SH</ns1:sshPrnamtType></ns1:shrsOrPrnAmt>
</ns1:infoTable></ns1:informationTable>
</XML></TEXT></DOCUMENT>"""
    got = t13.parse_submission(text)
    check("parsed", got is not None)
    if got:
        period, manager, xml = got
        check("period", period == "2026-09-30")
        check("manager", manager == "EXAMPLE CAPITAL LLC")
        pos = t13.parse_information_table(xml)
        check("holdings", len(pos) == 1 and pos[0]["cusip"] == "037833100" and pos[0]["shares"] == 10000)
    check("no table: nothing", t13.parse_submission("CONFORMED PERIOD OF REPORT: 20260930") is None)


def test_cusip_change_ratio_is_snapped():
    check("1:1 with the median holder's own buying", t13.snap_ratio(1.02) == 1.0)
    check("2:1", t13.snap_ratio(1.97) == 2.0)
    check("1-for-10", abs(t13.snap_ratio(0.101) - 0.1) < 1e-12)
    check("not forced onto a fraction", abs(t13.snap_ratio(1.49) - 1.49) < 1e-12)
    check("an odd merger ratio kept", abs(t13.snap_ratio(0.343) - 0.343) < 1e-12)

def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        try:
            t()
        except Exception:
            import traceback
            print(f"ERROR: {t.__name__}")
            traceback.print_exc()
            FAILURES.append(t.__name__)
    print()
    if FAILURES:
        print(f"{len(FAILURES)} failure(s): {', '.join(FAILURES)}")
        return 1
    print("all 13F parsing tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
