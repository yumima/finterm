#!/usr/bin/env python3
"""Where an earnings release says its call is webcast — offline.

The sentences are the real wording of 2026 releases (EX-99.1 exhibits on
EDGAR). The extractor turns filing text into a clickable link, so the cases
pin both halves: it finds the address in each house style, and it never
promotes a contact email, a newsroom, a social account or a non-web scheme.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import earnings_links as el  # noqa: E402

CASES = [
    # bare domain + path, no scheme
    ("AMZN", "Conference Call Information A conference call will be webcast live today at 2:00 p.m. PT/5:00 p.m. "
             "ET, and will be available for at least three months at amazon.com/ir. Contact ir@amazon.com.",
     "https://amazon.com/ir"),
    # a YouTube channel written without a domain
    ("NFLX", "Earnings Interview, 1:45pm PT Our live video interview will be on youtube/netflixir at 1:45pm PT "
             "today.",
     "https://www.youtube.com/netflixir"),
    # the call's own video — the query string is the whole point
    ("GOOGL", "A live audio webcast of our third quarter 2026 earnings release call will be available on YouTube "
              "at https://www.youtube.com/watch?v=X9mmCagxJTo. The call begins today at 1:30 PM (PT).",
     "https://www.youtube.com/watch?v=X9mmCagxJTo"),
    # investor page outranks the newsroom named in the same paragraph
    ("MSFT", "Microsoft will host a conference call and live webcast at 2:30 p.m. Pacific time. Visit "
             "http://www.microsoft.com/news for the release and http://www.microsoft.com/en-us/investor for the "
             "webcast.",
     "http://www.microsoft.com/en-us/investor"),
    # a direct events link outranks the IR home page
    ("events", "The conference call will be webcast at investor.example.com; to register go to "
               "https://events.q4inc.com/attendee/123456789.",
     "https://events.q4inc.com/attendee/123456789"),
    # social accounts and the SEC are never the answer
    ("social", "Follow the earnings call on https://twitter.com/acme or read it at https://www.sec.gov/x.htm.",
     None),
    # a URL nowhere near a call keyword is not a webcast
    ("far", "Our sustainability report is at https://acme.com/esg. " + "Revenue rose. " * 80 +
            "The conference call details will follow.",
     None),
    # nothing but an email address
    ("email", "Questions about the conference call: investors@acme.com.", None),
    # a company NAME ending in a TLD is not an address
    ("brand", "Booking.com will host a conference call today; Overstock.com, Inc. management will attend.", None),
    # ...but a bare www. host is (JPM, AMD write it that way)
    ("www", "The conference call will be webcast at www.jpmorganchase.com.", "https://www.jpmorganchase.com"),
    # a scheme that must never become a link
    ("scheme", "Listen to the call via javascript:alert(1) today.", None),
]


def main():
    failed = 0
    for name, text, want in CASES:
        got = el.find_webcast_url(text)
        ok = got == want
        failed += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {name}: {got!r}" + ("" if ok else f" (want {want!r})"))

    # Anchor targets survive the HTML → text step (a release that links
    # "our investor website" carries the URL only in the href).
    t = el._plain_text('<p>The webcast is on <a href="https://ir.acme.com/events">our website</a>.</p>')
    got = el.find_webcast_url(t)
    ok = got == "https://ir.acme.com/events"
    failed += not ok
    print(f"{'ok  ' if ok else 'FAIL'} href: {got!r}")

    # The filing index's document table: EX-99.1 found, the inline-XBRL viewer
    # prefix stripped from the cover document's link.
    idx = ('<table><tr><td>1</td><td>8-K</td><td><a href="/ix?doc=/Archives/edgar/data/1/2/a8-k.htm">a8-k.htm</a>'
           '</td><td>8-K</td><td>1</td></tr><tr><td>2</td><td>Release</td><td><a href="/Archives/edgar/data/1/2/'
           'ex991.htm">ex991.htm</a></td><td>EX-99.1</td><td>1</td></tr></table>')
    got = el._exhibits(idx)
    want = [("8-K", "https://www.sec.gov/Archives/edgar/data/1/2/a8-k.htm"),
            ("EX-99.1", "https://www.sec.gov/Archives/edgar/data/1/2/ex991.htm")]
    ok = got == want
    failed += not ok
    print(f"{'ok  ' if ok else 'FAIL'} exhibits: {got!r}")

    # The press release first, whatever order the index lists exhibits in.
    got = el._ex99_first([("EX-99.2", "b"), ("8-K", "c"), ("EX-99.1", "a"), ("EX-99.3", "d")])
    ok = got == ["a", "b", "d"]
    failed += not ok
    print(f"{'ok  ' if ok else 'FAIL'} ex99 order: {got!r}")

    print(f"{len(CASES) + 3 - failed}/{len(CASES) + 3} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
