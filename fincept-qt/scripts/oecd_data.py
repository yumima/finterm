"""
OECD Data Fetcher
Modular, fault-tolerant wrapper for OECD economic data using SDMX RESTful API
Sources: OECD SDMX API (https://sdmx.oecd.org/public/rest/)
Supports both SDMX API v1 and v2 endpoints with automatic fallback

API Documentation:
- Base URLs: https://sdmx.oecd.org/public/rest/ (v1) and https://sdmx.oecd.org/public/rest/v2/ (v2)
- Authentication: No API key required (public data)
- Rate limits: Standard web rate limiting applies
- Formats: Supports SDMX-JSON, SDMX-ML (XML), and SDMX-CSV formats

Each endpoint works independently with isolated error handling and automatic v1/v2 fallback.
Updated to use proper SDMX dataflow structure based on OECD API documentation.
"""

import sys
import json
import requests
import ssl
import urllib3
import pandas as pd
import numpy as np
from datetime import datetime, date, timedelta
from typing import Dict, Any, Optional, List, Union
from io import StringIO
from pathlib import Path
import os
import re
from defusedxml.ElementTree import fromstring

# OECD API Constants
BASE_URL = "https://sdmx.oecd.org/public/rest/"
SDMX_V1_BASE = "https://sdmx.oecd.org/public/rest/"
SDMX_V2_BASE = "https://sdmx.oecd.org/public/rest/v2/"

# Country mappings based on OpenBB constants
COUNTRY_TO_CODE_GDP = {
    "oecd": "OECD",
    "oecd_26": "OECD26",
    "oecd_europe": "OECDE",
    "g7": "G7",
    "g20": "G20",
    "euro_area": "EA20",
    "european_union_27": "EU27_2020",
    "european_union_15": "EU15",
    "nafta": "USMCA",
    "argentina": "ARG",
    "australia": "AUS",
    "austria": "AUT",
    "belgium": "BEL",
    "bulgaria": "BGR",
    "brazil": "BRA",
    "canada": "CAN",
    "chile": "CHL",
    "colombia": "COL",
    "costa_rica": "CRI",
    "croatia": "HRV",
    "czech_republic": "CZE",
    "denmark": "DNK",
    "estonia": "EST",
    "finland": "FIN",
    "france": "FRA",
    "germany": "DEU",
    "greece": "GRC",
    "hungary": "HUN",
    "iceland": "ISL",
    "india": "IND",
    "indonesia": "IDN",
    "ireland": "IRL",
    "israel": "ISR",
    "italy": "ITA",
    "japan": "JPN",
    "korea": "KOR",
    "latvia": "LVA",
    "lithuania": "LTU",
    "luxembourg": "LUX",
    "mexico": "MEX",
    "netherlands": "NLD",
    "new_zealand": "NZL",
    "norway": "NOR",
    "poland": "POL",
    "portugal": "PRT",
    "romania": "ROU",
    "russia": "RUS",
    "saudi_arabia": "SAU",
    "slovak_republic": "SVK",
    "slovenia": "SVN",
    "south_africa": "ZAF",
    "spain": "ESP",
    "sweden": "SWE",
    "switzerland": "CHE",
    "turkey": "TUR",
    "united_kingdom": "GBR",
    "united_states": "USA",
}

CODE_TO_COUNTRY_GDP = {v: k for k, v in COUNTRY_TO_CODE_GDP.items()}

COUNTRY_TO_CODE_CPI = {
    "G20": "G20",
    "G7": "G7",
    "argentina": "ARG",
    "australia": "AUS",
    "austria": "AUT",
    "belgium": "BEL",
    "brazil": "BRA",
    "canada": "CAN",
    "chile": "CHL",
    "china": "CHN",
    "colombia": "COL",
    "costa_rica": "CRI",
    "czech_republic": "CZE",
    "denmark": "DNK",
    "estonia": "EST",
    "euro_area_20": "EA20",
    "europe": "OECDE",
    "european_union_27": "EU27_2020",
    "finland": "FIN",
    "france": "FRA",
    "germany": "DEU",
    "greece": "GRC",
    "hungary": "HUN",
    "iceland": "ISL",
    "india": "IND",
    "indonesia": "IDN",
    "ireland": "IRL",
    "israel": "ISR",
    "italy": "ITA",
    "japan": "JPN",
    "korea": "KOR",
    "latvia": "LVA",
    "lithuania": "LTU",
    "luxembourg": "LUX",
    "mexico": "MEX",
    "netherlands": "NLD",
    "new_zealand": "NZL",
    "norway": "NOR",
    "oecd_total": "OECD",
    "poland": "POL",
    "portugal": "PRT",
    "russia": "RUS",
    "saudi_arabia": "SAU",
    "slovak_republic": "SVK",
    "slovenia": "SVN",
    "south_africa": "ZAF",
    "spain": "ESP",
    "sweden": "SWE",
    "switzerland": "CHE",
    "turkey": "TUR",
    "united_kingdom": "GBR",
    "united_states": "USA",
}

