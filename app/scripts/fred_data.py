"""
FRED Data Fetcher
Fetches economic data from Federal Reserve Economic Data (FRED)
Returns JSON output for Qt/C++ integration
Based on OpenBB FRED provider

Optimized with concurrent fetching for multiple series

The app's ONE FRED reader. With FRED_API_KEY set, series come from the keyed
API (with metadata). Without a key, `series` and `multiple` fall back to
FRED's public graph download (fredgraph.csv, no key) and answer in the SAME
shape — observations [{date, value}], metadata "N/A". A frequency or unit
transform still needs the key (the keyed API computes those); search and
categories have no keyless source.
"""

import sys
import json
import os
import re
import requests
from datetime import datetime
from typing import Optional, List, Dict, Any
from concurrent.futures import ThreadPoolExecutor, as_completed

FRED_API_BASE = "https://api.stlouisfed.org/fred"
FRED_API_KEY = os.environ.get('FRED_API_KEY', '')  # API key from environment variable

# Connection pool for better performance
session = requests.Session()
adapter = requests.adapters.HTTPAdapter(pool_connections=10, pool_maxsize=10, max_retries=3)
session.mount('https://', adapter)


def make_fred_request(endpoint: str, params: Dict[str, Any]) -> Dict:
    """Make request to FRED API using connection pool"""
    if not FRED_API_KEY:
        return {
            "error": "FRED API key not configured. Set FRED_API_KEY environment variable.",
            "error_code": "MISSING_API_KEY",
        }

    params['api_key'] = FRED_API_KEY
    params['file_type'] = 'json'

    url = f"{FRED_API_BASE}/{endpoint}"
    try:
        response = session.get(url, params=params, timeout=15)
        if response.status_code in (401, 403):
            return {
                "error": "FRED rejected the API key (HTTP {}).".format(response.status_code),
                "error_code": "INVALID_API_KEY",
            }
        if response.status_code == 429:
            retry_after = response.headers.get("Retry-After", "")
            try:
                retry_seconds = int(retry_after)
            except (TypeError, ValueError):
                retry_seconds = 60
            return {
                "error": "FRED rate limit hit. Retry in ~{}s.".format(retry_seconds),
                "error_code": "RATE_LIMITED",
                "retry_after": retry_seconds,
            }
        response.raise_for_status()
        return response.json()
    except requests.exceptions.HTTPError as e:
        return {"error": str(e), "error_code": "HTTP_ERROR"}
    except requests.exceptions.Timeout:
        return {"error": "FRED request timed out.", "error_code": "TIMEOUT"}
    except requests.exceptions.RequestException as e:
        return {"error": str(e), "error_code": "REQUEST_FAILED"}


FRED_CSV = "https://fred.stlouisfed.org/graph/fredgraph.csv"
_FRED_ID = re.compile(r"^[A-Z0-9_]{2,30}$")


def is_fred_csv(text: str, series_id: str) -> bool:
    """A real download starts with the header naming the series; an error
    page or a throttle notice does not — it is a failure, not an empty series."""
    first = (text or "").lstrip().split("\n", 1)[0].strip()
    parts = first.split(",", 1)
    return len(parts) == 2 and parts[0].strip().lower() == "observation_date" and parts[1].strip() == series_id


def parse_fred_csv(text: str) -> List[Dict]:
    """fredgraph.csv rows → [{date, value}]; FRED's "." (no value) and blanks
    are skipped — absent, never 0."""
    out = []
    for line in (text or "").splitlines()[1:]:
        day, _, raw = line.strip().partition(",")
        raw = raw.strip()
        if not day or raw in ("", "."):
            continue
        try:
            datetime.strptime(day, "%Y-%m-%d")
            value = float(raw)
        except ValueError:
            continue
        if value != value or value in (float("inf"), float("-inf")):
            continue
        out.append({"date": day, "value": value})
    return out


