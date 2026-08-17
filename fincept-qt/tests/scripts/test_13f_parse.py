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

import sec_13f_data as t13  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


# Namespaced exactly as EDGAR serves it, with Ally split across three rows the
# way Berkshire actually files it, one bond line, and two Alphabet classes.
TABLE = b"""<?xml version="1.0"?>
<informationTable xmlns="http://www.sec.gov/edgar/document/thirteenf/informationtable">
  <infoTable>
    <nameOfIssuer>ALLY FINL INC</nameOfIssuer><titleOfClass>COM</titleOfClass>
    <cusip>02005N100</cusip><value>600</value>
    <shrsOrPrnAmt><sshPrnamt>60</sshPrnamt><sshPrnamtType>SH</sshPrnamtType></shrsOrPrnAmt>
  </infoTable>
  <infoTable>
    <nameOfIssuer>ALLY FINL INC</nameOfIssuer><titleOfClass>COM</titleOfClass>
    <cusip>02005N100</cusip><value>300</value>
    <shrsOrPrnAmt><sshPrnamt>30</sshPrnamt><sshPrnamtType>SH</sshPrnamtType></shrsOrPrnAmt>
  </infoTable>
  <infoTable>
    <nameOfIssuer>ALLY FINL INC</nameOfIssuer><titleOfClass>COM</titleOfClass>
    <cusip>02005N100</cusip><value>100</value>
    <shrsOrPrnAmt><sshPrnamt>10</sshPrnamt><sshPrnamtType>SH</sshPrnamtType></shrsOrPrnAmt>
  </infoTable>
  <infoTable>
    <nameOfIssuer>ALPHABET INC</nameOfIssuer><titleOfClass>CAP STK CL A</titleOfClass>
    <cusip>02079K305</cusip><value>500</value>
    <shrsOrPrnAmt><sshPrnamt>25</sshPrnamt><sshPrnamtType>SH</sshPrnamtType></shrsOrPrnAmt>
  </infoTable>
  <infoTable>
    <nameOfIssuer>ALPHABET INC</nameOfIssuer><titleOfClass>CAP STK CL C</titleOfClass>
    <cusip>02079K107</cusip><value>400</value>
    <shrsOrPrnAmt><sshPrnamt>20</sshPrnamt><sshPrnamtType>SH</sshPrnamtType></shrsOrPrnAmt>
  </infoTable>
  <infoTable>
    <nameOfIssuer>ALLY FINL INC</nameOfIssuer><titleOfClass>COM</titleOfClass>
    <cusip>02005N100</cusip><value>9999</value>
    <shrsOrPrnAmt><sshPrnamt>777</sshPrnamt><sshPrnamtType>SH</sshPrnamtType></shrsOrPrnAmt>
    <putCall>Put</putCall>
  </infoTable>
  <infoTable>
    <nameOfIssuer>SOME BOND ISSUER</nameOfIssuer><titleOfClass>NOTE 5.00 2030</titleOfClass>
    <cusip>99999X999</cusip><value>1000</value>
    <shrsOrPrnAmt><sshPrnamt>1000000</sshPrnamt><sshPrnamtType>PRN</sshPrnamtType></shrsOrPrnAmt>
  </infoTable>
</informationTable>
"""


def by_cusip(rows):
    """Stock lines only — options share the CUSIP of their underlying."""
    return {r["cusip"]: r for r in rows if not r["is_derivative"]}


def test_namespaced_xml_is_parsed_at_all():
    rows = t13.parse_information_table(TABLE)
    check("namespace: rows found", len(rows) > 0, "a namespace mismatch returns an empty book")


def test_repeated_cusip_rows_are_aggregated():
    rows = by_cusip(t13.parse_information_table(TABLE))
    ally = rows.get("02005N100")
    check("aggregate: Ally present", ally is not None)
    if ally:
        check("aggregate: shares summed across all three rows",
              ally["shares"] == 100.0, str(ally["shares"]))
        check("aggregate: value summed across all three rows",
              ally["value"] == 1000.0, str(ally["value"]))


def test_share_classes_stay_separate():
    rows = by_cusip(t13.parse_information_table(TABLE))
    check("classes: Alphabet A and C are distinct CUSIPs",
          "02079K305" in rows and "02079K107" in rows, str(sorted(rows.keys())))
    check("classes: titles preserved",
          rows["02079K305"]["class"] == "CAP STK CL A", rows["02079K305"]["class"])


