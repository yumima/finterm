#!/usr/bin/env python3
"""Form 4 parsing and insider classification for the OWNERSHIP screen.

These run the SHIPPED functions from sec_ownership_data — no network, no
mirrored copies that could drift away from the code they claim to cover.

Three things here were found by testing against live EDGAR rather than by
reading the code, and each has a test below:

  • EDGAR carries the beneficial-ownership schedules under TWO spellings in the
    same submissions index — "SC 13D/A" and "SCHEDULE 13D/A". GameStop's index
    has all four variants at once. Matching one spelling silently dropped the
    newer structured filings, which reads as "no activist stakes" on an issuer
    that has several.

  • The routine/opportunistic classification needs years of history per insider,
    but the transaction table has to be capped — Ford filed 361 Form 4s in four
    years. Taking the most recent N collapsed the window to about one year and
    left every insider permanently "unclassified" on exactly the large caps
    people look at. Classification now reads each insider's own submissions
    index instead, so the cap and the classification stop fighting.

  • primaryDocument is often the XSL-rendered path; the machine-readable XML
    sits beside it with that prefix stripped.

The recurring principle in the assertions: a field absent from the filing must
be ABSENT from the output, never defaulted. A price of 0 is a real filed value
(a gift, a grant) and a price that was not reported is not, and the UI can only
tell them apart if the parser does.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

import sec_ownership_data as so  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


# A Form 4 with the exact shape EDGAR serves: <value> wrappers on the leaves,
# one reporting owner with two roles, and three transactions — an open-market
# purchase with a price, a grant with no price reported, and a derivative row.
FORM4 = b"""<?xml version="1.0"?>
<ownershipDocument>
  <issuer>
    <issuerCik>0000320193</issuerCik>
    <issuerName>Example Corp</issuerName>
    <issuerTradingSymbol>EXC</issuerTradingSymbol>
  </issuer>
  <reportingOwner>
    <reportingOwnerId>
      <rptOwnerCik>0001111111</rptOwnerCik>
      <rptOwnerName>Doe Jane</rptOwnerName>
    </reportingOwnerId>
    <reportingOwnerRelationship>
      <isDirector>1</isDirector>
      <isOfficer>1</isOfficer>
      <officerTitle>Chief Financial Officer</officerTitle>
    </reportingOwnerRelationship>
  </reportingOwner>
  <nonDerivativeTable>
    <nonDerivativeTransaction>
      <securityTitle><value>Common Stock</value></securityTitle>
      <transactionDate><value>2026-03-04</value></transactionDate>
      <transactionCoding><transactionCode>P</transactionCode></transactionCoding>
      <transactionAmounts>
        <transactionShares><value>1500</value></transactionShares>
        <transactionPricePerShare><value>42.50</value></transactionPricePerShare>
        <transactionAcquiredDisposedCode><value>A</value></transactionAcquiredDisposedCode>
      </transactionAmounts>
      <postTransactionAmounts>
        <sharesOwnedFollowingTransaction><value>20000</value></sharesOwnedFollowingTransaction>
      </postTransactionAmounts>
      <ownershipNature>
        <directOrIndirectOwnership><value>D</value></directOrIndirectOwnership>
      </ownershipNature>
    </nonDerivativeTransaction>
    <nonDerivativeTransaction>
      <securityTitle><value>Common Stock</value></securityTitle>
      <transactionDate><value>2026-02-01</value></transactionDate>
      <transactionCoding><transactionCode>A</transactionCode></transactionCoding>
      <transactionAmounts>
        <transactionShares><value>800</value></transactionShares>
        <transactionAcquiredDisposedCode><value>A</value></transactionAcquiredDisposedCode>
      </transactionAmounts>
    </nonDerivativeTransaction>
  </nonDerivativeTable>
  <derivativeTable>
    <derivativeTransaction>
      <securityTitle><value>Stock Option</value></securityTitle>
      <transactionDate><value>2026-02-01</value></transactionDate>
      <transactionCoding><transactionCode>M</transactionCode></transactionCoding>
      <transactionAmounts>
        <transactionShares><value>500</value></transactionShares>
        <transactionAcquiredDisposedCode><value>D</value></transactionAcquiredDisposedCode>
      </transactionAmounts>
    </derivativeTransaction>
  </derivativeTable>
