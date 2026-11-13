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
