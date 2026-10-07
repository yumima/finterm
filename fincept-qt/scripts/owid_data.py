"""
Our World in Data Fetcher
CO2, energy, health, poverty, education, democracy data for all countries.
Uses OWID API and GitHub CSV data. No API key required.
"""
import sys
import json
import os
import requests
from typing import Dict, Any, Optional, List

BASE_URL = "https://api.ourworldindata.org/v1"
GITHUB_RAW = "https://raw.githubusercontent.com/owid/owid-datasets/master/datasets"
CATALOG_URL = "https://catalog.ourworldindata.org"

session = requests.Session()
adapter = requests.adapters.HTTPAdapter(pool_connections=10, pool_maxsize=10, max_retries=3)
session.mount('https://', adapter)
session.mount('http://', adapter)
session.headers.update({"Accept": "application/json"})


def _make_request(endpoint: str, params: Dict = None) -> Any:
    """Make HTTP request with error handling."""
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


def _fetch_owid_csv_api(dataset: str, country: str = None, columns: List[str] = None) -> Any:
    """Fetch OWID data via the indicators API."""
    params = {"datasetCode": dataset}
    if country:
        params["entityName"] = country
    data = _make_request("indicators", params=params)
    return data


def get_indicator(indicator_id: int) -> Any:
    """Get data for a specific OWID indicator by numeric ID."""
    data = _make_request(f"indicator/{indicator_id}.json")
    if isinstance(data, dict) and "error" not in data:
        return data
    return data


def search_indicators(query: str) -> Any:
    """Search OWID indicators by keyword."""
    params = {"query": query, "limit": 30}
    data = _make_request("search", params=params)
    if isinstance(data, dict) and "results" in data:
        return {"query": query, "results": data["results"][:30], "count": len(data["results"])}
    return data


def get_co2_data(country: str = "World") -> Any:
    """Get CO2 and greenhouse gas emissions data for a country.
    Uses Our World in Data CO2 dataset via GitHub.
    country: Country name as in OWID (e.g. 'United States', 'Germany', 'China', 'World').
    """
    url = "https://nyc3.digitaloceanspaces.com/owid-public/data/co2/owid-co2-data.json"
    try:
        response = session.get(url, timeout=30)
        response.raise_for_status()
        data = response.json()
        # Find country
        country_data = None
        for key, val in data.items():
            if key.lower() == country.lower():
                country_data = val
                break
        if country_data is None:
            # fuzzy search
            matches = [k for k in data.keys() if country.lower() in k.lower()]
            if matches:
                country_data = data[matches[0]]
                country = matches[0]
            else:
                return {"error": f"Country '{country}' not found", "available_sample": list(data.keys())[:20]}

        # Return last 50 years of data
        records = country_data.get("data", [])
        recent = records[-50:] if len(records) > 50 else records
        return {
            "country": country,
            "data": recent,
            "count": len(recent),
            "fields": list(recent[0].keys()) if recent else [],
        }
    except Exception as e:
        return {"error": f"Failed to fetch CO2 data: {str(e)}"}


def get_energy_data(country: str = "World") -> Any:
    """Get energy consumption and production data for a country.
    country: Country name as in OWID.
    """
    url = "https://nyc3.digitaloceanspaces.com/owid-public/data/energy/owid-energy-data.json"
    try:
        response = session.get(url, timeout=30)
        response.raise_for_status()
        data = response.json()
        country_data = None
        for key, val in data.items():
            if key.lower() == country.lower():
                country_data = val
                break
        if country_data is None:
            matches = [k for k in data.keys() if country.lower() in k.lower()]
            if matches:
                country_data = data[matches[0]]
                country = matches[0]
            else:
                return {"error": f"Country '{country}' not found", "available_sample": list(data.keys())[:20]}

        records = country_data.get("data", [])
        recent = records[-30:] if len(records) > 30 else records
        return {
            "country": country,
            "data": recent,
            "count": len(recent),
            "fields": list(recent[0].keys()) if recent else [],
        }
    except Exception as e:
        return {"error": f"Failed to fetch energy data: {str(e)}"}


def get_health_data(country: str = None) -> Any:
    """Get health indicators (life expectancy, child mortality, etc.) via OWID API."""
    # Life expectancy indicator
    params = {"entityName": country} if country else {}
    data = _make_request("indicators/life-expectancy", params=params)
    if isinstance(data, dict) and "error" not in data:
        return {"category": "health", "country": country, "indicator": "life_expectancy", "data": data}
    # Fallback: return indicator IDs for health metrics
    return {
        "category": "health",
        "note": "Use indicator command with numeric IDs for specific metrics",
        "common_health_indicators": {
            "life_expectancy": "Search 'life expectancy' via search command",
            "child_mortality": "Search 'child mortality' via search command",
            "maternal_mortality": "Search 'maternal mortality rate' via search command",
        }
    }


def get_poverty_data(country: str = None) -> Any:
    """Get poverty and inequality data via OWID API."""
    params = {}
    if country:
        params["entityName"] = country
    data = _make_request("indicators/share-of-population-in-extreme-poverty", params=params)
    if isinstance(data, dict) and "error" not in data:
        return {"category": "poverty", "country": country, "indicator": "extreme_poverty", "data": data}
    return {
        "category": "poverty",
        "note": "Use search command to find specific poverty indicators",
        "common_poverty_indicators": {
            "extreme_poverty": "Search 'extreme poverty' via search command",
            "gini_coefficient": "Search 'gini coefficient' via search command",
            "income_share": "Search 'income share' via search command",
        }
    }