def test_principal_amounts_are_excluded():
    rows = by_cusip(t13.parse_information_table(TABLE))
    check("bonds: PRN row dropped — a principal amount cannot be added to a share count",
          "99999X999" not in rows, str(sorted(rows.keys())))


def test_options_are_not_counted_as_stock():
    """13F reports options in the SAME table, distinguished only by putCall.

    A put is a BEARISH position. Folding it into the share count reports a
    manager as long a name they may be short — the SEC data set for one quarter
    carries a 1,000,000-share PUT on Moderna, which a putCall-blind parser
    presents as a long holding.
    """
    rows = t13.parse_information_table(TABLE)
    ally_stock = [r for r in rows if r["cusip"] == "02005N100" and not r["is_derivative"]]
    ally_puts = [r for r in rows if r["cusip"] == "02005N100" and r["put_call"] == "PUT"]
    check("options: the put is kept as its own row", len(ally_puts) == 1, str(len(ally_puts)))
    check("options: the put is flagged derivative", ally_puts and ally_puts[0]["is_derivative"])
    check("options: exactly one stock row for the issuer",
          len(ally_stock) == 1, str(len(ally_stock)))
    check("options: put shares did NOT inflate the stock position",
          ally_stock and ally_stock[0]["shares"] == 100.0,
          str(ally_stock[0]["shares"]) if ally_stock else "none")
    check("options: put value did NOT inflate the stock position",
          ally_stock and ally_stock[0]["value"] == 1000.0,
          str(ally_stock[0]["value"]) if ally_stock else "none")


def test_value_units_follow_the_implied_price_not_just_the_date():
    # 100 shares for $1000 implies $10/share — whole dollars, plausible.
    modern = [{"value": 1000.0, "shares": 100.0}]
    out, basis = t13._normalise_values(list(modern), "2026-06-30")
    check("units: modern filing left alone", out[0]["value"] == 1000.0, str(out[0]["value"]))
    check("units: basis reported", "whole dollars" in basis, basis)

    # 100 shares for $1 implies $0.01/share — the values are in thousands.
    legacy = [{"value": 1.0, "shares": 100.0}]
    out2, basis2 = t13._normalise_values(list(legacy), "2019-06-30")
    check("units: thousands scaled to dollars", out2[0]["value"] == 1000.0, str(out2[0]["value"]))
    check("units: scaling disclosed", "thousands" in basis2, basis2)

    # A 2026 filing whose implied price says thousands: the empirical check wins
    # over the date rule, and the disagreement is stated rather than hidden.
    conflict = [{"value": 1.0, "shares": 100.0}]
    out3, basis3 = t13._normalise_values(list(conflict), "2026-06-30")
    check("units: implied price overrides the date rule", out3[0]["value"] == 1000.0)
    check("units: disagreement surfaced", "disagree" in basis3, basis3)


def test_diff_classifies_every_kind_of_move():
    cur = {"period": "2026-06-30", "positions": [
        {"cusip": "A", "issuer": "KEPT CO", "class": "COM", "shares": 100.0, "weight": 0.5},
        {"cusip": "B", "issuer": "ADDED CO", "class": "COM", "shares": 200.0, "weight": 0.3},
        {"cusip": "C", "issuer": "TRIMMED CO", "class": "COM", "shares": 50.0, "weight": 0.1},
        {"cusip": "D", "issuer": "NEW CO", "class": "COM", "shares": 10.0, "weight": 0.1},
    ]}
    prior = {"period": "2026-03-31", "positions": [
        {"cusip": "A", "issuer": "KEPT CO", "class": "COM", "shares": 100.0},
        {"cusip": "B", "issuer": "ADDED CO", "class": "COM", "shares": 100.0},
        {"cusip": "C", "issuer": "TRIMMED CO", "class": "COM", "shares": 200.0},
        {"cusip": "E", "issuer": "EXITED CO", "class": "COM", "shares": 500.0},
    ]}
    moves = {m["cusip"]: m for m in t13.diff_books(cur, prior)["moves"]}
    check("diff: unchanged position is not a move", "A" not in moves, str(list(moves)))
    check("diff: added", moves.get("B", {}).get("action") == "added")
    check("diff: added delta", moves.get("B", {}).get("shares_delta") == 100.0)
    check("diff: trimmed", moves.get("C", {}).get("action") == "trimmed")
    check("diff: trimmed delta is negative", moves.get("C", {}).get("shares_delta") == -150.0)
    check("diff: new", moves.get("D", {}).get("action") == "new")
    check("diff: exited", moves.get("E", {}).get("action") == "exited")
    check("diff: exited leaves zero shares", moves.get("E", {}).get("shares") == 0.0)


