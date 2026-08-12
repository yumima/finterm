#!/usr/bin/env python3
"""S-1 section slicing: pick the section BODY, not its table-of-contents entry.

`parse_s1_funding` finds each section with a heading regex and slices from the
match to the next section heading. It used `re.search`, which returns the FIRST
occurrence — and in a real S-1 that is the table of contents, where every
heading is listed with a page number. Because the boundary regex also matches
the NEXT ToC line, the "section" came back as a two-line ToC fragment. The UI
then rendered that fragment beneath the words "Verbatim excerpts below are
authoritative".

Two further traps, both hit while fixing the first:

  • Anchoring on ANY occurrence lets a passing mention win. Real text: "…the
    use of proceeds not held in the trust account…" is followed by a long
    paragraph, so a longest-wins rule PREFERRED it over the real section.
    Hence the match must look like a heading: alone on a short line, optionally
    behind an "Item 15." marker.
  • A cross-reference — see "Underwriting" for a description of… — puts the
    heading word alone on a line once the HTML is flattened. Hence the heading
    must be essentially the WHOLE line.

These tests run the SHIPPED slicer (yfinance_data.s1_extract_section) against
synthetic documents with the exact shape of a real filing — no network, and no
mirrored copy that could drift away from the code it claims to cover.
"""

import os
import re
import sys
import types

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

for _name in ("yfinance", "pandas", "numpy", "curl_cffi", "requests"):
    if _name not in sys.modules:
        try:
            __import__(_name)
        except ImportError:
            sys.modules[_name] = types.ModuleType(_name)

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


# The REAL slicer, imported — not a copy. It was originally a closure over the
# fetched document, which made it untestable; it is now module-level in
# yfinance_data precisely so these tests exercise the shipped code path rather
# than a mirror that can drift away from it.
import yfinance_data as yd  # noqa: E402

extract_section = yd.s1_extract_section


BODY = (" The Sponsor purchased an aggregate of 7,666,667 Class B ordinary shares "
        "for an aggregate purchase price of twenty five thousand dollars, or "
        "approximately zero point zero zero three dollars per share, in a private "
        "placement exempt from registration under Section 4(a)(2) of the Securities "
        "Act. These shares will automatically convert upon the closing of our "
        "initial business combination on a one for one basis, subject to adjustment. ")

DOC = (
    "TABLE OF CONTENTS\n"
    "Prospectus Summary 1\n"
    "Risk Factors 14\n"
    "Use of Proceeds 62\n"
    "Principal Shareholders 88\n"
    "Recent Sales of Unregistered Securities 104\n"
    "Underwriting 110\n"
    "\n" + ("Filler prospectus narrative paragraph. " * 60) + "\n"
    "USE OF PROCEEDS\n" + (BODY * 2) + "\n"
    "PRINCIPAL SHAREHOLDERS\n" + (BODY * 2) + "\n"
    "Item 15. Recent Sales of Unregistered Securities.\n" + (BODY * 3) + "\n"
    "Item 16. Exhibits\n"
)


def test_body_wins_over_table_of_contents():
    got = extract_section(DOC, r"recent\s+sales\s+of\s+unregistered\s+securities")
    check("slices the section body, not the ToC entry",
          "Sponsor purchased" in got, f"got {got[:70]!r}")
    check("the ToC page number is not in the result",
          "Recent Sales of Unregistered Securities 104" not in got)
    check("the body is substantial, not a two-line fragment",
          len(got) > 800, f"len={len(got)}")


def test_item_prefixed_heading_is_accepted():
    # Part II headings carry an "Item 15." marker; rejecting them would lose
    # the single most important section in the filing.
    got = extract_section(DOC, r"recent\s+sales\s+of\s+unregistered\s+securities")
    check("an 'Item 15.' prefix does not disqualify a heading", bool(got))