def get_series_keyless(series_id: str, start_date: Optional[str] = None,
                       end_date: Optional[str] = None) -> Dict:
    """get_series without a key: FRED's public graph download, same shape."""
    if not _FRED_ID.match(series_id or ""):
        return {"error": f"Not a FRED series id: {series_id!r}", "error_code": "BAD_SERIES_ID",
                "series_id": series_id}
    params = {"id": series_id}
    if start_date:
        params["cosd"] = start_date
    if end_date:
        params["coed"] = end_date
    try:
        response = session.get(FRED_CSV, params=params, timeout=15)
        if response.status_code == 429:
            return {"error": "FRED rate limit hit.", "error_code": "RATE_LIMITED", "series_id": series_id}
        response.raise_for_status()
    except requests.exceptions.Timeout:
        return {"error": "FRED request timed out.", "error_code": "TIMEOUT", "series_id": series_id}
    except requests.exceptions.RequestException as e:
        return {"error": str(e), "error_code": "REQUEST_FAILED", "series_id": series_id}
    if not is_fred_csv(response.text, series_id):
        # An error page or a throttle notice: a failure, never an empty series.
        return {"error": f"FRED returned no data for {series_id}", "error_code": "NO_DATA",
                "series_id": series_id}
    return _series_answer(series_id, parse_fred_csv(response.text), {}, 'fredgraph')


def _numeric_observations(raw) -> List[Dict]:
    """The keyed API's observations ({"date", "value": "324.5" | "."}) →
    [{date, value: float}]; "." and unparseable values are absent, never 0."""
    out = []
    for obs in raw or []:
        if not isinstance(obs, dict):
            continue
        try:
            value = float(obs.get('value'))
        except (TypeError, ValueError):
            continue
        if value != value or value in (float("inf"), float("-inf")) or not obs.get('date'):
            continue
        out.append({'date': obs['date'], 'value': value})
    return out


def _series_answer(series_id: str, observations: List[Dict], info: Dict, source: str) -> Dict:
    """The ONE shape a series answer has, keyed or keyless, metadata or not:
    series_id always, numeric observations, "N/A" for metadata not read."""
    return {
        'series_id': series_id,
        'title': info.get('title', 'N/A'),
        'units': info.get('units', 'N/A'),
        'frequency': info.get('frequency', 'N/A'),
        'seasonal_adjustment': info.get('seasonal_adjustment', 'N/A'),
        'last_updated': info.get('last_updated', 'N/A'),
        'observations': observations,
        'observation_count': len(observations),
        'source': source,
    }


def get_series(series_id: str, start_date: Optional[str] = None, end_date: Optional[str] = None,
               frequency: Optional[str] = None, transform: Optional[str] = None) -> Dict:
    """
    Fetch FRED series data

    Args:
        series_id: FRED series ID (e.g., 'GDP', 'UNRATE', 'CPIAUCSL')
        start_date: Start date YYYY-MM-DD
        end_date: End date YYYY-MM-DD
        frequency: a=Annual, q=Quarterly, m=Monthly, w=Weekly, d=Daily
        transform: chg=Change, pch=Percent Change, log=Natural Log

    Without FRED_API_KEY the plain series comes from the public graph
    download (get_series_keyless); a frequency or transform needs the key.
    """
    if not FRED_API_KEY and not frequency and not transform:
        return get_series_keyless(series_id, start_date, end_date)
    try:
        # Get series observations
        params = {'series_id': series_id}
        if start_date:
            params['observation_start'] = start_date
        if end_date:
            params['observation_end'] = end_date
        if frequency:
            params['frequency'] = frequency
        if transform:
            params['units'] = transform

        obs_data = make_fred_request('series/observations', params)

        if 'error' in obs_data:
            return {**obs_data, 'series_id': series_id}

        # Metadata is a nicety: when its request fails the observations are
        # still the answer, in the same shape ("N/A" metadata).
        metadata = make_fred_request('series', {'series_id': series_id})
        seriess = metadata.get('seriess') if 'error' not in metadata else None
        series_info = seriess[0] if isinstance(seriess, list) and seriess and isinstance(seriess[0], dict) else {}

        return _series_answer(series_id, _numeric_observations(obs_data.get('observations')), series_info, 'fred_api')
    except Exception as e:
        return {"error": str(e), "series_id": series_id}


