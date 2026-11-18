"""
US Treasury FiscalData API Fetcher
US Treasury fiscal data: debt to the penny, interest rates, exchange rates,
and account statements via the FiscalData Treasury API.
"""
import sys
import json
import os
import requests
from typing import Dict, Any, Optional, List

API_KEY = os.environ.get('TREASURY_API_KEY', '')
BASE_URL = "https://api.fiscaldata.treasury.gov/services/api/fiscal_service"

session = requests.Session()
adapter = requests.adapters.HTTPAdapter(pool_connections=10, pool_maxsize=10, max_retries=3)
session.mount('https://', adapter)
session.mount('http://', adapter)


def _make_request(endpoint: str, params: Dict = None) -> Any:
    url = f"{BASE_URL}/{endpoint}" if not endpoint.startswith('http') else endpoint
    if params is None:
        params = {}
    params["format"] = "json"
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


def get_debt_to_penny(start_date: str = None, end_date: str = None) -> Any:
    params = {"fields": "record_date,tot_pub_debt_out_amt,intragov_hold_amt,debt_held_public_amt", "sort": "-record_date", "page[size]": "365"}
    filters = []
    if start_date:
        filters.append(f"record_date:gte:{start_date}")
    if end_date:
        filters.append(f"record_date:lte:{end_date}")
    if filters:
        params["filters"] = ",".join(filters)
    return _make_request("v2/accounting/od/debt_to_penny", params)


def get_interest_rates(security_type: str = "Treasury Bills", start_date: str = None, end_date: str = None) -> Any:
    params = {
        "fields": "record_date,security_desc,avg_interest_rate_amt,src_line_nbr",
        "sort": "-record_date",
        "page[size]": "500",
        "filters": f"security_desc:eq:{security_type}",
    }
    if start_date:
        params["filters"] += f",record_date:gte:{start_date}"
    if end_date:
        params["filters"] += f",record_date:lte:{end_date}"
    return _make_request("v2/accounting/od/avg_interest_rates", params)


def get_exchange_rates(country_currency: str = None, start_date: str = None, end_date: str = None) -> Any:
    params = {
        "fields": "record_date,country,currency,exchange_rate,effective_date",
        "sort": "-record_date",
        "page[size]": "500",
    }
    filters = []
    if country_currency:
        filters.append(f"country_currency_desc:eq:{country_currency}")
    if start_date:
        filters.append(f"record_date:gte:{start_date}")
    if end_date:
        filters.append(f"record_date:lte:{end_date}")
    if filters:
        params["filters"] = ",".join(filters)
    return _make_request("v1/accounting/od/rates_of_exchange", params)


PAR_CURVE_URL = (
    "https://home.treasury.gov/resource-center/data-chart-center/interest-rates/"
    "daily-treasury-rates.csv/{year}/all?type=daily_treasury_yield_curve"
    "&field_tdr_date_value={year}&page&_format=csv"
)
# CSV column -> output key. Only the tenors the Markets WORLD board reads.
_PAR_TENORS = {"2 Yr": "y2", "5 Yr": "y5", "10 Yr": "y10", "30 Yr": "y30"}


def parse_par_curve_csv(text: str) -> List[Dict[str, Any]]:
    """Rows of the Treasury daily par yield curve CSV, newest first.

    A blank or non-numeric cell is None (absent), never 0.0 — a missing
    tenor must not read as a 0% yield downstream.
    """
    import csv
    import io
    from datetime import datetime as _dt

    rows: List[Dict[str, Any]] = []
    reader = csv.DictReader(io.StringIO(text))
    for rec in reader:
        raw_date = (rec.get("Date") or "").strip()
        try:
            day = _dt.strptime(raw_date, "%m/%d/%Y").date()
        except ValueError:
            continue
        row: Dict[str, Any] = {"date": day.isoformat()}
        for col, key in _PAR_TENORS.items():
            cell = (rec.get(col) or "").strip()
            try:
                val = float(cell)
                row[key] = val if val > 0 else None
            except ValueError:
                row[key] = None
        rows.append(row)
    rows.sort(key=lambda r: r["date"], reverse=True)
    return rows


def get_par_curve(limit: int = 5) -> Dict[str, Any]:
    """Latest rows of the daily par yield curve (end of day, keyless).

    Reads the current year's file, and the previous year's too when the
    current one has fewer than two rows (early January), so a day-over-day
    change is always available. A refused/failed fetch is {"error": ...}.
    """
    from datetime import date as _date

    year = _date.today().year
    rows: List[Dict[str, Any]] = []
    failure = None
    for y in (year, year - 1):
        try:
            resp = session.get(PAR_CURVE_URL.format(year=y), timeout=20)
            resp.raise_for_status()
        except requests.exceptions.RequestException as e:
            # Early January the new year's file may not exist yet: last
            # year's still answers. A failure is reported only if nothing does.
            failure = failure or f"Treasury par curve request failed: {e}"
            continue
        rows.extend(parse_par_curve_csv(resp.text))
        if len(rows) >= 2:
            break
    if not rows:
        return {"error": failure or "Treasury par curve returned no rows"}
    rows.sort(key=lambda r: r["date"], reverse=True)
    return {"source": "US Treasury daily par yield curve", "rows": rows[:limit]}


def main(args=None):
    if args is None:
        args = sys.argv[1:]
    if not args:
        print(json.dumps({"error": "No command provided"}))
        return
    command = args[0]
    result = {"error": f"Unknown command: {command}"}

    if command == "debt":
        start_date = args[1] if len(args) > 1 else None
        end_date = args[2] if len(args) > 2 else None
        result = get_debt_to_penny(start_date, end_date)
    elif command == "interest_rates":
        security_type = args[1] if len(args) > 1 else "Treasury Bills"
        start_date = args[2] if len(args) > 2 else None
        end_date = args[3] if len(args) > 3 else None
        result = get_interest_rates(security_type, start_date, end_date)
    elif command == "exchange_rates":
        country_currency = args[1] if len(args) > 1 else None
        start_date = args[2] if len(args) > 2 else None
        end_date = args[3] if len(args) > 3 else None
        result = get_exchange_rates(country_currency, start_date, end_date)
    elif command == "par_curve":
        result = get_par_curve()

    print(json.dumps(result))


if __name__ == "__main__":
    main()