</ownershipDocument>
"""


def test_parse_reads_the_filed_values():
    rows = so.parse_form4(FORM4, "http://x/form4.xml")
    check("parse: three transactions", len(rows) == 3, f"got {len(rows)}")
    buy = [r for r in rows if r["code"] == "P"][0]
    check("parse: insider name", buy["insider"] == "Doe Jane", buy["insider"])
    check("parse: issuer symbol", buy["symbol"] == "EXC", buy["symbol"])
    check("parse: shares", buy.get("shares") == 1500.0, str(buy.get("shares")))
    check("parse: price", buy.get("price") == 42.50, str(buy.get("price")))
    check("parse: value is shares x price", buy.get("value") == 1500 * 42.50)
    check("parse: direction", buy.get("direction") == "acquired")
    check("parse: shares held after", buy.get("shares_held_after") == 20000.0)
    check("parse: ownership", buy.get("ownership") == "direct")
    check("parse: source url carried", buy["source_url"] == "http://x/form4.xml")


def test_roles_include_the_officer_title():
    rows = so.parse_form4(FORM4, "u")
    roles = rows[0]["roles"]
    check("roles: director", "Director" in roles, str(roles))
    check("roles: officer title not the word Officer",
          "Chief Financial Officer" in roles, str(roles))


def test_absent_fields_are_absent_not_zero():
    # The grant reports no price. A defaulted 0 would render as "bought at
    # $0.00" — a confident number the filing never stated.
    rows = so.parse_form4(FORM4, "u")
    grant = [r for r in rows if r["code"] == "A"][0]
    check("absent: no price key", "price" not in grant, str(grant.get("price")))
    check("absent: no value key", "value" not in grant)
    check("absent: no shares_held_after", "shares_held_after" not in grant)
    check("absent: shares still present", grant.get("shares") == 800.0)


def test_open_market_flag_separates_decisions_from_mechanics():
    rows = so.parse_form4(FORM4, "u")
    by_code = {r["code"]: r for r in rows}
    check("open_market: P is a decision", by_code["P"]["open_market"] is True)
    check("open_market: A grant is not", by_code["A"]["open_market"] is False)
    check("open_market: M exercise is not", by_code["M"]["open_market"] is False)
    check("derivative flag set on the option row", by_code["M"]["derivative"] is True)
    check("derivative flag clear on common stock", by_code["P"]["derivative"] is False)


def test_both_edgar_spellings_of_schedule_13d():
    # The live bug: GameStop's index carries all four at once.
    check("form: SC 13D/A canonical", so._norm_form("SC 13D/A") == "SC 13D/A")
    check("form: SCHEDULE 13D/A normalises",
          so._norm_form("SCHEDULE 13D/A") == "SC 13D/A", so._norm_form("SCHEDULE 13D/A"))
    check("form: SCHEDULE 13G normalises", so._norm_form("SCHEDULE 13G") == "SC 13G")
    check("form: lowercase input", so._norm_form("schedule 13d") == "SC 13D")
    check("form: Form 4 untouched", so._norm_form("4") == "4")
    check("form: unrelated form untouched", so._norm_form("10-K") == "10-K")


def test_raw_xml_url_strips_the_xsl_wrapper():
    u = so._raw_xml_url("0000320193", "0001140361-26-032884", "xslF345X03/form4.xml")
    check("url: xsl prefix stripped", u.endswith("/form4.xml") and "xsl" not in u, str(u))
    check("url: accession dashes removed", "000114036126032884" in u, str(u))
    check("url: cik not zero-padded in archive path", "/data/320193/" in u, str(u))
    plain = so._raw_xml_url("320193", "0001-26-1", "wf-form4_1.xml")
    check("url: already-raw doc passes through", plain.endswith("wf-form4_1.xml"))
    check("url: non-xml doc refused",
          so._raw_xml_url("1", "0001-26-1", "primary_doc.htm") is None)


def test_classification_declines_without_enough_history():
    # One year of filings cannot support a same-month-every-year test.
    tx = [{"insider": "Solo Trader", "date": "2026-01-05"},
          {"insider": "Solo Trader", "date": "2026-06-05"}]
    got = {e["insider"]: e for e in so.classify_insiders(tx)}["Solo Trader"]
    check("classify: unclassified on short history",
          got["pattern"] == "unclassified", got["pattern"])
    check("classify: says why", "year" in got.get("reason", ""), got.get("reason", ""))


def test_routine_is_the_same_month_across_years():
    tx = [{"insider": "Board Member", "date": "2026-07-14"}]
    hist = {"Board Member": ["2022-07-10", "2023-07-11", "2024-07-12",
                             "2025-07-13", "2026-07-14"]}
    got = {e["insider"]: e for e in so.classify_insiders(tx, hist)}["Board Member"]
    check("classify: routine", got["pattern"] == "routine", got["pattern"])
    check("classify: names the month", got.get("routine_month") == 7,
          str(got.get("routine_month")))
    check("classify: counts full history not the window",
          got["trades"] == 5 and got["trades_in_window"] == 1,
          f"{got['trades']}/{got['trades_in_window']}")


def test_opportunistic_is_scattered_across_years():
    tx = [{"insider": "Scattered", "date": "2026-04-02"}]
    hist = {"Scattered": ["2022-01-05", "2023-06-19", "2024-11-02",
                          "2025-03-27", "2026-04-02"]}
    got = {e["insider"]: e for e in so.classify_insiders(tx, hist)}["Scattered"]
    check("classify: opportunistic", got["pattern"] == "opportunistic", got["pattern"])
    check("classify: no routine month claimed", "routine_month" not in got)


def test_clusters_need_two_distinct_insiders_buying():
    same_person = [
        {"insider": "A", "code": "P", "date": "2026-05-01", "value": 10.0},
        {"insider": "A", "code": "P", "date": "2026-05-08", "value": 10.0},
    ]
    check("cluster: one person twice is not a cluster",
          so.find_clusters(same_person) == [], str(so.find_clusters(same_person)))

    two_people = [
        {"insider": "A", "code": "P", "date": "2026-05-01", "value": 10.0},
        {"insider": "B", "code": "P", "date": "2026-05-08", "value": 25.0},
    ]
    got = so.find_clusters(two_people)
    check("cluster: two people inside the window", len(got) == 1, str(got))
    if got:
        check("cluster: both named", got[0]["insiders"] == ["A", "B"])
        check("cluster: value summed", got[0]["total_value"] == 35.0)


def test_clusters_ignore_grants_and_distant_dates():
    grants = [
        {"insider": "A", "code": "A", "date": "2026-05-01", "value": 10.0},
        {"insider": "B", "code": "A", "date": "2026-05-02", "value": 10.0},
    ]
    check("cluster: simultaneous grants are a calendar artifact, not a cluster",
          so.find_clusters(grants) == [], str(so.find_clusters(grants)))

    far_apart = [
        {"insider": "A", "code": "P", "date": "2026-01-01", "value": 10.0},
        {"insider": "B", "code": "P", "date": "2026-04-01", "value": 10.0},
    ]
    check("cluster: outside the 30-day window",
          so.find_clusters(far_apart) == [], str(so.find_clusters(far_apart)))


def test_malformed_xml_yields_nothing_rather_than_raising():
    check("robust: garbage in, empty out", so.parse_form4(b"<not xml", "u") == [])
    check("robust: empty document", so.parse_form4(b"<ownershipDocument/>", "u") == [])


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
    print("all Form 4 / ownership parsing tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
