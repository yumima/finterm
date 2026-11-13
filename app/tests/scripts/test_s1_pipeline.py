#!/usr/bin/env python3
"""IPO pipeline grouping: withdrawal detection and the readiness clock.

Two bugs motivate this file.

THE QUERY. EDGAR full-text search treats any `forms` entry containing "/A" as
a file_type refinement and then eliminates every root form with no matching
file_type. The production query listed both roots and amendments, so it
returned ONLY amendments — measured 52 hits, all "/A", versus 96 for the same
window using root forms. Consequences: `first_filed` was really the first
AMENDMENT date, and the RW added for withdrawal detection returned nothing at
all, making the entire withdrawal feature dead on arrival. A root form already
includes its own amendments, so the fix is to list roots only. That is asserted
by test_query_uses_root_forms_only below — it is a string check, not a network
call, because the live behaviour is EDGAR's and can't be pinned in a unit test.

THE SEMANTICS. Withdrawal is not "an RW appears in the history": companies pull
a deal and re-file, an RW filed the same day as a corrected amendment is a live
deal being repaired, an RW must not start the readiness clock, and an RW from a
filer that never registered (fund trusts withdrawing N-1A amendments dominate
the form) is not an IPO at all. Each of those was got wrong on a first attempt,
so each has a test.
"""

import os
import sys
import types

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

for _name in ("requests",):
    if _name not in sys.modules:
        try:
            __import__(_name)
        except ImportError:
            sys.modules[_name] = types.ModuleType(_name)

import sec_s1_pipeline as sp  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


def hit(cik, name, form, filed):
    """One efts-shaped hit. `_id` must be unique or accession dedup drops it."""
    return {
        "_id": f"{cik}-{form}-{filed}",
        "_source": {"ciks": [cik], "display_names": [name],
                    "form": form, "file_date": filed},
    }


def build(raw, curated=None):
    return {e["company_name"]: e
            for e in sp.build_pipeline_entries(raw, curated or set())}


def test_query_uses_root_forms_only():
    import inspect
    src = inspect.getsource(sp.fetch_pipeline) + inspect.getsource(sp.pipeline_newest)
    # Find the forms= values actually sent to EDGAR.
    import re
    sent = re.findall(r'"forms":\s*"([^"]+)"', src)
    check("both efts queries specify a form set", len(sent) == 2, f"{sent}")
    for f in sent:
        check(f"query '{f}' lists no /A amendment forms",
              "/A" not in f,
              "an /A entry silently turns the query into amendments-only")
        check(f"query '{f}' includes RW", "RW" in f)


def test_withdrawn_when_rw_is_the_latest_filing():
    out = build([hit("1", "Alpha", "S-1", "2026-01-10"),
                 hit("1", "Alpha", "S-1/A", "2026-02-01"),
                 hit("1", "Alpha", "RW", "2026-03-01")])
    check("RW after the last amendment marks the deal withdrawn",
          out["Alpha"]["withdrawn"] is True)
    check("withdrawn_date is recorded",
          out["Alpha"]["withdrawn_date"] == "2026-03-01")


def test_refiling_after_a_withdrawal_is_live_again():
    out = build([hit("2", "Beta", "S-1", "2026-01-10"),
                 hit("2", "Beta", "RW", "2026-03-01"),
                 hit("2", "Beta", "S-1", "2026-07-01")])
    check("a company that re-filed after withdrawing is NOT withdrawn",
          out["Beta"]["withdrawn"] is False,
          "withdrawn must mean 'the latest event was a withdrawal'")


def test_same_day_tie_goes_to_the_active_filing():
    # Withdraw a defective pre-effective amendment and refile it the same day.
    # A >= comparison called this a pulled deal and zeroed its readiness.
    out = build([hit("3", "Gamma", "S-1", "2026-01-10"),
                 hit("3", "Gamma", "RW", "2026-04-01"),
                 hit("3", "Gamma", "S-1/A", "2026-04-01")])
    check("a same-day withdrawal + refiling is still live",
          out["Gamma"]["withdrawn"] is False)


def test_non_registrant_withdrawals_are_excluded():
    # RW withdraws ANY Securities Act registration. Fund trusts dominate the
    # form; without a gate each became a "company" in the IPO pipeline.
    out = build([hit("4", "Tidal Trust IV", "RW", "2026-03-01"),
                 hit("5", "Real Co", "S-1", "2026-01-01")])
    check("an RW-only filer never becomes a pipeline company",
          "Tidal Trust IV" not in out, f"{list(out)}")
    check("a genuine registrant is kept", "Real Co" in out)


def test_withdrawal_does_not_start_the_readiness_clock():
    # RW in March, fresh S-1 in July. Dating from the withdrawal would run
    # days_since_first ahead and decay days_to_price_est toward "due any day".
    out = build([hit("6", "Delta", "RW", "2026-03-01"),
                 hit("6", "Delta", "S-1", "2026-07-01")])
    check("first_filed is the registration, not the withdrawal",
          out["Delta"]["first_filed"] == "2026-07-01",
          f"got {out['Delta']['first_filed']}")


def test_withdrawal_is_not_counted_as_an_amendment():
    out = build([hit("7", "Epsilon", "S-1", "2026-01-10"),
                 hit("7", "Epsilon", "RW", "2026-03-01")])
    check("RW does not increment amendment_count",
          out["Epsilon"]["amendment_count"] == 0,
          f"got {out['Epsilon']['amendment_count']}")


def test_draft_registration_does_not_start_the_clock():
    # Pre-existing behaviour, pinned so the withdrawal changes don't break it:
    # the clock starts at the public S-1, not the confidential draft.
    out = build([hit("8", "Zeta", "DRS", "2026-01-01"),
                 hit("8", "Zeta", "S-1", "2026-05-01")])
    check("first_filed skips the confidential DRS draft",
          out["Zeta"]["first_filed"] == "2026-05-01",
          f"got {out['Zeta']['first_filed']}")


def test_amendments_are_counted_and_dated():
    out = build([hit("9", "Eta", "S-1", "2026-01-10"),
                 hit("9", "Eta", "S-1/A", "2026-02-01"),
                 hit("9", "Eta", "S-1/A", "2026-03-05")])
    check("amendment_count counts public amendments",
          out["Eta"]["amendment_count"] == 2, f"got {out['Eta']['amendment_count']}")
    check("latest_amended is the newest amendment",
          out["Eta"]["latest_amended"] == "2026-03-05")


def test_draft_amendment_is_not_a_public_amendment():
    out = build([hit("10", "Theta", "S-1", "2026-01-10"),
                 hit("10", "Theta", "DRS/A", "2026-02-01")])
    check("a DRS/A is not counted as an S-1 amendment",
          out["Theta"]["amendment_count"] == 0,
          f"got {out['Theta']['amendment_count']}")


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
    print("all s1 pipeline tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
