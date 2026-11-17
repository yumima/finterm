#!/usr/bin/env python3
"""Clarity page parsers for the Private Market screen.

No network: the pages are shaped from live samples taken 2026-11-17 (Stripe,
Anthropic, the C50 index page, page 1 of browse-companies, the sitemap).
"""

import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import clarity_market as cm  # noqa: E402

FAILURES = []


def page(obj):
    """An HTML page whose RSC payload carries `obj`, split across two pushes
    the way Next.js streams it (the split lands mid-object)."""
    s = 'x:{"a":1,"company":' + json.dumps(obj, separators=(",", ":")) + '}'  # compact, as served
    half = len(s) // 2
    chunks = [s[:half], s[half:]]
    return "<html>" + "".join(
        '<script>self.__next_f.push([1,%s])</script>' % json.dumps(c) for c in chunks) + "</html>"


STRIPE = {
    "nameSlug": "stripe", "name": "Stripe", "isLiquidityIndexMember": True,
    "heroTags": ["Payments", "Payments Infrastructure"], "status": "LISTED", "isTradeable": True,
    "ipoDate": None, "acquiredAt": None, "ipoPrice": "—", "publicTicker": None,
    "description": "Payments APIs.",
    "price": {"clarityPrice": "$$73.70", "changes": {
        "max": {"label": "+84.25% all time"}, "oneYear": {"label": "+48.17% past year"},
        "sixMonths": {"label": "+6.78% past 6 months"}, "threeMonths": {"label": "-4.32% past 3 months"}}},
    "chart": {"points": [{"day": "2026-11-15", "value": 73.69}, {"day": "2026-11-16", "value": 0},
                         {"day": "2026-11-17", "value": 73.7}], "currency": "USD"},
    "priceDate": "November 17, 2026",
    "facts": {"founded": "2010", "employees": "5001-10000", "headquarters": "South San Francisco, California",
              "website": "stripe.com", "websiteHref": "https://stripe.com"},
    "funding": {"totalRounds": 3, "totalRaised": "$$8.6B", "keyInvestors": "Coatue, Thrive Capital",
                "rounds": [
                    {"closeDate": "February 2026", "seriesClass": "Tender Offer", "isMostRecent": True,
                     "raised": "—", "postMoney": "$$159B", "pps": "$$63.00", "investors": "Coatue, a16z"},
                    {"closeDate": "March 2023", "seriesClass": "Series I", "raised": "$$6.5B",
                     "postMoney": "$$50B", "pps": "$$20.13", "investors": "Thrive Capital"},
                    {"closeDate": "July 2012", "seriesClass": "Series B", "raised": "$$20M",
                     "postMoney": "—", "pps": "—", "investors": "—"}]},
}


def test_money_strings():
    assert cm.parse_money("$$73.70") == 73.7
    assert cm.parse_money("$$159B") == 159e9
    assert cm.parse_money("$$694.2M") == 694.2e6
    assert cm.parse_money("$$1,011.54") == 1011.54
    assert cm.parse_money("$$1.47T") == 1.47e12
    assert cm.parse_money({"amount": "5430000000", "currency": "USD"}) == 5.43e9
    # Absent is absent — never 0 (the recurring vendor-null seam).
    for absent in ("—", "", None, "N/A"):
        assert cm.parse_money(absent) is None, absent


def test_pct_and_dates():
    assert cm.parse_pct("+84.25% all time") == 84.25
    assert cm.parse_pct("-4.32% past 3 months") == -4.32
    assert cm.parse_pct("-23.63") == -23.63
    assert cm.parse_pct(None) is None
    assert cm.parse_month("February 2026") == "2026-02"
    assert cm.parse_month("—") is None
    assert cm.parse_long_date("November 17, 2026") == "2026-11-17"
    assert cm.parse_long_date("17 Nov") is None


def test_company_page():
    c = cm.parse_company_page(page(STRIPE))
    assert c["slug"] == "stripe" and c["name"] == "Stripe" and c["c50"]
    assert c["price"] == 73.7 and c["price_date"] == "2026-11-17"
    assert c["chg_3m"] == -4.32 and c["chg_1y"] == 48.17 and c["chg_max"] == 84.25
    # A zero price point is a hole in the series, not a price.
    assert c["points"] == [["2026-11-15", 73.69], ["2026-11-17", 73.7]], c["points"]
    r0, r1, r2 = c["rounds"]
    assert r0 == {"month": "2026-02", "series": "Tender Offer", "tender": True, "raised": None,
                  "post_money": 159e9, "pps": 63.0, "investors": ["Coatue", "a16z"]}, r0
    assert not r1["tender"] and r1["pps"] == 20.13 and r1["raised"] == 6.5e9
    assert r2["post_money"] is None and r2["pps"] is None and r2["investors"] == []
    assert c["total_raised"] == 8.6e9 and c["website"] == "https://stripe.com"
    assert c["ipo_price"] is None and c["public_ticker"] is None


def test_listed_company_dates():
    # SpaceX, 2026-11-17: listed in June; Clarity spells the date out.
    a = dict(STRIPE, nameSlug="spacex", name="SpaceX", publicTicker="SPCX", ipoDate="June 12, 2026",
             ipoPrice="$$135.00", acquiredAt=None)
    c = cm.parse_company_page(page(a))
    assert c["public_ticker"] == "SPCX" and c["ipo_date"] == "2026-06-12" and c["ipo_price"] == 135.0, c
    b = dict(STRIPE, acquiredAt="2025-03-01T00:00:00Z")
    assert cm.parse_company_page(page(b))["acquired_at"] == "2025-03-01"


