"""
ONS Data Fetcher
UK Office for National Statistics (ONS): UK GDP, CPI, labour market, trade, population, housing.
"""
import sys
import json
import os
import requests
from typing import Dict, Any, Optional, List

BASE_URL = "https://api.ons.gov.uk/v1"

ONS_DATASETS = {
    "gdp": "cpih01",
    "cpi": "mm23",
    "unemployment": "lms",
    "trade": "ots",
    "population": "mid-year-pop-est",
    "housing": "hpssa"
}

ONS_TIMESERIES = {
    "gdp_quarterly": {"dataset": "qna", "timeseries": "ABMI"},
    "cpi_all_items": {"dataset": "mm23", "timeseries": "D7G7"},
    "unemployment_rate": {"dataset": "lms", "timeseries": "MGSX"},
    "exports_goods": {"dataset": "ots", "timeseries": "BOKH"},
    "imports_goods": {"dataset": "ots", "timeseries": "BOKJ"}
}

session = requests.Session()
adapter = requests.adapters.HTTPAdapter(pool_connections=10, pool_maxsize=10, max_retries=3)
session.mount('https://', adapter)
session.mount('http://', adapter)


def _make_request(endpoint: str, params: Dict = None) -> Any:
    url = f"{BASE_URL}/{endpoint}" if not endpoint.startswith('http') else endpoint
    try:
        response = session.get(url, params=params, timeout=30)
        response.raise_for_status()
        return response.json()
    except requests.exceptions.HTTPError as e:
        return {"error": f"HTTP {e.response.status_code}: {str(e)}"}
    except requests.exceptions.RequestException as e:
        return {"error": f"Request failed: {str(e)}"}
    except (json.JSONDecodeError, ValueError) as e:
        return {"error": f"JSON decode error: {str(e)}"}


def get_dataset(dataset_id: str) -> Any:
    return _make_request(f"datasets/{dataset_id}")


def get_timeseries(dataset_id: str, timeseries_id: str, start_year: str = None, end_year: str = None) -> Any:
    endpoint = f"datasets/{dataset_id}/timeseries/{timeseries_id}/data"
    params = {}
    if start_year:
        params["startYear"] = start_year
    if end_year:
        params["endYear"] = end_year
    return _make_request(endpoint, params=params)


# ── Headline series used by the Economics ▸ ONS panel ───────────────────────
# The old api.ons.gov.uk/v1 timeseries endpoint is retired (404). The ONS
# website serves the same series as JSON at <uri>/data, keyed by CDID.
ONS_SITE = "https://www.ons.gov.uk"
ONS_SERIES = {
    "gdp":           ("/economy/grossdomesticproductgdp/timeseries/abmi/qna",
                      "GDP: chained volume measures, SA (£m)"),
    "cpi":           ("/economy/inflationandpriceindices/timeseries/d7g7/mm23",
                      "CPI annual rate: all items (%)"),
    "cpih":          ("/economy/inflationandpriceindices/timeseries/l55o/mm23",
                      "CPIH annual rate: all items (%)"),
    "rpi":           ("/economy/inflationandpriceindices/timeseries/czbh/mm23",
                      "RPI: % change over 12 months"),
    "unemployment":  ("/employmentandlabourmarket/peoplenotinwork/unemployment/timeseries/mgsx/lms",
                      "Unemployment rate, 16+, SA (%)"),
    "employment":    ("/employmentandlabourmarket/peopleinwork/employmentandemployeetypes/timeseries/lf24/lms",
                      "Employment rate, 16-64, SA (%)"),
    "trade_balance": ("/economy/nationalaccounts/balanceofpayments/timeseries/ikbj/pnbp",
                      "Total trade balance, BoP, CP, SA (£m)"),
    "avg_earnings":  ("/employmentandlabourmarket/peopleinwork/earningsandworkinghours/timeseries/kab9/lms",
                      "AWE: whole economy total pay, SA (£/week)"),
    "public_debt":   ("/economy/governmentpublicsectorandtaxes/publicsectorfinance/timeseries/hf6x/pusf",
                      "PSND ex public sector banks (% of GDP, NSA)"),
}

_MONTHS = {m: i + 1 for i, m in enumerate(
    ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"])}


def _iso_period(label: str) -> Optional[str]:
    """'2026 AUG' -> '2026-08', '2026 Q2' -> '2026-Q2', '2026' -> '2026'."""
    parts = label.strip().upper().split()
    if len(parts) == 1 and parts[0].isdigit():
        return parts[0]
    if len(parts) == 2 and parts[0].isdigit():
        if parts[1] in _MONTHS:
            return f"{parts[0]}-{_MONTHS[parts[1]]:02d}"
        if parts[1] in ("Q1", "Q2", "Q3", "Q4"):
            return f"{parts[0]}-{parts[1]}"
    return None


def get_series(key: str, start_year: Optional[str] = None) -> Dict[str, Any]:
    if key not in ONS_SERIES:
        return {"error": f"Unknown series: {key}", "available": list(ONS_SERIES)}
    uri, label = ONS_SERIES[key]
    try:
        r = session.get(f"{ONS_SITE}{uri}/data", timeout=30,
                        headers={"User-Agent": "Mozilla/5.0 (finterm economics)"})
        r.raise_for_status()
        raw = r.json()
    except requests.exceptions.RequestException as e:
        return {"error": f"Request failed: {e}"}
    except ValueError as e:
        return {"error": f"JSON decode error: {e}"}

    # Use the finest frequency the series has; never mix frequencies.
    obs = raw.get("months") or raw.get("quarters") or raw.get("years") or []
    data = []
    for o in obs:
        date = _iso_period(o.get("date", ""))
        try:
            value = float(o.get("value"))
        except (TypeError, ValueError):
            continue  # blank / non-numeric = unavailable, skip
        if not date or (start_year and date[:4] < str(start_year)):
            continue
        data.append({"date": date, "value": value})
    data.sort(key=lambda x: x["date"])
    desc = raw.get("description", {})
    return {
        "success": True,
        "series": desc.get("cdid", key),
        "label": label,
        "title": desc.get("title"),
        "unit": desc.get("unit"),
        "release_date": desc.get("releaseDate"),
        "count": len(data),
        "data": data,
    }


def main(args=None):
    if args is None:
        args = sys.argv[1:]
    if not args:
        print(json.dumps({"error": "No command provided"}))
        return
    command = args[0]
    result = {"error": f"Unknown command: {command}"}
    if command == "dataset":
        dataset_id = args[1] if len(args) > 1 else "mm23"
        result = get_dataset(dataset_id)
    elif command == "timeseries":
        dataset_id = args[1] if len(args) > 1 else "qna"
        timeseries_id = args[2] if len(args) > 2 else "ABMI"
        start_year = args[3] if len(args) > 3 else None
        end_year = args[4] if len(args) > 4 else None
        result = get_timeseries(dataset_id, timeseries_id, start_year, end_year)
    elif command in ONS_SERIES:
        start_year = args[1] if len(args) > 1 and args[1].isdigit() else None
        result = get_series(command, start_year)
    elif command == "search":
        query = args[1] if len(args) > 1 else "GDP"
        result = search(query)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
