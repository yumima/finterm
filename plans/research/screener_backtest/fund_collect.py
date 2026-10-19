"""Collect annual fundamentals for every SEC filer from the XBRL frames API.

One request per (concept, calendar year) returns that concept for every
company, so 17 years x ~20 concepts is ~350 requests instead of one
companyfacts download per name. Each row carries the period `end` date; the
backtest only lets a value be seen 120 days after that end (10-K deadline is
60-90 days), which is the point-in-time approximation. Frames hold the latest
filed value for a period, so restatements leak a little — noted, not fixed.
"""
import json, os, sys, time, pickle, urllib.request, urllib.error

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "fincept-qt", "scripts"))
from finterm_identity import user_agent

OUT = os.path.join(HERE, "data", "frames.pkl")
UA = {"User-Agent": user_agent(), "Accept-Encoding": "identity"}

DURATION = {  # flows over the fiscal year
    "Revenues": "USD", "RevenueFromContractWithCustomerExcludingAssessedTax": "USD",
    "SalesRevenueNet": "USD", "GrossProfit": "USD", "CostOfRevenue": "USD",
    "CostOfGoodsAndServicesSold": "USD", "OperatingIncomeLoss": "USD",
    "NetIncomeLoss": "USD", "NetCashProvidedByUsedInOperatingActivities": "USD",
    "WeightedAverageNumberOfDilutedSharesOutstanding": "shares",
    "PaymentsForRepurchaseOfCommonStock": "USD", "PaymentsOfDividends": "USD",
    "PaymentsOfDividendsCommonStock": "USD",
}
INSTANT = {  # balances at fiscal year end
    "Assets": "USD", "AssetsCurrent": "USD", "LiabilitiesCurrent": "USD",
    "LongTermDebtNoncurrent": "USD", "LongTermDebt": "USD",
    "CashAndCashEquivalentsAtCarryingValue": "USD",
    "PropertyPlantAndEquipmentNet": "USD", "StockholdersEquity": "USD",
}

def get(url):
    for attempt in range(4):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
                return json.loads(r.read())
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            if e.code == 403:  # undeclared identity: retrying only records empty frames
                raise SystemExit("SEC 403 -- set FINCEPT_CONTACT_EMAIL")
            time.sleep(2 + attempt * 3)
        except Exception:
            time.sleep(2 + attempt * 3)
    return None

data = {}
if os.path.exists(OUT):
    data = pickle.load(open(OUT, "rb"))
years = range(2008, 2026)
jobs = [(c, u, f"CY{y}") for c, u in DURATION.items() for y in years]
# Cover-page public float: dollars as of the fiscal Q2 end, never restated and
# immune to splits -- the market-cap basis (shares x split-adjusted price is
# off by every later split, and frames carry split-restated share counts).
# Fiscal Q2 falls in any calendar quarter (Apple: Q1, Microsoft: Q4), so all four.
jobs += [("dei/EntityPublicFloat", "USD", f"CY{y}Q{q}I") for y in years for q in (1, 2, 3, 4)]
jobs += [(c, u, f"CY{y}Q4I") for c, u in INSTANT.items() for y in years]
for i, (concept, unit, period) in enumerate(jobs):
    key = (concept, period)
    if key in data:
        continue
    j = get(f"https://data.sec.gov/api/xbrl/frames/{concept if '/' in concept else 'us-gaap/' + concept}/{unit}/{period}.json")
    data[key] = [(d["cik"], d["end"], d["val"]) for d in (j or {}).get("data", [])]
    time.sleep(0.15)
    if i % 25 == 0:
        print(i, "/", len(jobs), concept, period, len(data[key]), flush=True)
        pickle.dump(data, open(OUT, "wb"))
pickle.dump(data, open(OUT, "wb"))

tick = get("https://www.sec.gov/files/company_tickers.json")
pickle.dump({v["ticker"].replace(".", "-"): v["cik_str"] for v in tick.values()},
            open(os.path.join(HERE, "data", "cik.pkl"), "wb"))
print("done", len(data))