def get_democracy_data(country: str = None) -> Any:
    """Get democracy and governance indices (V-Dem, Freedom House, Polity)."""
    params = {}
    if country:
        params["entityName"] = country
    data = _make_request("indicators/electoral-democracy", params=params)
    if isinstance(data, dict) and "error" not in data:
        return {"category": "democracy", "country": country, "data": data}
    return {
        "category": "democracy",
        "note": "Use search command to find democracy/governance indicators",
        "suggested_searches": ["v-dem", "democracy index", "freedom house", "polity"],
    }


# ── Grapher-chart series (single indicator, one country) ───────────────────
# command: (grapher slug, value column or prefix)
GRAPHER_SERIES = {
    "co2_per_capita":  ("co-emissions-per-capita", "emissions_total_per_capita"),
    "life_expectancy": ("life-expectancy", "life_expectancy_0"),
    "gdp_per_capita":  ("gdp-per-capita-worldbank", "ny_gdp_pcap_pp_kd"),
    "poverty":         ("share-of-population-in-extreme-poverty", "headcount_ratio"),
}


def _year_in_range(year, start, end) -> bool:
    try:
        y = int(year)
    except (TypeError, ValueError):
        return False
    if start and str(start).lstrip("-").isdigit() and y < int(start):
        return False
    if end and str(end).lstrip("-").isdigit() and y > int(end):
        return False
    return True


def get_grapher_series(command: str, country: str = "World", start: str = None, end: str = None) -> Any:
    """One OWID grapher indicator for one entity, as [{year, value}]."""
    import csv
    import io
    slug, column = GRAPHER_SERIES[command]
    base = f"https://ourworldindata.org/grapher/{slug}"
    headers = {"User-Agent": "Mozilla/5.0 (finterm economics)"}
    try:
        r = session.get(f"{base}.csv", params={"useColumnShortNames": "true"}, headers=headers, timeout=60)
        r.raise_for_status()
        text = r.text
    except requests.exceptions.RequestException as e:
        return {"error": f"Request failed: {e}"}

    reader = csv.DictReader(io.StringIO(text))
    fields = reader.fieldnames or []
    value_col = next((f for f in fields if f == column or f.startswith(column)), None)
    if value_col is None:
        return {"error": f"OWID chart '{slug}' no longer has column '{column}'"}

    rows = list(reader)
    entities = {row.get("entity", "") for row in rows}
    match = next((e for e in entities if e.lower() == country.lower()), None)
    if match is None:
        return {"error": f"Country '{country}' not found in OWID chart '{slug}'",
                "available_sample": sorted(entities)[:20]}

    data = []
    for row in rows:
        if row.get("entity") != match or not _year_in_range(row.get("year"), start, end):
            continue
        raw = row.get(value_col, "")
        if raw in ("", None):
            continue  # no observation that year
        try:
            data.append({"year": int(row["year"]), "value": float(raw)})
        except (TypeError, ValueError):
            continue
    data.sort(key=lambda x: x["year"])

    title, unit = slug, None
    try:
        meta = session.get(f"{base}.metadata.json", headers=headers, timeout=30).json()
        title = (meta.get("chart") or {}).get("title") or title
        col_meta = (meta.get("columns") or {})
        for cm in col_meta.values():
            if (cm.get("shortName") or "") == value_col or len(col_meta) == 1:
                unit = cm.get("unit")
                break
        else:
            unit = next(iter(col_meta.values()), {}).get("unit")
    except Exception:
        pass  # metadata is cosmetic; data above is complete without it

    return {
        "title": title + (f" ({unit})" if unit else ""),
        "country": match,
        "unit": unit,
        "count": len(data),
        "data": data,
    }


def main(args=None):
    if args is None:
        args = sys.argv[1:]
    if not args:
        print(json.dumps({"error": "No command provided. Available: co2, energy, co2_per_capita, life_expectancy, gdp_per_capita, poverty, health, democracy, indicator, search"}))
        return

    command = args[0]

    start = args[2] if len(args) > 2 else None
    end = args[3] if len(args) > 3 else None
    if command in GRAPHER_SERIES:
        result = get_grapher_series(command, args[1] if len(args) > 1 else "World", start, end)
    elif command in ("co2", "energy"):
        country = args[1] if len(args) > 1 else "World"
        result = get_co2_data(country) if command == "co2" else get_energy_data(country)
        # Honour the [start] [end] year range when given.
        if isinstance(result, dict) and isinstance(result.get("data"), list) and (start or end):
            result["data"] = [r for r in result["data"] if _year_in_range(r.get("year"), start, end)]
            result["count"] = len(result["data"])
    elif command == "health":
        country = args[1] if len(args) > 1 else None
        result = get_health_data(country)
    elif command == "democracy":
        country = args[1] if len(args) > 1 else None
        result = get_democracy_data(country)
    elif command == "indicator":
        if len(args) < 2:
            result = {"error": "Usage: indicator <indicator_id>"}
        else:
            try:
                result = get_indicator(int(args[1]))
            except ValueError:
                result = {"error": f"Indicator ID must be numeric, got: {args[1]}"}
    elif command == "search":
        if len(args) < 2:
            result = {"error": "Usage: search <query>"}
        else:
            result = search_indicators(args[1])
    else:
        result = {"error": f"Unknown command: {command}. Available: co2, energy, co2_per_capita, life_expectancy, gdp_per_capita, poverty, health, democracy, indicator, search"}

    print(json.dumps(result))


if __name__ == "__main__":
    main()