CODE_TO_COUNTRY_CPI = {v: k for k, v in COUNTRY_TO_CODE_CPI.items()}

# Expenditure categories for CPI
EXPENDITURE_DICT = {
    "total": "_T",
    "food_non_alcoholic_beverages": "CP01",
    "alcoholic_beverages_tobacco_narcotics": "CP02",
    "clothing_footwear": "CP03",
    "housing_water_electricity_gas": "CP04",
    "furniture_household_equipment": "CP05",
    "health": "CP06",
    "transport": "CP07",
    "communication": "CP08",
    "recreation_culture": "CP09",
    "education": "CP10",
    "restaurants_hotels": "CP11",
    "miscellaneous_goods_services": "CP12",
    "energy": "CP045_0722",
    "goods": "GD",
    "housing": "CP041T043",
    "housing_excluding_rentals": "CP041T043X042",
    "all_non_food_non_energy": "_TXCP01_NRG",
    "services_less_housing": "SERVXCP041_0432",
    "services_less_house_excl_rentals": "SERVXCP041_043",
    "services": "SERV",
    "overall_excl_energy_food_alcohol_tobacco": "_TXNRG_01_02",
    "residuals": "CPRES",
    "fuels_lubricants_personal": "CP0722",
    "actual_rentals": "CP041",
    "imputed_rentals": "CP042",
    "maintenance_repair_dwelling": "CP043",
    "water_supply_other_services": "CP044",
    "electricity_gas_other_fuels": "CP045",
}

# ISO 3166 alpha-2 → OECD REF_AREA (alpha-3), so callers such as the
# Economics panel can pass "US"/"DE" as well as "united_states"/"USA".
ISO2_TO_ISO3 = {
    "US": "USA", "DE": "DEU", "JP": "JPN", "FR": "FRA", "GB": "GBR", "UK": "GBR",
    "CA": "CAN", "AU": "AUS", "KR": "KOR", "IT": "ITA", "ES": "ESP", "CN": "CHN",
    "IN": "IND", "BR": "BRA", "MX": "MEX", "NL": "NLD", "CH": "CHE", "SE": "SWE",
    "NO": "NOR", "DK": "DNK", "BE": "BEL", "AT": "AUT", "IE": "IRL", "PL": "POL",
    "PT": "PRT", "NZ": "NZL", "TR": "TUR", "ZA": "ZAF", "FI": "FIN", "GR": "GRC",
}

_FREQ_ALIASES = {
    "a": "annual", "annual": "annual", "y": "annual", "yearly": "annual",
    "q": "quarter", "quarter": "quarter", "quarterly": "quarter",
    "m": "monthly", "monthly": "monthly", "month": "monthly",
}


def _norm_freq(value: Optional[str], default: str) -> str:
    """Accept SDMX codes (A/Q/M) as well as annual/quarter/monthly."""
    if value is None:
        return default
    return _FREQ_ALIASES.get(str(value).strip().lower(), str(value))


class OECDError:
    """Error handling wrapper for OECD API responses"""
    def __init__(self, endpoint: str, error: str, status_code: Optional[int] = None):
        self.endpoint = endpoint
        self.error = error
        self.status_code = status_code
        self.timestamp = int(datetime.now().timestamp())

    def to_dict(self) -> Dict[str, Any]:
        return {
            "success": False,
            "error": self.error,
            "endpoint": self.endpoint,
            "status_code": self.status_code,
            "timestamp": self.timestamp
        }

