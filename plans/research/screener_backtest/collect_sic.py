"""SIC code per CIK (SEC submissions API) for the names fund_backtest can see.

The shipped Magic Formula and Quality+Momentum exclude Finance and Utilities
(TradingView sectors). The backtest that supplies their evidence must exclude
the same names: SIC 6000-6799 (finance, insurance, real estate incl. REITs —
TradingView's "Finance") and 4900-4999 (utilities).
"""
import json, os, pickle, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "fincept-qt", "scripts"))
from finterm_identity import user_agent

D = os.path.join(HERE, "data")
cik_of = pickle.load(open(os.path.join(D, "cik.pkl"), "rb"))
prices = pickle.load(open(os.path.join(D, "prices.pkl"), "rb"))
ciks = sorted({cik_of[s] for s in prices if s in cik_of})
out_path = os.path.join(D, "sic.pkl")
sic = pickle.load(open(out_path, "rb")) if os.path.exists(out_path) else {}
for i, cik in enumerate(ciks):
    if cik in sic:
        continue
    url = f"https://data.sec.gov/submissions/CIK{cik:010d}.json"
    try:
        req = urllib.request.Request(url, headers={"User-Agent": user_agent()})
        j = json.loads(urllib.request.urlopen(req, timeout=30).read())
        sic[cik] = int(j.get("sic") or 0) or None  # filed, but no SIC: genuinely none
    except urllib.error.HTTPError as e:
        if e.code == 403:
            pickle.dump(sic, open(out_path, "wb"))  # keep what was fetched
            raise SystemExit("SEC 403 -- set FINCEPT_CONTACT_EMAIL")
        if e.code == 404:
            sic[cik] = None
        # anything else is transient: left absent, so the next run retries it
        # (stored as None it read as "operating" and kept a bank in the screens)
    except Exception:
        pass
    time.sleep(0.12)
    if i % 100 == 0:
        pickle.dump(sic, open(out_path, "wb"))
        print(i, "/", len(ciks), flush=True)
pickle.dump(sic, open(out_path, "wb"))
missing = [c for c in ciks if c not in sic]
print("sic for", sum(v is not None for v in sic.values()), "of", len(ciks),
      f"({len(missing)} not fetched — re-run to retry)" if missing else "")
