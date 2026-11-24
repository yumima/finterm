#!/usr/bin/env python3
"""A model may quote a figure, never originate one — the agent runtime's copy.

scripts/agents/finagent_core/number_grounding.py is the Python twin of
src/ai_chat/NumberGrounding.h. The two guard different paths to the same
screen, so they must give the same verdict on the same text: these are the
cases of tests/ai_chat/test_number_grounding.cpp, with the same expectations.
A case added to one belongs in the other.
"""

import json
import os
import sys
import traceback
import types

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts", "agents", "finagent_core"))
import number_grounding as ng  # noqa: E402

YEAR = 2026


def run(evidence, reply):
    ev = ng.Evidence(YEAR)
    ev.add(evidence)
    return ng.enforce(reply, ev)


def test_quoted_as_given():
    r = run("price 150.25, change_pct -3.2, volume 1234567",
            "It closed at $150.25, down 3.2%, on volume of 1,234,567.")
    assert r.removed == [] and r.checked == 3


def test_rounded_to_stated_digits():
    r = run("marketCap 1234567890 price 150.37 margin 0.1534",
            "Market cap is $1.2B (1,234.6M), the price about $150 and the margin 15.3%.")
    assert r.removed == []


def test_scale_words_and_filing_units():
    assert run("Revenue (in millions): 4,512", "Revenue was $4.5 billion.").removed == []


def test_rounding_is_not_licence():
    r = run("growth 19.2", "Growth was nearly 20%.")
    assert r.removed == ["20%"] and ng.MARKER in r.text and "20" not in r.text


def test_invented_figures_removed():
    r = run("price 150.25", "It trades at $150.25; my target is $185.00, about 23% upside, "
                            "with revenue of $4.2 billion expected.")
    assert r.removed == ["$185.00", "23%", "$4.2 billion"]
    assert "$150.25" in r.text and "185" not in r.text and "4.2" not in r.text


def test_no_evidence_means_no_figures():
    assert len(run("", "The stock usually returns 12.5% a year and trades near $48.").removed) == 2


def test_future_year_is_a_claim():
    r = run("founded 1998", "Founded in 1998, it survived 2008 and expects profit by 2029.")
    assert r.removed == ["2029"]


def test_derivation_with_operands_passes():
    assert run("open 120 close 150", "It rose from $120 to $150, a 25% gain of $30.").removed == []


def test_wrong_arithmetic_is_removed():
    assert run("open 120 close 150", "It rose from $120 to $150, a 40% gain.").removed == ["40%"]


def test_derivation_needs_operands_in_the_sentence():
    assert run("open 120 close 150", "Momentum is strong.\n\nThe gain was 25%.").removed == ["25%"]
    assert run("open 120 close 150", "It went from $120 to $150. The gain was 30%.").removed == ["30%"]


def test_unverified_operands_derive_nothing():
    r = run("price 150", "2. There are 5 reasons it trades at $150, a 40% gain to $300 since 2019.")
    assert r.removed == ["40%", "$300"]


def test_user_arithmetic():
    assert run("what is 15% of 240?", "15% of 240 is 36.").removed == []


def test_names_identifiers_and_structure():
    r = run("", "1. Read the 10-K and the S-1.\n2. Compare Q3 with the S&P 500 and Russell 2000.\n"
                "12. Check Form 4 and 13F filings under Rule 144.\nThere are 3 reasons, since 2019.")
    assert r.removed == [] and r.checked == 0


def test_code_is_left_alone():
    assert run("", "Use `df.rolling(20).mean()`:\n```python\nx = 42 * 1.5\n```\nDone.").removed == []


def test_range_shares_its_unit():
    assert len(run("", "Expect a move of 5-10% next week.").removed) == 2


def test_small_integer_with_a_unit_is_checked():
    assert len(run("", "Yield is 4% and it costs $7.").removed) == 2


def test_lowercase_and_unknown_units_are_checked():
    r = run("", "Revenue of $900m, EBITDA $2.5bn, a 150USD price, 25bp wider and up 30pts.")
    assert len(r.removed) == 5
    assert run("revenue 900000000", "Revenue of $900m.").removed == []


def test_backticks_do_not_launder_a_figure():
    r = run("", "Target `$250`, EPS `3.41`.\n```\n| Year | Sales |\n| A | 9,400 |\n```\n")
    assert len(r.removed) == 3


def test_an_index_name_is_not_an_index_level():
    r = run("", "The S&P 500 reaches S&P 7,500 and Dow 52,000; support level 182.50, window 30.")
    assert r.removed == ["7,500", "52,000", "182.50", "30"]


def test_a_date_does_not_ground_an_amount():
    ev = "Today is 2026-11-15. Bars: 2026-11-13T16:00 close 142.37"
    assert run(ev, "Up 15% to $11M by 16:00 on 2026-11-13.").removed == ["15%", "$11M"]
    assert run(ev, "As of 2026-11-15 it closed at 142.37.").removed == []


def test_a_stray_digit_does_not_ground_a_scaled_amount():
    assert run("3 directors bought", "Revenue was $3 billion.").removed == ["$3 billion"]


def test_an_id_that_reads_as_a_huge_exponent_is_not_a_figure():
    # Transaction ids are hex: "6795e478" and "0e931" once overflowed here and
    # failed the whole portfolio analysis.
    ev = "ids 6795e478-11aa, a0e931ff, 1e400, 9e300T, " + "9" * 400 + "; close 142.37"
    assert [n.value for n in ng.extract("6795e478 0e931 1e400 9e300T " + "9" * 400, YEAR, lenient=True)] == []
    assert run(ev, "Closed at $142.37, target $185.00.").removed == ["$185.00"]


def test_the_directive_carries_no_figures():
    assert ng.extract(ng.DIRECTIVE, YEAR, lenient=True) == []


def test_json_array_stays_parseable():
    r = run("", '["Compute the 50-day and 200-day averages", "Compare them"]')
    assert r.json and isinstance(json.loads(r.text), list) and len(r.removed) == 2


def test_json_numbers_become_null():
    r = run("price 150.25",
            '{"direction":"up","target_price":185.5,"last":150.25,"why":"From $150.25 to $185.50."}')
    o = json.loads(r.text)
    assert o["target_price"] is None and o["last"] == 150.25 and o["direction"] == "up"
    assert ng.MARKER in o["why"] and len(r.removed) == 2


def test_clean_json_is_untouched():
    j = '{ "b": 150.25,  "a": "closed at 150.25" }'
    assert run("150.25", j).text == j


def test_evidence_from_a_run_output():
    # What an agent run can show for itself: the messages it was given and its
    # tool results count; its OWN answer text must not ground itself.
    msg = lambda role, content: types.SimpleNamespace(role=role, content=content)  # noqa: E731
    tool = types.SimpleNamespace(result='{"price": 150.25}', tool_args={"symbol": "ACME"})
    out = types.SimpleNamespace(
        content="It trades at $150.25 and will reach $185.00.",
        messages=[msg("system", "You are an analyst."), msg("user", "How is ACME?"),
                  msg("assistant", "It trades at $150.25 and will reach $185.00.")],
        tools=[tool],
    )
    text = ng.ground(out.content, out)
    assert "$150.25" in text and "185" not in text and ng.MARKER in text
    assert "1 figure was removed" in text


def main():
    failures = []
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except Exception:
                traceback.print_exc()
                failures.append(name)
    if failures:
        print(f"{len(failures)} failure(s): {', '.join(failures)}")
        return 1
    print("all number grounding tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
