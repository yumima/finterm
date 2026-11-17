#!/usr/bin/env python3
"""Market-wide 13D / 13G: index parsing, header parties, structured fields.

Runs the SHIPPED functions from sec_stakes_market — no network. Each check
pins a trap met against real filings (2026-11-16):

  * EDGAR spells the schedules two ways ("SCHEDULE 13D/A", "SC 13D/A") and
    lists each filing under both its subject and its filer.
  * A group filing repeats the reporting-person block once per member; the
    group's stake is the largest, and its share count goes with it.
  * The event date is MM/DD/YYYY in the filing.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

import sec_stakes_market as s  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    print(("PASS : " if cond else "FAIL : ") + name + ("" if cond else f" {detail}"))
    if not cond:
        FAILURES.append(name)


INDEX = """Form Type   Company Name                                                  CIK         Date Filed  File Name
SCHEDULE 13D     Coursera, Inc.                                                1651562     20261113    edgar/data/1651562/0001072613-26-000890.txt
SCHEDULE 13D     NEW ENTERPRISE ASSOCIATES 13 LP                               1452907     20261113    edgar/data/1452907/0001072613-26-000890.txt
SC 13G/A         OLD STYLE CO                                                  1111111     20261113    edgar/data/1111111/0000000000-26-000001.txt
4                SOMEONE                                                       2222222     20261113    edgar/data/2222222/0000000000-26-000002.txt
"""

HEADER = """SUBJECT COMPANY:\t
\tCOMPANY DATA:\t
\t\tCOMPANY CONFORMED NAME:\t\t\tCoursera, Inc.
\t\tCENTRAL INDEX KEY:\t\t\t0001651562
FILED BY:\t\t
\tCOMPANY DATA:\t
\t\tCOMPANY CONFORMED NAME:\t\t\tNEW ENTERPRISE ASSOCIATES 13 LP
\t\tCENTRAL INDEX KEY:\t\t\t0001452907
"""

GROUP = """<dateOfEvent>11/05/2026</dateOfEvent>
<reportingPersonInfo><aggregateAmountOwned>12867769.00</aggregateAmountOwned><percentOfClass>5.1</percentOfClass></reportingPersonInfo>
<reportingPersonInfo><aggregateAmountOwned>13000000.00</aggregateAmountOwned><percentOfClass>5.2</percentOfClass></reportingPersonInfo>
"""


def test_index_both_spellings_once_each():
    idx = s.parse_index(INDEX)
    check("one row per filing, though listed twice", list(idx) == ["0001072613-26-000890", "0000000000-26-000001"])
    check("form kept", idx["0001072613-26-000890"][0] == "SCHEDULE 13D")
    check("not a stake form: skipped", "0000000000-26-000002" not in idx)
    check("spellings normalised", s.norm_form("SCHEDULE 13D/A") == s.norm_form("SC 13D/A") == "SC 13D/A")


def test_which_filings_are_read_in_full():
    check("13D", s.is_opened("SCHEDULE 13D"))
    check("13D amendment", s.is_opened("SC 13D/A"))
    check("new 13G", s.is_opened("SCHEDULE 13G"))
    check("13G amendment is listed, not opened", not s.is_opened("SCHEDULE 13G/A"))


def test_header_names_both_sides():
    got = s.parse_header(HEADER)
    check("subject", got[:2] == ("0001651562", "Coursera, Inc."), got)
    check("filer", got[2:] == ("0001452907", "NEW ENTERPRISE ASSOCIATES 13 LP"), got)


def test_structured_group_takes_the_largest_stake():
    pct, shares, event = s.parse_structured(GROUP)
    check("group stake", pct == 5.2)
    check("its share count", shares == 13_000_000.0)
    check("event date", event == "2026-11-05")
    check("prose: nothing invented", s.parse_structured("Item 5. The Reporting Persons own 6.1%.") == (None, None, ""))


def main():
    for k, t in sorted(globals().items()):
        if k.startswith("test_"):
            try:
                t()
            except Exception:
                import traceback
                traceback.print_exc()
                FAILURES.append(k)
    print()
    if FAILURES:
        print(f"{len(FAILURES)} failure(s): {', '.join(FAILURES)}")
        return 1
    print("all stakes tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
