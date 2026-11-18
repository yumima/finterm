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

    extra = 3
    from datetime import date

    def check(name, got, want):
        nonlocal failed, extra
        extra += 1
        ok = got == want
        failed += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {name}: {got!r}" + ("" if ok else f" (want {want!r})"))

    # ── the call, from a Q4 IR site's upcoming-events feed ──────────────────
    # Item shapes as served 2026-11-17 (investor.costco.com, .nvidia.com).
    def item(title, link, webcast=None):
        desc = (f"<description>&lt;br /&gt;&lt;a href=\"{webcast}\"&gt;View the live Webcast&lt;/a&gt;"
                "</description>") if webcast else "<description />"
        return f"<item><title>{title}</title><guid>x</guid>{desc}<link>{link}</link></item>"

    costco = "<rss><channel>" + "".join([
        item("12/2/2026 : November Sales Results", "https://ir/nov-sales"),
        item("12/10/2026 : Q1 2027 Earnings Results", "https://ir/q1-results"),
        item("12/10/2026 : Q1 2027 Earnings Call", "https://ir/q1-call", "https://events.q4inc.com/attendee/689013617"),
        item("1/6/2027 : December Sales Results", "https://ir/dec-sales"),
    ]) + "</channel></rss>"
    # Two items on the print's date: the one carrying the webcast wins.
    check("rss call", el.call_from_rss(costco, date(2026, 12, 10)),
          ("https://events.q4inc.com/attendee/689013617", "https://ir/q1-call", None))
    # A day off is still the print (Yahoo's date vs the call's).
    check("rss ±1 day", el.call_from_rss(costco, date(2026, 12, 11))[0],
          "https://events.q4inc.com/attendee/689013617")
    # A sales-results event on the date is not an earnings call…
    check("rss not a call", el.call_from_rss(costco, date(2026, 12, 2)), (None, None, None))
    # …and a different print's call is never borrowed.
    check("rss other print", el.call_from_rss(costco, date(2027, 3, 5)), (None, None, None))
    # Posted without its webcast link yet: the event's own page, never a guess.
    nv = "<rss>" + item("11/17/2026 : NVIDIA 3rd Quarter FY27 Financial Results", "https://ir/nvda-q3") + "</rss>"
    check("rss page only", el.call_from_rss(nv, date(2026, 11, 17)), (None, "https://ir/nvda-q3", None))
    # Two earnings items, neither with a webcast, one without a <link>: the
    # sort used to compare None with str and raise (review, reproduced).
    two = ("<rss><item><title>11/17/2026 : Q3 Earnings Call</title><description /></item>"
           + item("11/17/2026 : Q3 Earnings Results", "https://ir/a") + "</rss>")
    check("rss None vs str", el.call_from_rss(two, date(2026, 11, 17)), (None, "https://ir/a", None))
    # A replay file in the item is playable as-is.
    mp3 = "<rss>" + item("11/17/2026 : Q3 Earnings Call", "https://ir/c",
                         "https://cdn.example.com/q3-call.mp3") + "</rss>"
    check("rss media", el.call_from_rss(mp3, date(2026, 11, 17))[2], "https://cdn.example.com/q3-call.mp3")

    # ── the call, printed next to its date on an IR page (HD) ───────────────
    page = ("Q2 2026 Earnings Call August 18, 2026 https://event.choruscall.com/mediaframe/webcast.html?webcastid=OLD1 "
            + "Presentation slides and more. " * 10 +
            "Q3 2026 Earnings Call November 17, 2026 9:00 AM ET "
            "https://event.choruscall.com/mediaframe/webcast.html?webcastid=tz6tzaIE")
    check("page by date", el.call_near_date(page, date(2026, 11, 17)),
          "https://event.choruscall.com/mediaframe/webcast.html?webcastid=tz6tzaIE")
    check("page other date", el.call_near_date(page, date(2026, 11, 3)), None)
    # Day 7 must not match inside "17 November" (review, reproduced).
    check("day inside day", el.call_near_date("Q3 Earnings Call 17 November 2026 "
                                              "https://event.choruscall.com/mediaframe/webcast.html?webcastid=X",
                                              date(2026, 11, 7)), None)
    # A site/login link beside the date is not a call.
    check("page not a call", el.call_near_date("November 17, 2026 https://identity.q4inc.com/oauth/auth "
                                               "https://www.youtube.com/Salesforce", date(2026, 11, 17)), None)

    # ── a call link outlives the host's "upcoming events" feed ───────────────
    # get_earnings_links end to end, with EDGAR and the IR site faked; the
    # store in a temp dir, and every patched name restored afterwards.
    import shutil
    import tempfile
    tmp = tempfile.mkdtemp()
    patched = {n: getattr(el, n) for n in ("_calls_path", "_cik_for", "_earnings_8ks", "_filing",
                                           "_addresses_in", "_find_call", "_today_et")}
    try:
        el._calls_path = lambda: os.path.join(tmp, "calls.json")
        el._cik_for = lambda sym: 1045810
        el._earnings_8ks = lambda cik: ({"filingDate": ["2026-11-17"]}, [0])
        el._filing = lambda cik, rec, i: ("https://sec/q3fy27pr.htm", ["https://sec/q3fy27pr.htm"], "2026-11-17")
        el._addresses_in = lambda docs: ("https://investor.nvidia.com", "https://investor.nvidia.com")
        el._today_et = lambda: date(2026, 11, 17)
        feed = {"url": "https://events.q4inc.com/attendee/902213296", "page": "https://ir/nvda-q3",
                "media": "https://cdn/replay.mp3", "ir_url": "https://investor.nvidia.com/home/default.aspx"}
        el._find_call = lambda site, d, deadline: dict(feed)
        r = el.get_earnings_links("NVDA", "2026-11-17")
        check("found call", (r["call"]["url"], (r["audio"] or {}).get("kind")),
              ("https://events.q4inc.com/attendee/902213296", "media"))
        # After the call the feed has moved on: no call, no media.
        el._find_call = lambda site, d, deadline: {"ir_url": feed["ir_url"]}
        r = el.get_earnings_links("NVDA", "2026-11-17")
        check("remembered call", (r["call"]["url"], r["call"]["source"], (r["audio"] or {}).get("url"), r["note"]),
              ("https://events.q4inc.com/attendee/902213296", "remembered", "https://cdn/replay.mp3", ""))
        # Seen again WITHOUT the media: the media found earlier is not forgotten.
        el._find_call = lambda site, d, deadline: {"ir_url": feed["ir_url"], "url": feed["url"], "page": None}
        el.get_earnings_links("NVDA", "2026-11-17")
        check("media kept", el.remembered_call("NVDA", date(2026, 11, 17))["media"], "https://cdn/replay.mp3")
        # A malformed store is ignored, never raised.
        with open(os.path.join(tmp, "calls.json"), "w") as f:
            f.write('{"NVDA|2026-11-17": "https://x"}')
        check("malformed store", el.remembered_call("NVDA", date(2026, 11, 17)), None)
        # Old prints are pruned on the next write.
        el.remember_call("NVDA", date(2026, 11, 17), {"url": "u"}, today=date(2026, 11, 17))
        el.remember_call("COST", date(2028, 1, 10), {"url": "v"}, today=date(2028, 1, 10))
        check("old pruned", el.remembered_call("NVDA", date(2026, 11, 17)), None)
        # An unwritable store never breaks the answer.
        el._calls_path = lambda: "/proc/no/such/dir/calls.json"
        el.remember_call("NVDA", date(2026, 11, 17), {"url": "u"})
        check("unwritable store", True, True)
    finally:
        for n, f in patched.items():
            setattr(el, n, f)
        shutil.rmtree(tmp, ignore_errors=True)

    # ── which 8-K is the print's release ─────────────────────────────────────
    rec = {"filingDate": ["2026-11-23", "2026-11-19", "2026-11-16"]}
    # Fri 20 Nov after-close print, filed after 17:30 → dated Mon 23 Nov.
    check("release Monday after Friday", el._release_for(rec, [0, 1, 2], date(2026, 11, 20)), 0)
    # A guidance 2.02 the day BEFORE the print is not its release.
    check("release not day before", el._release_for({"filingDate": ["2026-11-19"]}, [0], date(2026, 11, 20)), None)
    check("release other quarter", el._release_for(rec, [0, 1, 2], date(2027, 2, 20)), None)

    # ── what finterm may play itself ─────────────────────────────────────────
    today = date(2026, 11, 17)
    check("audio youtube video", el.playable_audio("https://www.youtube.com/watch?v=X9mm", None, "", today, today),
          {"url": "https://www.youtube.com/watch?v=X9mm", "kind": "youtube"})
    check("audio media first", el.playable_audio("https://events.q4inc.com/attendee/1", "https://c/x.mp3", "",
                                                 today, today), {"url": "https://c/x.mp3", "kind": "media"})
    # A registration-gated webcast is never "playable".
    check("audio q4 gated", el.playable_audio("https://events.q4inc.com/attendee/1", None,
                                              "https://investor.nvidia.com", today, today), None)
    # A channel plays its live stream only around the print…
    check("audio channel live", el.playable_audio(None, None, "https://www.youtube.com/netflixir", today, today),
          {"url": "https://www.youtube.com/netflixir/live", "kind": "youtube_live"})
    # …not weeks ahead, when /live would be whatever else is streaming.
    check("audio channel early", el.playable_audio(None, None, "https://www.youtube.com/netflixir",
                                                   date(2027, 1, 20), today), None)
    # Last quarter's call video is never offered as the IR site.
    check("site skips calls", el.find_webcast_url(
        "The conference call webcast is at https://www.youtube.com/watch?v=OLD and investor.example.com.",
        sites_only=True), "https://investor.example.com")

    # ── whose identity each request carries ──────────────────────────────────
    # The user's contact is declared to SEC alone — decided by the host, so an
    # exhibit link in a filing that points off sec.gov never carries it.
    sent = []

    def fake_urlopen(req, timeout=None):
        sent.append(req.get_header("User-agent"))
        raise OSError("offline")

    real_open, real_env = el._OPENER.open, os.environ.get("FINTERM_CONTACT_EMAIL")
    el._OPENER.open, os.environ["FINTERM_CONTACT_EMAIL"] = fake_urlopen, "me@example.com"
    try:
        for url in ("https://www.sec.gov/Archives/x.htm", "https://evil.example.com/sec.gov/x.htm",
                    "https://sec.gov.example.com/x.htm"):
            try:
                el._get(url, as_json=False)
            except el._FetchFailed:
                pass
        # …and a sec.gov URL that redirects off SEC does not take it along
        hop = el._ContactStaysOnSec()
        req = el.urllib.request.Request("https://www.sec.gov/x", headers={"User-Agent": el.user_agent()})
        off = hop.redirect_request(req, None, 302, "Found", {}, "https://evil.example.com/x")
        on = hop.redirect_request(req, None, 301, "Moved", {}, "https://data.sec.gov/x")
        sent += [off.get_header("User-agent"), on.get_header("User-agent")]
    finally:
        el._OPENER.open = real_open
        if real_env is None:
            os.environ.pop("FINTERM_CONTACT_EMAIL", None)
        else:
            os.environ["FINTERM_CONTACT_EMAIL"] = real_env
    check("contact only to sec.gov", ["me@example.com" in (ua or "") for ua in sent], [True, False, False, False, True])

    print(f"{len(CASES) + extra - failed}/{len(CASES) + extra} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