def test_diff_ignores_noise_but_labels_share_classes():
    cur = {"period": "q2", "positions": [
        {"cusip": "A", "issuer": "TINY MOVE", "class": "COM", "shares": 1005.0},
        {"cusip": "X", "issuer": "ALPHABET INC", "class": "CAP STK CL A", "shares": 200.0},
        {"cusip": "Y", "issuer": "ALPHABET INC", "class": "CAP STK CL C", "shares": 100.0},
    ]}
    prior = {"period": "q1", "positions": [
        {"cusip": "A", "issuer": "TINY MOVE", "class": "COM", "shares": 1000.0},
        {"cusip": "X", "issuer": "ALPHABET INC", "class": "CAP STK CL A", "shares": 100.0},
        {"cusip": "Y", "issuer": "ALPHABET INC", "class": "CAP STK CL C", "shares": 50.0},
    ]}
    moves = {m["cusip"]: m for m in t13.diff_books(cur, prior)["moves"]}
    check("diff: a 0.5% drift is not a decision", "A" not in moves, str(list(moves)))
    check("diff: both Alphabet classes move independently",
          "X" in moves and "Y" in moves, str(list(moves)))
    check("diff: class A labelled distinctly",
          moves["X"]["label"] == "ALPHABET INC CAP STK CL A", moves["X"]["label"])
    check("diff: class C labelled distinctly",
          moves["Y"]["label"] == "ALPHABET INC CAP STK CL C", moves["Y"]["label"])
    check("diff: plain COM is not suffixed",
          t13.diff_books({"positions": [{"cusip": "Z", "issuer": "PLAIN CO", "class": "COM",
                                         "shares": 10.0}]},
                         {"positions": []})["moves"][0]["label"] == "PLAIN CO")


def test_issuer_normalisation_survives_corporate_suffixes():
    n = t13._norm_issuer
    check("norm: Inc dropped", n("APPLE INC") == "APPLE", n("APPLE INC"))
    check("norm: punctuation dropped", n("Apple Inc.") == "APPLE", n("Apple Inc."))
    check("norm: Corporation dropped", n("CHEVRON CORPORATION") == "CHEVRON")
    check("norm: multi-word core kept",
          n("BANK OF AMER CORP") == "BANK OF AMER", n("BANK OF AMER CORP"))
    check("norm: empty is empty", n("") == "")
    check("norm: distinct companies stay distinct", n("MICROSOFT CORP") != n("APPLE INC"))


def test_issuer_matching_refuses_a_plausible_wrong_company():
    """The worst failure mode this file guards.

    "Apple Inc." normalises to APPLE, and APPLE is a substring of
    APPLE HOSPITALITY REIT — so a contains() test reports a REIT position as an
    AAPL position, with a real share count and a real weight, presented as fact.
    A missing row is visibly missing; a wrong row is not.
    """
    m, n = t13.issuer_matches, t13._norm_issuer
    apple = n("Apple Inc.")
    check("match: the real issuer", m(apple, n("APPLE INC")))
    check("match: REIT with a colliding prefix refused",
          not m(apple, n("APPLE HOSPITALITY REIT INC")))
    check("match: bare colliding prefix refused", not m(apple, n("APPLE HOSPITALITY")))

    # Prefix matching still has to admit EDGAR's abbreviations of long names.
    check("match: BANK OF AMER abbreviation accepted",
          m(n("Bank of America Corporation"), n("BANK OF AMER CORP")))
    check("match: short exact names still match",
          m(n("Chevron Corporation"), n("CHEVRON CORPORATION")))

    # Genuinely different companies must never match.
    check("match: distinct companies refused", not m(n("Microsoft Corp"), n("APPLE INC")))
    check("match: empty target refuses", not m("", n("APPLE INC")))
    check("match: empty candidate refuses", not m(apple, ""))


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


def test_namespace_stripping_helper():
    check("local: namespaced tag",
          t13._local("{http://www.sec.gov/edgar/x}infoTable") == "infoTable")
    check("local: bare tag untouched", t13._local("infoTable") == "infoTable")


def test_malformed_input_yields_nothing_rather_than_raising():
    check("robust: garbage", t13.parse_information_table(b"<not xml") == [])
    check("robust: empty table", t13.parse_information_table(
        b'<informationTable xmlns="x"/>') == [])
    check("robust: empty book normalises", t13._normalise_values([], "2026-06-30")[0] == [])


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