def test_passing_mention_does_not_anchor_a_section():
    doc = (
        "TABLE OF CONTENTS\nUse of Proceeds 62\nUnderwriting 110\n\n"
        + "We may not be able to control the use of proceeds not held in the trust "
          "account, and our shareholders may not agree with how we allocate them. "
        + ("Long paragraph of prose that follows the passing mention. " * 80) + "\n"
        "USE OF PROCEEDS\n" + (BODY * 2) + "\n"
        "UNDERWRITING\n"
    )
    got = extract_section(doc, r"use\s+of\s+proceeds")
    check("a mid-sentence mention does not become the section",
          got.upper().startswith("USE OF PROCEEDS"), f"got {got[:60]!r}")
    check("the section body is the one returned",
          "Sponsor purchased" in got)


def test_cross_reference_line_is_rejected():
    # HTML flattening puts the quoted heading alone on its own line.
    doc = (
        "TABLE OF CONTENTS\nUnderwriting 110\n\n"
        + 'See the section titled\nUnderwriting\n" for a description of the '
          "compensation payable to the underwriters and other items of value. "
        + ("Cross reference trailing prose. " * 80) + "\n"
        "UNDERWRITING\n" + (BODY * 3) + "\n"
        "Item 15. Recent Sales of Unregistered Securities.\n"
    )
    got = extract_section(doc, r"underwriting")
    check("a cross-reference does not become the section",
          "for a description of the compensation" not in got[:200],
          f"got {got[:80]!r}")
    check("the real underwriting body is returned", "Sponsor purchased" in got)


def test_toc_only_document_returns_nothing():
    # A document with the heading ONLY in its ToC must yield nothing, not a
    # page-number list presented as an authoritative excerpt.
    doc = ("TABLE OF CONTENTS\n"
           "Use of Proceeds 62\n"
           "Principal Shareholders 88\n"
           "Recent Sales of Unregistered Securities 104\n"
           "Underwriting 110\n"
           "Signatures 120\n")
    got = extract_section(doc, r"recent\s+sales\s+of\s+unregistered\s+securities")
    check("a ToC-only document yields no section", got == "", f"got {got[:60]!r}")


def test_shareholders_spelling_is_covered():
    # SPACs and foreign private issuers write "Shareholders"; the pattern used
    # to require "Stockholders" and silently found nothing.
    got = extract_section(DOC, r"principal\s+(?:and\s+selling\s+)?(?:stock|share)holders?")
    check("'Principal Shareholders' is found, not just 'Stockholders'",
          "Sponsor purchased" in got, f"got {got[:60]!r}")


def test_cross_reference_continuation_is_rejected():
    """The shape that beat a 40,000-char RISK FACTORS body on a real filing.

    HTML flattening splits `as described under "Risk Factors." Should one or
    more...` across lines, leaving the heading alone on its own short line —
    indistinguishable from a real heading by the line itself. The giveaway is
    the NEXT line, which opens with the punctuation that continues the
    sentence. Taken from a live 927k-char S-1/A (Amanat Acquisition Corp).
    """
    doc = (
        "TABLE OF CONTENTS\nRisk Factors 14\nUnderwriting 110\n\n"
        + "You should carefully consider the matters described under\n"
          "Risk Factors\n"
          '.\u201d Should one or more of these risks or uncertainties materialize, '
        + ("or should underlying assumptions prove incorrect, actual results may "
           "vary materially. " * 40) + "\n"
        "RISK FACTORS\n" + (BODY * 4) + "\n"
        "UNDERWRITING\n"
    )
    got = extract_section(doc, r"risk\s+factors")
    check("a cross-reference continuation is not the section",
          not got.lstrip().startswith("Risk Factors\n."), f"got {got[:60]!r}")
    check("the real RISK FACTORS body is returned", "Sponsor purchased" in got)