def search_series(search_text: str, limit: int = 10) -> Dict:
    """Search for FRED series"""
    try:
        params = {
            'search_text': search_text,
            'limit': limit,
            'order_by': 'popularity',
            'sort_order': 'desc'
        }

        result = make_fred_request('series/search', params)

        if 'error' in result:
            return result

        series_list = []
        for series in result.get('seriess', []):
            series_list.append({
                'id': series.get('id'),
                'title': series.get('title'),
                'frequency': series.get('frequency'),
                'units': series.get('units'),
                'seasonal_adjustment': series.get('seasonal_adjustment'),
                'last_updated': series.get('last_updated'),
                'popularity': series.get('popularity')
            })

        return {
            'search_text': search_text,
            'count': len(series_list),
            'series': series_list
        }
    except Exception as e:
        return {"error": str(e), "search_text": search_text}


def get_categories(category_id: int = 0) -> Dict:
    """Get FRED categories"""
    try:
        params = {'category_id': category_id}
        result = make_fred_request('category/children', params)

        if 'error' in result:
            return result

        categories = []
        for cat in result.get('categories', []):
            categories.append({
                'id': cat.get('id'),
                'name': cat.get('name'),
                'parent_id': cat.get('parent_id')
            })

        return {
            'category_id': category_id,
            'categories': categories
        }
    except Exception as e:
        return {"error": str(e), "category_id": category_id}


def get_multiple_series(series_ids: List[str], start_date: Optional[str] = None,
                        end_date: Optional[str] = None) -> List[Dict]:
    """Fetch multiple FRED series concurrently for better performance"""
    results = []

    # Use ThreadPoolExecutor for concurrent fetching
    # Limit to 5 concurrent requests to avoid rate limiting
    max_workers = min(5, len(series_ids))

    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        # Submit all tasks
        future_to_series = {
            executor.submit(get_series, series_id, start_date, end_date): series_id
            for series_id in series_ids
        }

        # Collect results as they complete
        results_dict = {}
        for future in as_completed(future_to_series):
            series_id = future_to_series[future]
            try:
                result = future.result()
                results_dict[series_id] = result
            except Exception as e:
                results_dict[series_id] = {"error": str(e), "series_id": series_id}

    # Return results in original order
    for series_id in series_ids:
        results.append(results_dict.get(series_id, {"error": "Unknown error", "series_id": series_id}))

    return results


def main(args=None):
    # Support both function call and CLI execution
    if args is None:
        args = sys.argv[1:]

    if len(args) < 1:
        print(json.dumps({"error": "Usage: python fred_data.py <command> <args>"}))
        sys.exit(1)

    command = args[0]

    if command == "series":
        if len(args) < 2:
            print(json.dumps({"error": "Usage: python fred_data.py series <series_id> [start_date] [end_date] [frequency] [transform]"}))
            sys.exit(1)

        series_id = args[1]
        start_date = args[2] if len(args) > 2 else None
        end_date = args[3] if len(args) > 3 else None
        frequency = args[4] if len(args) > 4 else None
        transform = args[5] if len(args) > 5 else None

        result = get_series(series_id, start_date, end_date, frequency, transform)
        print(json.dumps(result, indent=2))

    elif command == "multiple":
        if len(args) < 2:
            print(json.dumps({"error": "Usage: python fred_data.py multiple <series_id1> <series_id2> ... [start_date] [end_date]"}))
            sys.exit(1)

        # Find where dates start (if they exist)
        series_ids = []
        start_date = None
        end_date = None

        for i in range(1, len(args)):
            arg = args[i]
            if '-' in arg and len(arg) == 10:  # Date format YYYY-MM-DD
                if start_date is None:
                    start_date = arg
                else:
                    end_date = arg
            else:
                series_ids.append(arg)

        result = get_multiple_series(series_ids, start_date, end_date)
        print(json.dumps(result, indent=2))

    elif command == "search":
        if len(args) < 2:
            print(json.dumps({"error": "Usage: python fred_data.py search <search_text> [limit]"}))
            sys.exit(1)

        search_text = args[1]
        limit = int(args[2]) if len(args) > 2 else 10

        result = search_series(search_text, limit)
        print(json.dumps(result, indent=2))

    elif command == "categories":
        category_id = int(args[1]) if len(args) > 1 else 0
        result = get_categories(category_id)
        print(json.dumps(result, indent=2))

    else:
        print(json.dumps({"error": f"Unknown command: {command}"}))
        sys.exit(1)


if __name__ == "__main__":
    main()