class OECDWrapper:
    """Modular OECD API wrapper with fault tolerance"""

    def __init__(self, cache_dir: Optional[str] = None):
        self.cache_dir = cache_dir or os.path.join(os.path.expanduser("~"), ".oecd_cache")
        Path(self.cache_dir).mkdir(parents=True, exist_ok=True)
        self.session = self._get_legacy_session()

    def _get_legacy_session(self):
        """Create a custom session for OECD compatibility"""
        ctx = ssl.create_default_context(ssl.Purpose.SERVER_AUTH)
        ctx.options |= 0x4  # OP_LEGACY_SERVER_CONNECT
        session = requests.Session()
        session.mount("https://", self.CustomHttpAdapter(ctx))
        return session

    class CustomHttpAdapter(requests.adapters.HTTPAdapter):
        """Transport adapter that allows us to use custom ssl_context."""
        def __init__(self, ssl_context=None, **kwargs):
            self.ssl_context = ssl_context
            super().__init__(**kwargs)

        def init_poolmanager(self, connections, maxsize, block=False):
            self.poolmanager = urllib3.poolmanager.PoolManager(
                num_pools=connections,
                maxsize=maxsize,
                block=block,
                ssl_context=self.ssl_context,
            )

    def _make_request(self, url: str, method: str = 'GET', params: Optional[Dict] = None,
                     format_type: str = 'csv', api_version: str = 'v1') -> Dict[str, Any]:
        """Make HTTP request with comprehensive error handling"""
        try:
            # Set proper SDMX Accept headers
            headers = {
                'Accept-Language': 'en',
                'Accept-Encoding': 'gzip, deflate'
            }

            if format_type == 'json':
                if api_version == 'v2':
                    headers['Accept'] = 'application/vnd.sdmx.data+json; charset=utf-8; version=2'
                else:
                    headers['Accept'] = 'application/vnd.sdmx.data+json; charset=utf-8; version=1.0'
            elif format_type == 'xml':
                headers['Accept'] = 'application/vnd.sdmx.structurespecificdata+xml; charset=utf-8; version=2.1'
            elif format_type == 'csv':
                headers['Accept'] = 'application/vnd.sdmx.data+csv; charset=utf-8'
            else:
                # Default to CSV with format parameter
                headers['Accept'] = 'application/vnd.sdmx.data+csv; charset=utf-8'
                if params is None:
                    params = {}
                params['format'] = 'csvfile'

            response = self.session.request(method=method, url=url, params=params, headers=headers, timeout=30)
            response.raise_for_status()

            # Check if response is XML, JSON or CSV
            content_type = response.headers.get('content-type', '').lower()

            if 'json' in content_type or format_type == 'json':
                try:
                    return {"success": True, "data": response.text, "format": "json"}
                except Exception as e:
                    return {"error": f"JSON parsing error: {str(e)}", "json_error": True}
            elif 'xml' in content_type or format_type == 'xml':
                try:
                    return {"success": True, "data": response.text, "format": "xml"}
                except Exception as e:
                    return {"error": f"XML parsing error: {str(e)}", "xml_error": True}
            else:
                try:
                    return {"success": True, "data": response.text, "format": "csv"}
                except Exception as e:
                    return {"error": f"Response parsing error: {str(e)}", "response_error": True}

        except requests.exceptions.Timeout:
            return {"error": "Request timeout", "timeout": True, "status_code": None}
        except requests.exceptions.ConnectionError:
            return {"error": "Connection error", "connection_error": True, "status_code": None}
        except requests.exceptions.HTTPError as e:
            response = locals().get('response')
            if response is None:
                return {"error": f"HTTP error: {e}", "http_error": True, "status_code": None}
            if response.status_code == 404:
                return {"error": "Data not found", "not_found": True, "status_code": response.status_code}
            elif response.status_code == 429:
                return {"error": "Rate limit exceeded", "rate_limit_error": True, "status_code": response.status_code}
            else:
                return {"error": f"HTTP error: {e}", "http_error": True, "status_code": response.status_code}
        except requests.exceptions.RequestException as e:
            return {"error": f"Request error: {e}", "request_error": True, "status_code": None}
        except Exception as e:
            return {"error": f"Unexpected error: {e}", "general_error": True, "status_code": None}

    def _parse_xml_to_dataframe(self, xml_string: str) -> pd.DataFrame:
        """Parse the OECD XML and return a dataframe."""
        try:
            root = fromstring(xml_string)

            namespaces = {
                "message": "http://www.sdmx.org/resources/sdmxml/schemas/v2_1/message",
                "generic": "http://www.sdmx.org/resources/sdmxml/schemas/v2_1/data/generic",
            }

            data = []

            for series in root.findall(".//generic:Series", namespaces=namespaces):
                series_data = {}
                for value in series.findall(".//generic:Value", namespaces=namespaces):
                    series_data[value.get("id")] = value.get("value")
                for obs in series.findall("./generic:Obs", namespaces=namespaces):
                    obs_data = series_data.copy()
                    obs_data["TIME_PERIOD"] = obs.find("./generic:ObsDimension", namespaces=namespaces).get("value")
                    obs_data["VALUE"] = obs.find("./generic:ObsValue", namespaces=namespaces).get("value")
                    data.append(obs_data)

            return pd.DataFrame(data)

        except Exception as e:
            raise ValueError(f"Failed to parse XML: {str(e)}")

    def _oecd_date_to_python_date(self, input_date: Union[str, int]) -> date:
        """Date formatter helper."""
        input_date = str(input_date)
        if "Q" in input_date:
            return pd.to_datetime(input_date).to_period("Q").start_time.date()
        if len(input_date) == 4:
            return date(int(input_date), 1, 1)
        if len(input_date) == 7:
            return pd.to_datetime(input_date).to_period("M").start_time.date()
        raise ValueError("Date not in expected format")

    def _country_string(self, countries: str, country_mapping: Dict[str, str]) -> str:
        """Convert list of countries to OECD codes"""
        if countries == "all":
            return ""
        country_list = countries.split(",")
        return "+".join([country_mapping.get(country.lower(), ISO2_TO_ISO3.get(country.upper(), country))
                         for country in country_list])

    # The dataflow keys previously used here (DF_QNA 1.0, AES@DF_AES,
    # DSD_EO@DF_EO 1.0, VALUE column parsing) no longer exist on the OECD
    # SDMX service, so every call failed. The keys below were checked against
    # sdmx.oecd.org; each returns one series per country.

    def _series_response(self, endpoint: str, df: Any, description: str,
                         params: Dict[str, Any]) -> Dict[str, Any]:
        if isinstance(df, dict):
            return df
        df = df.rename(columns={"REF_AREA": "country", "TIME_PERIOD": "date", "OBS_VALUE": "value"})
        df = df.sort_values(by=["country", "date"])
        data = df[["country", "date", "value"]].replace({np.nan: None}).to_dict(orient="records")
        if not data:
            return OECDError(endpoint, 'No data found for the given parameters').to_dict()
        return {
            "success": True,
            "endpoint": endpoint,
            "description": description,
            "parameters": params,
            "total_records": len(data),
            "data": data,
            "timestamp": int(datetime.now().timestamp())
        }

    # ===== GDP REAL ENDPOINT =====

    def get_gdp_real(self, countries: str = "united_states",
                     frequency: str = "quarter",
                     start_date: Optional[str] = None,
                     end_date: Optional[str] = None) -> Dict[str, Any]:
        """Real GDP, chained-volume (reference year 2020), USD PPP millions,
        seasonally adjusted (quarterly values annualised) — DSD_NAMAIN1@DF_QNA."""
        try:
            if frequency not in ["quarter", "annual"]:
                return OECDError('gdp_real', f'Invalid frequency: {frequency}. Must be quarter or annual').to_dict()
            freq_code = "Q" if frequency == "quarter" else "A"
            country_codes = self._country_string(countries, COUNTRY_TO_CODE_GDP)
            start_p, end_p = self._period_bounds(frequency, start_date or "1990-01-01", end_date)
            # FREQ.ADJUSTMENT.REF_AREA.SECTOR.COUNTERPART_SECTOR.TRANSACTION.INSTR_ASSET.ACTIVITY.
            # EXPENDITURE.UNIT_MEASURE.PRICE_BASE.TRANSFORMATION.TABLE_IDENTIFIER
            url = (f"{SDMX_V1_BASE}data/OECD.SDD.NAD,DSD_NAMAIN1@DF_QNA,1.1/"
                   f"{freq_code}.Y.{country_codes}.S1..B1GQ._Z...USD_PPP.LR.LA.T0102")
            df = self._sdmx_series('gdp_real', url, start_p, end_p, [])
            return self._series_response(
                'gdp_real', df, "Real GDP, chained volume (2020 prices), USD PPP millions, SA, annualised",
                {"countries": countries, "frequency": frequency, "start_date": start_p, "end_date": end_p})
        except Exception as e:
            return OECDError('gdp_real', str(e)).to_dict()

    # ===== CONSUMER PRICE INDEX ENDPOINT =====

    def get_consumer_price_index(self, countries: str = "united_states",
                                 expenditure: str = "total",
                                 frequency: str = "monthly",
                                 units: str = "index",
                                 harmonized: bool = False,
                                 start_date: Optional[str] = None,
                                 end_date: Optional[str] = None) -> Dict[str, Any]:
        """CPI (index level, YoY % or MoM %) — DSD_PRICES@DF_PRICES_ALL."""
        try:
            if frequency not in ["monthly", "quarter", "annual"]:
                return OECDError('cpi', f'Invalid frequency: {frequency}').to_dict()
            if units not in ["index", "yoy", "mom"]:
                return OECDError('cpi', f'Invalid units: {units}').to_dict()
            exp_code = EXPENDITURE_DICT.get(expenditure, expenditure)
            freq_code = {"monthly": "M", "quarter": "Q", "annual": "A"}[frequency]
            unit_code, transform = {"index": ("IX", "_Z"), "yoy": ("PA", "GY"), "mom": ("PA", "G1")}[units]
            methodology = "HICP" if harmonized else "N"
            country_codes = self._country_string(countries, COUNTRY_TO_CODE_GDP)
            start_p, end_p = self._period_bounds(frequency, start_date or "1990-01-01", end_date)
            # REF_AREA.FREQ.METHODOLOGY.MEASURE.UNIT_MEASURE.EXPENDITURE.ADJUSTMENT.TRANSFORMATION
            url = (f"{SDMX_V1_BASE}data/OECD.SDD.TPS,DSD_PRICES@DF_PRICES_ALL,1.0/"
                   f"{country_codes}.{freq_code}.{methodology}.CPI.{unit_code}.{exp_code}.N.{transform}")
            df = self._sdmx_series('cpi', url, start_p, end_p, [])
            label = {"index": "CPI index level", "yoy": "CPI inflation, % year-on-year",
                     "mom": "CPI inflation, % month-on-month"}[units]
            return self._series_response(
                'cpi', df, f"{label} ({'HICP' if harmonized else 'national'}, {expenditure}, NSA)",
                {"countries": countries, "expenditure": expenditure, "frequency": frequency,
                 "units": units, "start_date": start_p, "end_date": end_p})
        except Exception as e:
            return OECDError('cpi', str(e)).to_dict()

    # ===== GDP FORECAST ENDPOINT =====

    def get_gdp_forecast(self, countries: str = "united_states",
                          start_date: Optional[str] = None,
                          end_date: Optional[str] = None) -> Dict[str, Any]:
        """Real GDP growth, % y/y, from the latest OECD Economic Outlook
        (history plus the Outlook's projection years) — DSD_EO@DF_EO."""
        try:
            current_year = date.today().year
            start_p = (start_date or f"{current_year - 5}")[:4]
            end_p = (end_date or f"{current_year + 2}")[:4]
            country_codes = self._country_string(countries, COUNTRY_TO_CODE_GDP)
            url = f"{SDMX_V1_BASE}data/OECD.ECO.MAD,DSD_EO@DF_EO,/{country_codes}.GDPV_ANNPCT.A"
            df = self._sdmx_series('gdp_forecast', url, start_p, end_p, [])
            return self._series_response(
                'gdp_forecast', df,
                "Real GDP growth, % y/y — latest OECD Economic Outlook (recent years are projections)",
                {"countries": countries, "start_date": start_p, "end_date": end_p})
        except Exception as e:
            return OECDError('gdp_forecast', str(e)).to_dict()

    # ===== UNEMPLOYMENT ENDPOINT =====

    def get_unemployment(self, countries: str = "united_states",
                        frequency: str = "monthly",
                        start_date: Optional[str] = None,
                        end_date: Optional[str] = None) -> Dict[str, Any]:
        """Harmonised unemployment rate, 15+, SA, % of labour force — DSD_LFS@DF_IALFS_UNE_M."""
        try:
            if frequency not in ["quarter", "annual", "monthly"]:
                return OECDError('unemployment', f'Invalid frequency: {frequency}').to_dict()
            freq_code = {"monthly": "M", "quarter": "Q", "annual": "A"}[frequency]
            country_codes = self._country_string(countries, COUNTRY_TO_CODE_GDP)
            start_p, end_p = self._period_bounds(frequency, start_date, end_date)
            # REF_AREA.MEASURE.UNIT_MEASURE.TRANSFORMATION.ADJUSTMENT.SEX.AGE.ACTIVITY.FREQ
            url = (f"{SDMX_V1_BASE}data/OECD.SDD.TPS,DSD_LFS@DF_IALFS_UNE_M,1.0/"
                   f"{country_codes}..._Z.Y._T.Y_GE15..{freq_code}")
            df = self._sdmx_series('unemployment', url, start_p, end_p, [])
            return self._series_response(
                'unemployment', df, "Unemployment rate, 15+, SA, % of labour force",
                {"countries": countries, "frequency": frequency, "start_date": start_p, "end_date": end_p})
        except Exception as e:
            return OECDError('unemployment', str(e)).to_dict()

    def get_economic_summary(self, country: str = "united_states",
                            start_date: Optional[str] = None,
                            end_date: Optional[str] = None) -> Dict[str, Any]:
        """Get comprehensive economic summary for a country"""
        result = {
            "success": True,
            "country": country,
            "start_date": start_date,
            "end_date": end_date,
            "timestamp": int(datetime.now().timestamp()),
            "endpoints": {},
            "failed_endpoints": []
        }

        # Define endpoints to try
        endpoints = [
            ('gdp_real', lambda: self.get_gdp_real(countries=country, start_date=start_date, end_date=end_date)),
            ('cpi', lambda: self.get_consumer_price_index(countries=country, start_date=start_date, end_date=end_date)),
            ('gdp_forecast', lambda: self.get_gdp_forecast(countries=country, start_date=start_date, end_date=end_date)),
            ('unemployment', lambda: self.get_unemployment(countries=country, start_date=start_date, end_date=end_date))
        ]

        overall_success = False

        for endpoint_name, endpoint_func in endpoints:
            try:
                endpoint_result = endpoint_func()
                result["endpoints"][endpoint_name] = endpoint_result

                if endpoint_result.get("success"):
                    overall_success = True
                else:
                    result["failed_endpoints"].append({
                        "endpoint": endpoint_name,
                        "error": endpoint_result.get("error", "Unknown error")
                    })

            except Exception as e:
                result["failed_endpoints"].append({
                    "endpoint": endpoint_name,
                    "error": str(e)
                })

        result["success"] = overall_success
        return result

    def get_country_list(self) -> Dict[str, Any]:
        """Get list of available countries"""
        try:
            return {
                "success": True,
                "endpoint": "country_list",
                "available_countries": {
                    "gdp": list(COUNTRY_TO_CODE_GDP.keys()),
                    "cpi": list(COUNTRY_TO_CODE_CPI.keys())
                },
                "country_codes": {
                    "gdp": COUNTRY_TO_CODE_GDP,
                    "cpi": COUNTRY_TO_CODE_CPI
                },
                "expenditure_categories": list(EXPENDITURE_DICT.keys()),
                "timestamp": int(datetime.now().timestamp())
            }
        except Exception as e:
            return OECDError('country_list', str(e)).to_dict()

    # ===== ADDITIONAL OECD DATA ENDPOINTS =====

    def _sdmx_series(self, endpoint: str, url: str, start_period: str, end_period: str,
                     group_cols: List[str]) -> Any:
        """Fetch an SDMX v1 CSV and return a DataFrame with REF_AREA, TIME_PERIOD,
        OBS_VALUE plus group_cols, or an OECDError dict."""
        params = {
            "startPeriod": start_period,
            "endPeriod": end_period,
            "dimensionAtObservation": "AllDimensions",
            "format": "csvfile",
        }
        result = self._make_request(url, format_type='csvfile', api_version='v1', params=params)
        if "error" in result:
            return OECDError(endpoint, result['error'], result.get('status_code')).to_dict()
        text = result.get('data') or ''
        if not text.strip() or text.strip() == "NoResultsFound":
            return OECDError(endpoint, 'No data found for the given parameters').to_dict()
        try:
            df = pd.read_csv(StringIO(text))
        except Exception as e:
            return OECDError(endpoint, f'Failed to parse data: {e}').to_dict()
        needed = ["REF_AREA", "TIME_PERIOD", "OBS_VALUE"] + group_cols
        if any(c not in df.columns for c in needed):
            return OECDError(endpoint, 'Unexpected response layout').to_dict()
        df = df[needed].copy()
        df["OBS_VALUE"] = pd.to_numeric(df["OBS_VALUE"], errors="coerce")
        return df.dropna(subset=["OBS_VALUE"])

    @staticmethod
    def _period_bounds(frequency: str, start_date: Optional[str], end_date: Optional[str]):
        start = start_date or "2000-01-01"
        end = end_date or f"{date.today().year}-12-31"
        if frequency == "annual":
            return start[:4], end[:4]
        if frequency == "quarter":
            def q(d):
                return f"{d[:4]}-Q{(int(d[5:7]) - 1) // 3 + 1}"
            return q(start), q(end)
        return start[:7], end[:7]

    def get_interest_rates(self, countries: str = "united_states",
                          frequency: str = "monthly",
                          start_date: Optional[str] = None,
                          end_date: Optional[str] = None) -> Dict[str, Any]:
        """Short-term (3-month) interest rates, % p.a. — DSD_STES@DF_FINMARK."""
        try:
            if frequency not in ["monthly", "quarter", "annual"]:
                return OECDError('interest_rates', f'Invalid frequency: {frequency}').to_dict()
            freq_code = {"monthly": "M", "quarter": "Q", "annual": "A"}[frequency]
            country_codes = self._country_string(countries, COUNTRY_TO_CODE_GDP)
            start_p, end_p = self._period_bounds(frequency, start_date, end_date)
            # REF_AREA.FREQ.MEASURE.UNIT_MEASURE.ACTIVITY.ADJUSTMENT.TRANSFORMATION.TIME_HORIZ.METHODOLOGY
            url = (f"{SDMX_V1_BASE}data/OECD.SDD.STES,DSD_STES@DF_FINMARK,4.0/"
                   f"{country_codes}.{freq_code}.IR3TIB.PA.....")
            df = self._sdmx_series('interest_rates', url, start_p, end_p, [])
            if isinstance(df, dict):
                return df
            df = df.rename(columns={"REF_AREA": "country", "TIME_PERIOD": "date", "OBS_VALUE": "value"})
            df = df.sort_values(by=["country", "date"])
            data = df.replace({np.nan: None}).to_dict(orient="records")
            return {
                "success": True,
                "endpoint": "interest_rates",
                "description": "Short-term interest rate (3-month), % per annum",
                "parameters": {"countries": countries, "frequency": frequency,
                               "start_date": start_p, "end_date": end_p},
                "total_records": len(data),
                "data": data,
                "timestamp": int(datetime.now().timestamp())
            }
        except Exception as e:
            return OECDError('interest_rates', str(e)).to_dict()

    def get_trade_balance(self, countries: str = "united_states",
                         frequency: str = "quarter",
                         start_date: Optional[str] = None,
                         end_date: Optional[str] = None) -> Dict[str, Any]:
        """Goods & services balance (BPM6: goods balance + services balance),
        seasonally adjusted, USD millions — DSD_BOP@DF_BOP."""
        try:
            if frequency not in ["quarter", "annual"]:
                return OECDError('trade_balance', f'Invalid frequency: {frequency}. BoP is quarter or annual').to_dict()
            freq_code = {"quarter": "Q", "annual": "A"}[frequency]
            country_codes = self._country_string(countries, COUNTRY_TO_CODE_GDP)
            start_p, end_p = self._period_bounds(frequency, start_date, end_date)
            # REF_AREA.COUNTERPART_AREA.MEASURE.ACCOUNTING_ENTRY.FS_ENTRY.FREQ.UNIT_MEASURE.ADJUSTMENT
            url = (f"{SDMX_V1_BASE}data/OECD.SDD.TPS,DSD_BOP@DF_BOP,1.0/"
                   f"{country_codes}.WXD.G+S.B.T.{freq_code}.USD_EXC.Y")
            df = self._sdmx_series('trade_balance', url, start_p, end_p, ["MEASURE"])
            if isinstance(df, dict):
                return df
            wide = df.pivot_table(index=["REF_AREA", "TIME_PERIOD"], columns="MEASURE",
                                  values="OBS_VALUE", aggfunc="first").reset_index()
            # Only periods where both legs are published; never half a balance.
            if "G" not in wide.columns or "S" not in wide.columns:
                return OECDError('trade_balance', 'Goods or services balance missing from response').to_dict()
            wide = wide.dropna(subset=["G", "S"])
            data = [{"country": r.REF_AREA, "date": r.TIME_PERIOD,
                     "value": float(r.G) + float(r.S),
                     "goods_balance": float(r.G), "services_balance": float(r.S)}
                    for r in wide.sort_values(by=["REF_AREA", "TIME_PERIOD"]).itertuples()]
            return {
                "success": True,
                "endpoint": "trade_balance",
                "description": "Goods & services balance (BoP), SA, USD millions",
                "parameters": {"countries": countries, "frequency": frequency,
                               "start_date": start_p, "end_date": end_p},
                "total_records": len(data),
                "data": data,
                "timestamp": int(datetime.now().timestamp())
            }
        except Exception as e:
            return OECDError('trade_balance', str(e)).to_dict()

