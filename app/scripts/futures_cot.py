"""CFTC Commitments of Traders for one futures market — weekly history.

    futures_cot.py <cftc_contract_market_code> [weeks=156]

Legacy futures-only report (the headline one): non-commercial (large
speculators), commercial (hedgers) and non-reportable positions against open
interest. Queried by the exact market code, so a name search can never mix two
markets. Prints {"code", "market", "rows": [{date, oi, nc_long, nc_short,
c_long, c_short, nr_long, nr_short}]} oldest first, or {"error": str}.
A field the CFTC did not report is null, never 0.
"""

import json
import sys
from datetime import date, timedelta

import requests

from finterm_identity import project_user_agent

URL = "https://publicreporting.cftc.gov/resource/6dca-aqww.json"

FIELDS = {
    "oi": "open_interest_all",
    "nc_long": "noncomm_positions_long_all",
    "nc_short": "noncomm_positions_short_all",
    "c_long": "comm_positions_long_all",
    "c_short": "comm_positions_short_all",
    "nr_long": "nonrept_positions_long_all",
    "nr_short": "nonrept_positions_short_all",
}


def _int(v):
    try:
        return int(float(v))
    except (TypeError, ValueError):
        return None


def shape(records):
    rows, market = [], ""
    for r in records or []:
        d = str(r.get("report_date_as_yyyy_mm_dd") or "")[:10]
        if len(d) != 10:
            continue
        market = market or str(r.get("market_and_exchange_names") or "")
        row = {"date": d}
        for key, src in FIELDS.items():
            row[key] = _int(r.get(src))
        rows.append(row)
    rows.sort(key=lambda x: x["date"])
    return market, rows


def fetch(code, weeks=156):
    code = str(code or "").strip().upper()
    if not code.isalnum() or len(code) != 6:
        return {"error": f"not a CFTC market code: {code!r}"}
    since = (date.today() - timedelta(weeks=int(weeks))).isoformat()
    try:
        r = requests.get(URL, timeout=30, headers={"User-Agent": project_user_agent()}, params={
            "$select": ",".join(["report_date_as_yyyy_mm_dd", "market_and_exchange_names", *FIELDS.values()]),
            "$where": f"cftc_contract_market_code='{code}' AND report_date_as_yyyy_mm_dd >= '{since}'",
            "$order": "report_date_as_yyyy_mm_dd ASC",
            "$limit": 1000,
        })
        r.raise_for_status()
        market, rows = shape(r.json())
    except Exception as e:
        return {"error": f"CFTC did not answer ({str(e)[:160]})"}
    if not rows:
        return {"error": f"the CFTC reports no positions for market {code}"}
    return {"code": code, "market": market, "rows": rows}


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(json.dumps({"error": "usage: futures_cot.py <cftc_contract_market_code> [weeks]"}))
        sys.exit(1)
    print(json.dumps(fetch(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 156)))