def test_company_without_price():
    # Anthropic, 2026-11-17: covered, rounds with PPS, but no Clarity Price.
    a = dict(STRIPE, nameSlug="anthropic", name="Anthropic",
             price={"clarityPrice": "—", "changes": {}}, chart={"points": [], "ranges": []})
    c = cm.parse_company_page(page(a))
    assert c["price"] is None and c["points"] == [] and c["chg_1y"] is None
    assert c["rounds"][0]["pps"] == 63.0


def test_not_a_company_page():
    assert cm.parse_company_page("<html>Not found</html>") is None
    assert cm.parse_company_page(page({"price": {"clarityPrice": "$$1"}})) is None  # no slug


def test_index_page():
    obj = {"index": {"level": "181.45", "updatedOn": "November 17, 2026",
                     "changes": {"oneYear": {"label": "+55.04% past year"}},
                     "points": [{"day": "2023-01-01", "value": 99.93}, {"day": "2026-11-17", "value": 181.45}]},
           "members": [{"rank": 1, "rankChange": 2, "rankChangeDirection": "UP", "name": "Lambda",
                        "nameSlug": "lambda", "clarityPrice": "$$50.46", "sixMonthReturn": "+12.13%",
                        "lastRoundValuation": "$$5.4B", "sparkline": [4341, 4347]},
                       {"rank": 2, "rankChange": 3, "rankChangeDirection": "DOWN", "name": "X",
                        "nameSlug": "x", "clarityPrice": "—", "sixMonthReturn": "—",
                        "lastRoundValuation": "—", "sparkline": []}]}
    d = cm.parse_index_page(page(obj))
    assert d["level"] == 181.45 and d["updated"] == "2026-11-17" and d["chg_1y"] == 55.04
    assert d["chg_3m"] is None
    assert len(d["points"]) == 2
    m0, m1 = d["members"]
    assert m0["slug"] == "lambda" and m0["price"] == 50.46 and m0["chg_6m"] == 12.13
    assert m0["last_round_valuation"] == 5.4e9 and m0["rank_change"] == 2
    assert m1["rank_change"] == -3 and m1["price"] is None and m1["chg_6m"] is None


def test_browse_and_sitemap():
    obj = {"__typename": "CompanySearchResultOffsetPage",
           "items": [{"name": "Ripple Labs", "nameSlug": "ripple-labs",
                      "clarityPrice": {"amount": "91.69", "currency": "USD"}, "sixMonthReturn": "-23.63",
                      "lastRoundValuation": {"amount": "50000000000", "currency": "USD"}}],
           "pageInfo": {"totalCount": 3143}}
    rows, total = cm.parse_browse_page(page(obj))
    assert total == 3143 and rows[0]["price"] == 91.69 and rows[0]["chg_6m"] == -23.63
    assert rows[0]["last_round_valuation"] == 5e10
    xml = ("<urlset><url><loc>https://www.clarity.com/browse-companies/ripple-labs-stock</loc></url>"
           "<url><loc>https://www.clarity.com/guides/x</loc></url>"
           "<url><loc>https://www.clarity.com/browse-companies/ripple-labs-stock</loc></url>"
           "<url><loc>https://www.clarity.com/browse-companies/open-ai-stock</loc></url></urlset>")
    assert cm.parse_sitemap(xml) == ["ripple-labs", "open-ai"]


def test_cache_stale_fallback(tmp="/tmp"):
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        cache = cm.Cache(os.path.join(d, "c.sqlite"))
        calls = []
        v, _, stale = cm._cached(cache, "k", 3600, lambda: calls.append(1) or "body", lambda b: {"ok": b})
        assert v == {"ok": "body"} and not stale
        v, _, stale = cm._cached(cache, "k", 3600, lambda: calls.append(1) or "body", lambda b: {"ok": b})
        assert len(calls) == 1 and not stale  # fresh cache, no fetch
        # Expired and the site does not answer: the last copy, flagged stale.
        v, _, stale = cm._cached(cache, "k", 0, lambda: None, lambda b: {"ok": b})
        assert v == {"ok": "body"} and stale


def test_404_and_failure_are_different_answers():
    import tempfile
    real = cm._get
    try:
        with tempfile.TemporaryDirectory() as d:
            cache = cm.Cache(os.path.join(d, "c.sqlite"))
            cm._get = lambda url: "" if "gone" in url else None  # 404 vs no answer
            r = cm.build_companies(cache, ["gone-co", "flaky-co", "BAD SLUG"])
            assert r["errors"] == {"gone-co": "not_found", "flaky-co": "fetch_failed", "BAD SLUG": "not_found"}, r
            cm._get = lambda url: page(STRIPE)
            r = cm.build_companies(cache, ["stripe"])
            assert r["companies"]["stripe"]["price"] == 73.7 and not r["errors"]
    finally:
        cm._get = real


def main():
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"ok   {name}")
            except AssertionError as e:
                FAILURES.append(name)
                print(f"FAIL {name}: {e}")
    if FAILURES:
        sys.exit(1)


if __name__ == "__main__":
    main()