def test_heading_word_inside_a_longer_heading_is_rejected():
    """"Underwriting Agreement" is a financial-statement note, not the
    UNDERWRITING section. On a real filing it sat near the end and won."""
    doc = (
        "TABLE OF CONTENTS\nUnderwriting 110\n\n"
        "UNDERWRITING\n" + (BODY * 2) + "\n"
        "Item 15. Recent Sales of Unregistered Securities.\n" + BODY + "\n"
        "Underwriting Agreement\n"
        + ("The Company will grant the underwriter a 45-day option to purchase "
           "additional Public Shares to cover over-allotments. " * 60) + "\n"
    )
    got = extract_section(doc, r"underwriting")
    check("a longer heading containing the word is not the section",
          not got.startswith("Underwriting Agreement"), f"got {got[:50]!r}")
    check("the real UNDERWRITING body is returned", "Sponsor purchased" in got)


def test_toc_page_number_on_its_own_line():
    """EDGAR renders the contents as a two-column <table>, so get_text() puts
    the page number on its OWN line — not trailing the heading. Checking only
    for a trailing number missed every real filing: across 6 live S-1s the
    UNDERWRITING ToC entry won on length in 6 of 6, because its ToC neighbours
    (LEGAL MATTERS, EXPERTS) are not section boundaries, so its slice ran the
    full 12,000-char cap against a real section of ~2,200.
    """
    doc = (
        "TABLE OF CONTENTS\n"
        "Prospectus Summary\n1\n"
        "Underwriting\n168\n"
        "Legal Matters\n171\n"
        "Experts\n172\n"
        + ("Cover page and summary prose. " * 400) + "\n"
        "UNDERWRITING\n" + (BODY * 2) + "\n"
        "Item 15. Recent Sales of Unregistered Securities.\n"
    )
    got = extract_section(doc, r"underwriting")
    check("a ToC entry with its page number on the next line is rejected",
          "Sponsor purchased" in got, f"got {got[:70]!r}")


def test_item_heading_with_nonbreaking_spaces():
    """"ITEM 15.\xa0\xa0RECENT SALES OF UNREGISTERED SECURITIES." — the single
    most common Item-15 shape. Measuring `trailing` by subtracting lengths from
    the whole line charged it for the two non-breaking spaces plus the
    heading's own terminal period, scoring 3 with NOTHING after the heading, so
    a <= 2 bound discarded it. The miss is then cached as "section not found",
    i.e. permanent."""
    doc = (
        "TABLE OF CONTENTS\nRecent Sales of Unregistered Securities\n104\n"
        + ("Filler. " * 200) + "\n"
        "ITEM 15.\u00a0\u00a0RECENT SALES OF UNREGISTERED SECURITIES.\n"
        + (BODY * 2) + "\n"
        "ITEM 16.\u00a0\u00a0EXHIBITS\n"
    )
    got = extract_section(doc, r"recent\s+sales\s+of\s+unregistered\s+securities")
    check("a heading padded with non-breaking spaces is still a heading",
          "Sponsor purchased" in got, f"got {got[:70]!r}")


def test_heading_period_in_its_own_node_is_not_a_continuation():
    """EDGAR wraps the heading in <b>…</b> and frequently leaves the trailing
    "." outside, so it flattens onto its own line. Treating any line opening
    with punctuation as a cross-reference continuation lost the section on a
    real filing — and cached the miss. A line that is ONLY punctuation is a
    heading artefact."""
    doc = (
        "TABLE OF CONTENTS\nRecent Sales of Unregistered Securities\n104\n"
        + ("Filler. " * 200) + "\n"
        "Recent Sales of Unregistered Securities\n"
        ".\n"
        + (BODY * 2) + "\n"
        "Item 16. Exhibits\n"
    )
    got = extract_section(doc, r"recent\s+sales\s+of\s+unregistered\s+securities")
    check("a lone '.' after a heading is an artefact, not a continuation",
          "Sponsor purchased" in got, f"got {got[:70]!r}")


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
    print("all S-1 section-slicing tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