# ===== CLI INTERFACE =====

def main(args=None):
    
    if args is None:
        args = sys.argv[1:]
    if len(args) + 1 < 2:
        print(json.dumps({
            "success": False,
            "error": "Usage: python oecd_data.py <command> <args>",
            "available_commands": [
                "gdp_real [countries] [frequency] [start_date] [end_date]",
                "cpi [countries] [expenditure] [frequency] [units] [harmonized] [start_date] [end_date]",
                "gdp_forecast [countries] [start_date] [end_date]",
                "unemployment [countries] [frequency] [start_date] [end_date]",
                "interest_rates [countries] [frequency] [start_date] [end_date]",
                "trade_balance [countries] [frequency] [start_date] [end_date]",
                "economic_summary [country] [start_date] [end_date]",
                "country_list"
            ],
            "examples": [
                "python oecd_data.py gdp_real united_states quarter 2020-01-01 2024-12-31",
                "python oecd_data.py cpi united_states total monthly index false 2020-01-01 2024-12-31",
                "python oecd_data.py unemployment united_states quarter 2020-01-01 2024-12-31",
                "python oecd_data.py country_list"
            ],
            "notes": [
                "No API key required - OECD data is publicly available",
                "Countries: Use country names like 'united_states', 'germany', 'japan' or ISO codes",
                "Frequency: 'monthly', 'quarter', 'annual' (varies by endpoint)",
                "Date format: YYYY-MM-DD",
                "The API supports both SDMX v1 and v2 endpoints with automatic fallback"
            ]
        }))
        sys.exit(1)

    command = args[0]
    wrapper = OECDWrapper()

    try:
        if command == "gdp_real":
            countries = args[1] if len(args) + 1 > 2 else "united_states"
            frequency = _norm_freq(args[2] if len(args) + 1 > 3 else None, "quarter")
            start_date = args[3] if len(args) + 1 > 4 else None
            end_date = args[4] if len(args) + 1 > 5 else None

            result = wrapper.get_gdp_real(
                countries=countries,
                frequency=frequency,
                start_date=start_date,
                end_date=end_date
            )
            print(json.dumps(result, indent=2))

        elif command == "cpi":
            countries = args[1] if len(args) + 1 > 2 else "united_states"
            expenditure = args[2] if len(args) + 1 > 3 else "total"
            frequency = _norm_freq(args[3] if len(args) + 1 > 4 else None, "monthly")
            units = args[4] if len(args) + 1 > 5 else "index"
            harmonized = sys.argv[6].lower() == "true" if len(args) + 1 > 6 else False
            start_date = sys.argv[7] if len(args) + 1 > 7 else None
            end_date = sys.argv[8] if len(args) + 1 > 8 else None

            result = wrapper.get_consumer_price_index(
                countries=countries,
                expenditure=expenditure,
                frequency=frequency,
                units=units,
                harmonized=harmonized,
                start_date=start_date,
                end_date=end_date
            )
            print(json.dumps(result, indent=2))

        elif command == "gdp_forecast":
            countries = args[1] if len(args) + 1 > 2 else "united_states"
            start_date = args[2] if len(args) + 1 > 3 else None
            end_date = args[3] if len(args) + 1 > 4 else None

            result = wrapper.get_gdp_forecast(
                countries=countries,
                start_date=start_date,
                end_date=end_date
            )
            print(json.dumps(result, indent=2))

        elif command == "unemployment":
            countries = args[1] if len(args) + 1 > 2 else "united_states"
            frequency = _norm_freq(args[2] if len(args) + 1 > 3 else None, "quarter")
            start_date = args[3] if len(args) + 1 > 4 else None
            end_date = args[4] if len(args) + 1 > 5 else None

            result = wrapper.get_unemployment(
                countries=countries,
                frequency=frequency,
                start_date=start_date,
                end_date=end_date
            )
            print(json.dumps(result, indent=2))

        elif command == "economic_summary":
            country = args[1] if len(args) + 1 > 2 else "united_states"
            start_date = args[2] if len(args) + 1 > 3 else None
            end_date = args[3] if len(args) + 1 > 4 else None

            result = wrapper.get_economic_summary(
                country=country,
                start_date=start_date,
                end_date=end_date
            )
            print(json.dumps(result, indent=2))

        elif command == "interest_rates":
            countries = args[1] if len(args) + 1 > 2 else "united_states"
            frequency = _norm_freq(args[2] if len(args) + 1 > 3 else None, "monthly")
            start_date = args[3] if len(args) + 1 > 4 else None
            end_date = args[4] if len(args) + 1 > 5 else None

            result = wrapper.get_interest_rates(
                countries=countries,
                frequency=frequency,
                start_date=start_date,
                end_date=end_date
            )
            print(json.dumps(result, indent=2))

        elif command == "trade_balance":
            countries = args[1] if len(args) + 1 > 2 else "united_states"
            frequency = _norm_freq(args[2] if len(args) + 1 > 3 else None, "quarter")
            start_date = args[3] if len(args) + 1 > 4 else None
            end_date = args[4] if len(args) + 1 > 5 else None

            result = wrapper.get_trade_balance(
                countries=countries,
                frequency=frequency,
                start_date=start_date,
                end_date=end_date
            )
            print(json.dumps(result, indent=2))

        elif command == "country_list":
            result = wrapper.get_country_list()
            print(json.dumps(result, indent=2))

        else:
            print(json.dumps({
                "success": False,
                "error": f"Unknown command: {command}",
                "available_commands": [
                    "gdp_real [countries] [frequency] [start_date] [end_date]",
                    "cpi [countries] [expenditure] [frequency] [units] [harmonized] [start_date] [end_date]",
                    "gdp_forecast [countries] [start_date] [end_date]",
                    "unemployment [countries] [frequency] [start_date] [end_date]",
                    "interest_rates [countries] [frequency] [start_date] [end_date]",
                    "trade_balance [countries] [frequency] [start_date] [end_date]",
                    "economic_summary [country] [start_date] [end_date]",
                    "country_list"
                ]
            }))
            sys.exit(1)

    except KeyboardInterrupt:
        print(json.dumps({"success": False, "error": "Operation cancelled by user"}))
        sys.exit(1)
    except Exception as e:
        print(json.dumps({"success": False, "error": f"Unexpected error: {str(e)}"}))
        sys.exit(1)

if __name__ == "__main__":
    main()