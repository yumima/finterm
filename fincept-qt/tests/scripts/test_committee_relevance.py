#!/usr/bin/env python3
"""Committee-oversight matching for Power Trader.

`committee_relevance` is the single largest input to every conflict/insider
number on the screen (30 of the signal score's reachable 55, and the whole
cmte_overlap term of the insider composite). It was wrong in both directions.

FALSE NEGATIVES — the reverse map kept only the FIRST committee per sector,
and dict declaration order picked the winner: Technology resolved to
"Intelligence" (declared before Commerce), Healthcare to "Veterans Affairs"
(before Health), Financials to "Finance" (before Banking). So the three most
obvious conflicts on the screen — a Commerce member trading NVDA, a HELP
member trading PFE, a Banking member trading JPM — all scored as no overlap.

FALSE POSITIVES — matching was a bidirectional 8-character prefix substring
test, so "Energy and Natural Resources" matched "Energy and Commerce"
("energy a" is a substring of both): a senator's energy seat flagged trades
overseen by a different House committee.

These tests pin both directions. The map is curated data, so they assert on
the RELATION (does this member's seat cover this ticker) rather than on the
map's internal contents.
"""

import os
import sys
import types

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

# The module imports network/scraping libs at import time; a CI interpreter
# may not have them and these tests touch none of that code.
for _name in ("requests", "bs4", "yfinance", "pandas", "curl_cffi"):
    if _name not in sys.modules:
        try:
            __import__(_name)
        except ImportError:
            sys.modules[_name] = types.ModuleType(_name)

import senate_disclosures_data as sd  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


def rel(ticker, committees):
    return sd._compute_committee_relevance(ticker, committees)


def test_the_three_conflicts_that_used_to_be_missed():
    # Each of these returned "" before the map became many-to-many.
    cases = [
        ("NVDA", "Commerce, Science, and Transportation"),
        ("AAPL", "Commerce, Science, and Transportation"),
        ("PFE",  "Health, Education, Labor, and Pensions"),
        ("JNJ",  "Health, Education, Labor, and Pensions"),
        ("JPM",  "Banking, Housing, and Urban Affairs"),
        ("GS",   "Banking, Housing, and Urban Affairs"),
    ]
    for ticker, cmte in cases:
        got = rel(ticker, [cmte])
        check(f"{ticker} flags for a member of {cmte[:24]}…",
              got == cmte, f"got {got!r}")


def test_committee_returns_the_members_own_spelling():
    # The UI shows this string, so it must be the member's committee name,
    # not our internal map key.
    got = rel("NVDA", ["Commerce, Science, and Transportation"])
    check("returns the member's spelling, not the map key",
          got == "Commerce, Science, and Transportation", f"got {got!r}")


def test_energy_committees_are_not_confused():
    # THE false positive: "energy a" is a substring of both names.
    check("Senate Energy and House Energy&Commerce are different committees",
          not sd._committees_match("Energy and Natural Resources", "Energy and Commerce"))
    # XOM is an Energy-sector name overseen by both, but via the map, not via
    # a name collision — so assert the matcher itself, above, stays strict.


def test_unrelated_seat_does_not_flag():
    for ticker, cmte in [("LMT", "Agriculture, Nutrition, and Forestry"),
                         ("JPM", "Armed Services"),
                         ("XOM", "Judiciary")]:
        got = rel(ticker, [cmte])
        check(f"{ticker} does not flag for {cmte[:20]}…", got == "", f"got {got!r}")


def test_amplified_map_errors_stay_fixed():
    # Going many-to-many turns any bad map entry into a live false positive.
    # These three were latent before and are now asserted absent.
    check("Veterans Affairs does not oversee defense contractors",
          rel("LMT", ["Veterans Affairs"]) == "")
    check("Budget has no financial-industry oversight",
          rel("JPM", ["Budget"]) == "")
    check("Education has no consumer-discretionary oversight",
          rel("DIS", ["Education"]) == "")
    # …but Veterans Affairs SHOULD still cover healthcare.
    check("Veterans Affairs still covers healthcare",
          rel("UNH", ["Veterans Affairs"]) == "Veterans Affairs")


def test_short_key_matches_long_official_name():
    check("'Commerce' matches 'Commerce, Science, and Transportation'",
          sd._committees_match("Commerce", "Commerce, Science, and Transportation"))
    check("'Health' matches 'Health, Education, Labor, and Pensions'",
          sd._committees_match("Health", "Health, Education, Labor, and Pensions"))
    check("matching is symmetric",
          sd._committees_match("Commerce, Science, and Transportation", "Commerce"))


def test_filler_words_do_not_block_matches():
    # The case stopword-stripping actually exists for. Chamber prefixes and a
    # trailing "Committee" are decoration, and Congress.gov is inconsistent
    # about both. Without stripping, {senate,armed,services} and
    # {armed,services,committee} are subsets in NEITHER direction and the same
    # committee fails to match itself.
    check("chamber prefix and trailing 'Committee' are ignored",
          sd._committees_match("Senate Armed Services", "Armed Services Committee"))
    check("'Committee on Finance' matches 'Finance'",
          sd._committees_match("Committee on Finance", "Finance"))
    # And the real-data shape: our short key vs a decorated official name.
    check("a decorated official name still resolves a ticker",
          rel("LMT", ["Senate Committee on Armed Services"]) ==
          "Senate Committee on Armed Services")


def test_filler_words_do_not_create_matches():
    # Two committees sharing only stopwords must not match, or every pair
    # containing "and"/"committee" would collide.
    check("stopword-only overlap is not a match",
          not sd._committees_match("Committee on the Judiciary",
                                   "Committee on Agriculture"))
    check("empty/garbage names never match",
          not sd._committees_match("", "Armed Services"))
    check("a name of only stopwords never matches",
          not sd._committees_match("the and of", "Armed Services"))


def test_uncovered_ticker_is_empty_not_a_guess():
    # ~84 tickers are covered. Anything else must return "" rather than
    # picking a plausible committee.
    check("an uncovered ticker returns empty",
          rel("ZZZZ", ["Armed Services"]) == "")


def test_member_on_several_committees_matches_any():
    got = rel("NVDA", ["Agriculture, Nutrition, and Forestry",
                       "Commerce, Science, and Transportation"])
    check("a member with several seats matches on the relevant one",
          got == "Commerce, Science, and Transportation", f"got {got!r}")


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
    print("all committee-relevance tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
