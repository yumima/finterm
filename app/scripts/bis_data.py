"""
BIS (Bank for International Settlements) SDMX API wrapper
Provides access to global economic and financial statistics data from BIS

API Documentation: https://www.bis.org/statistics/about_bis_stats_data_services.htm
Base URL: https://stats.bis.org/api/v1

Key Features:
- No API key required (public data)
- SDMX 2.1 format support
- Multiple response formats (XML, JSON, CSV)
- Comprehensive time series data
- Global economic and financial statistics
"""

import asyncio
import json
import sys
import re
import xml.etree.ElementTree as ET
from datetime import datetime, timedelta
from typing import Dict, Any, Optional, List, Union
from urllib.parse import urlencode, quote
import aiohttp
import html


class BISError(Exception):
    """Custom exception for BIS API errors"""
    def __init__(self, message: str, status_code: Optional[int] = None, endpoint: Optional[str] = None):
        super().__init__(message)
        self.status_code = status_code
        self.endpoint = endpoint


class BISAPI:
    """BIS SDMX API client with comprehensive coverage of all endpoints"""

    def __init__(self, base_url: str = "https://stats.bis.org/api/v1", timeout: int = 30):
        self.base_url = base_url.rstrip('/')
        self.timeout = aiohttp.ClientTimeout(total=timeout)
        self.session = None

        # Common BIS data flows (statistical domains)
        self.known_flows = {
            "WS_EER": "Effective exchange rates",
            "WS_CBPOL": "Central bank policy rates",
            "WS_DT1": "Debt securities",
            "WS_LTINT": "Long-term interest rates",
            "WS_STINT": "Short-term interest rates",
            "WS_MON": "Monetary aggregates",
            "WS_XRU": "Exchange rates",
            "WS_CRD": "Credit to the non-financial sector",
            "WS_HP": "House prices",
            "WS_REER": "Real effective exchange rates",
            "WS_CUST": "Customs and exchange controls",
            "WS_FDI": "Foreign direct investment",
            "WS_CUR": "Currency composition of official foreign exchange reserves"
        }

        # Random user agents for requests
        self.user_agents = [
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
            "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36",
            "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36"
        ]

    async def __aenter__(self):
        self.session = aiohttp.ClientSession(timeout=self.timeout)
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        if self.session:
            await self.session.close()

    def _get_headers(self, endpoint_type: str = "data") -> Dict[str, str]:
        """Get request headers with random user agent"""
        import random

        # Set appropriate Accept header based on endpoint type
        if endpoint_type == "structure":
            accept_header = "application/vnd.sdmx.structure+json;version=1.0.0,application/vnd.sdmx.structure+xml;version=2.1,application/xml"
        else:
            accept_header = "application/vnd.sdmx.data+json;version=1.0.0,application/vnd.sdmx.genericdata+xml;version=2.1,application/xml"

        return {
            "User-Agent": random.choice(self.user_agents),
            "Accept": accept_header,
            "Accept-Encoding": "gzip, deflate"
        }

    async def _make_request(self, endpoint: str, params: Optional[Dict[str, Any]] = None, endpoint_type: str = "data") -> Dict[str, Any]:
        """Make HTTP request with error handling"""
        if not self.session:
            raise BISError("Session not initialized. Use async with BISAPI()...")

        url = f"{self.base_url}/{endpoint}"
        headers = self._get_headers(endpoint_type)

        try:
            async with self.session.get(url, params=params, headers=headers) as response:
                if response.status == 200:
                    content_type = response.headers.get('content-type', '')

                    if 'application/json' in content_type or 'application/vnd.sdmx.data+json' in content_type:
                        data = await response.json()
                        return self._format_response(data, endpoint)
                    elif 'application/xml' in content_type or 'text/xml' in content_type:
                        text_data = await response.text()
                        return self._parse_xml_response(text_data, endpoint)
                    elif 'text/csv' in content_type:
                        text_data = await response.text()
                        return self._parse_csv_response(text_data, endpoint)
                    else:
                        # Try to parse as JSON first, then as text
                        try:
                            data = await response.json()
                            return self._format_response(data, endpoint)
                        except:
                            text_data = await response.text()
                            return {
                                "success": True,
                                "data": text_data,
                                "content_type": content_type,
                                "endpoint": endpoint,
                                "params": params
                            }
                else:
                    error_text = await response.text()
                    raise BISError(f"HTTP {response.status}: {error_text}", response.status, endpoint)

        except asyncio.TimeoutError:
            raise BISError(f"Request timeout after {self.timeout.total} seconds", None, endpoint)
        except aiohttp.ClientError as e:
            raise BISError(f"Network error: {str(e)}", None, endpoint)
        except Exception as e:
            raise BISError(f"Unexpected error: {str(e)}", None, endpoint)

    def _format_response(self, data: Dict[str, Any], endpoint: str) -> Dict[str, Any]:
        """Format API response with metadata"""
        return {
            "success": True,
            "data": data,
            "endpoint": endpoint,
            "timestamp": datetime.now().isoformat()
        }

    def _parse_xml_response(self, xml_data: str, endpoint: str) -> Dict[str, Any]:
        """Parse XML SDMX response"""
        try:
            root = ET.fromstring(xml_data)

            # Extract basic information
            result = {
                "success": True,
                "data": {
                    "xml_raw": xml_data,
                    "root_tag": root.tag,
                    "namespaces": dict(root.attrib.items()) if hasattr(root, 'attrib') else {}
                },
                "endpoint": endpoint,
                "format": "xml"
            }

            # Try to extract series data if present
            series = root.findall('.//{http://www.sdmx.org/resources/sdmxml/schemas/v2_1/data/generic}Series')
            if series:
                result["data"]["series_count"] = len(series)
                series_data = []
                for series_elem in series[:5]:  # Limit to first 5 for preview
                    series_info = {}
                    for child in series_elem:
                        if child.tag.endswith('SeriesKey'):
                            series_info["key"] = {v.attrib.get('id'): v.text for v in child}
                        elif child.tag.endswith('Obs'):
                            obs_info = {v.attrib.get('id'): v.text for v in child}
                            series_info.setdefault("observations", []).append(obs_info)
                    series_data.append(series_info)
                result["data"]["series_preview"] = series_data

            return result

        except ET.ParseError as e:
            return {
                "success": True,
                "data": {"xml_raw": xml_data, "parse_error": str(e)},
                "endpoint": endpoint,
                "format": "xml"
            }

    def _parse_csv_response(self, csv_data: str, endpoint: str) -> Dict[str, Any]:
        """Parse CSV response"""
        lines = csv_data.strip().split('\n')
        return {
            "success": True,
            "data": {
                "csv_raw": csv_data,
                "lines_count": len(lines),
                "header": lines[0] if lines else "",
                "preview": lines[:10]  # First 10 lines as preview
            },
            "endpoint": endpoint,
            "format": "csv"
        }

    # DATA QUERY ENDPOINTS

    async def get_data(
        self,
        flow: str,
        key: str = "all",
        start_period: Optional[str] = None,
        end_period: Optional[str] = None,
        first_n_observations: Optional[int] = None,
        last_n_observations: Optional[int] = None,
        detail: str = "full"
    ) -> Dict[str, Any]:
        """
        Get statistical data for a specific flow and key combination

        Args:
            flow: Statistical domain (e.g., 'WS_EER', 'BIS,WS_EER,1.0')
            key: Data key identifying series (use 'all' for all data)
            start_period: Start period (ISO 8601 or SDMX format)
            end_period: End period (ISO 8601 or SDMX format)
            first_n_observations: Return first N observations from oldest
            last_n_observations: Return last N observations from newest
            detail: Detail level ('full', 'dataonly', 'serieskeysonly', 'nodata')
        """
        params = {}
        if start_period:
            params["startPeriod"] = start_period
        if end_period:
            params["endPeriod"] = end_period
        if first_n_observations:
            params["firstNObservations"] = first_n_observations
        if last_n_observations:
            params["lastNObservations"] = last_n_observations
        if detail != "full":
            params["detail"] = detail

        # URL encode the flow and key properly
        encoded_flow = quote(flow, safe='')
        encoded_key = quote(key, safe='')

        endpoint = f"data/{encoded_flow}/{encoded_key}/all"
        return await self._make_request(endpoint, params)

    # STRUCTURE QUERY ENDPOINTS

    async def get_dataflows(
        self,
        agency_id: str = "all",
        resource_id: str = "all",
        version: str = "all",
        references: str = "none",
        detail: str = "full"
    ) -> Dict[str, Any]:
        """Get dataflow definitions (available datasets)"""
        params = {"references": references, "detail": detail}

        agency_id = quote(agency_id, safe='')
        resource_id = quote(resource_id, safe='')
        version = quote(version, safe='')

        endpoint = f"dataflow/{agency_id}/{resource_id}/{version}"
        return await self._make_request(endpoint, params, "structure")

    async def get_codelists(
        self,
        agency_id: str = "all",
        resource_id: str = "all",
        version: str = "all",
        references: str = "none",
        detail: str = "full"
    ) -> Dict[str, Any]:
        """Get codelist definitions"""
        params = {"references": references, "detail": detail}

        agency_id = quote(agency_id, safe='')
        resource_id = quote(resource_id, safe='')
        version = quote(version, safe='')

        endpoint = f"codelist/{agency_id}/{resource_id}/{version}"
        return await self._make_request(endpoint, params, "structure")

    # ITEM QUERY ENDPOINTS

    async def get_categories(
        self,
        agency_id: str,
        resource_id: str,
        version: str,
        item_id: str = "all",
        references: str = "none",
        detail: str = "full"
    ) -> Dict[str, Any]:
        """Get specific categories from category schemes"""
        params = {"references": references, "detail": detail}

        agency_id = quote(agency_id, safe='')
        resource_id = quote(resource_id, safe='')
        version = quote(version, safe='')
        item_id = quote(item_id, safe='')

        endpoint = f"categoryscheme/{agency_id}/{resource_id}/{version}/{item_id}"
        return await self._make_request(endpoint, params, "structure")

    # CONVENIENCE METHODS FOR COMMON DATA TYPES

    async def get_effective_exchange_rates(
        self,
        countries: Optional[List[str]] = None,
        start_period: Optional[str] = None,
        end_period: Optional[str] = None,
        detail: str = "dataonly"
    ) -> Dict[str, Any]:
        """
        Get effective exchange rates data (WS_EER)

        Args:
            countries: List of country codes (e.g., ['US', 'GB', 'JP'])
            start_period: Start period
            end_period: End period
            detail: Detail level
        """
        key = "all"
        if countries:
            # Build key with country codes
            # Format example: M.N.B.US+GB+JP
            key = f"M.N.B.{'+'.join(countries)}"

        return await self.get_data(
            flow="WS_EER",
            key=key,
            start_period=start_period,
            end_period=end_period,
            detail=detail
        )

    async def get_central_bank_policy_rates(
        self,
        countries: Optional[List[str]] = None,
        start_period: Optional[str] = None,
        end_period: Optional[str] = None,
        detail: str = "dataonly"
    ) -> Dict[str, Any]:
        """
        Get central bank policy rates (WS_CBPOL)

        Args:
            countries: List of country codes
            start_period: Start period
            end_period: End period
            detail: Detail level
        """
        key = "all"
        if countries:
            key = f"D.{'+'.join(countries)}"

        return await self.get_data(
            flow="WS_CBPOL",
            key=key,
            start_period=start_period,
            end_period=end_period,
            detail=detail
        )

    async def get_long_term_interest_rates(
        self,
        countries: Optional[List[str]] = None,
        start_period: Optional[str] = None,
        end_period: Optional[str] = None,
        detail: str = "dataonly"
    ) -> Dict[str, Any]:
        """Get long-term interest rates (WS_LTINT)"""
        key = "all"
        if countries:
            key = f"M.{'+'.join(countries)}"

        return await self.get_data(
            flow="WS_LTINT",
            key=key,
            start_period=start_period,
            end_period=end_period,
            detail=detail
        )

    async def get_short_term_interest_rates(
        self,
        countries: Optional[List[str]] = None,
        start_period: Optional[str] = None,
        end_period: Optional[str] = None,
        detail: str = "dataonly"
    ) -> Dict[str, Any]:
        """Get short-term interest rates (WS_STINT)"""
        key = "all"
        if countries:
            key = f"M.{'+'.join(countries)}"

        return await self.get_data(
            flow="WS_STINT",
            key=key,
            start_period=start_period,
            end_period=end_period,
            detail=detail
        )

    async def get_exchange_rates(
        self,
        currency_pairs: Optional[List[str]] = None,
        start_period: Optional[str] = None,
        end_period: Optional[str] = None,
        detail: str = "dataonly"
    ) -> Dict[str, Any]:
        """
        Get exchange rates (WS_XRU)

        Args:
            currency_pairs: List of currency pairs (e.g., ['USD', 'EUR', 'GBP'])
            start_period: Start period
            end_period: End period
            detail: Detail level
        """
        key = "all"
        if currency_pairs:
            key = f"D.{'+'.join(currency_pairs)}"

        return await self.get_data(
            flow="WS_XRU",
            key=key,
            start_period=start_period,
            end_period=end_period,
            detail=detail
        )

    async def get_credit_to_non_financial_sector(
        self,
        countries: Optional[List[str]] = None,
        sectors: Optional[List[str]] = None,
        start_period: Optional[str] = None,
        end_period: Optional[str] = None,
        detail: str = "dataonly"
    ) -> Dict[str, Any]:
        """
        Get credit to non-financial sector (WS_CRD)

        Args:
            countries: List of country codes
            sectors: List of sectors
            start_period: Start period
            end_period: End period
            detail: Detail level
        """
        key = "all"
        if countries and sectors:
            # Example format: Q.US.GBFC+GBFCNP+GBFCP
            key = f"Q.{'+'.join(countries)}.{'+'.join(sectors)}"
        elif countries:
            key = f"Q.{'+'.join(countries)}"

        return await self.get_data(
            flow="WS_CRD",
            key=key,
            start_period=start_period,
            end_period=end_period,
            detail=detail
        )

    async def search_datasets(
        self,
        query: str,
        agency_id: str = "BIS"
    ) -> Dict[str, Any]:
        """
        Search for datasets matching a query

        Args:
            query: Search term
            agency_id: Agency to search in
        """
        # Get all dataflows and search through them
        result = await self.get_dataflows(agency_id=agency_id, detail="full")

        if result.get("success") and "data" in result:
            dataflows = result["data"]
            # Simple text search in the response
            matches = []
            if isinstance(dataflows, dict):
                for key, value in dataflows.items():
                    if query.lower() in str(value).lower():
                        matches.append({key: value})

            return {
                "success": True,
                "data": {
                    "query": query,
                    "matches": matches,
                    "total_matches": len(matches)
                },
                "endpoint": "search"
            }

        return result

    # COMPREHENSIVE ANALYSIS METHODS

    async def get_economic_overview(
        self,
        countries: Optional[List[str]] = None,
        start_period: Optional[str] = None,
        end_period: Optional[str] = None
    ) -> Dict[str, Any]:
        """
        Get comprehensive economic overview for countries

        Args:
            countries: List of country codes
            start_period: Start period
            end_period: End period
        """
        results = {}

        # Get multiple economic indicators
        indicators = [
            ("effective_exchange_rates", self.get_effective_exchange_rates),
            ("central_bank_policy_rates", self.get_central_bank_policy_rates),
            ("long_term_interest_rates", self.get_long_term_interest_rates),
            ("short_term_interest_rates", self.get_short_term_interest_rates),
            ("exchange_rates", self.get_exchange_rates),
            ("credit_to_non_financial_sector", self.get_credit_to_non_financial_sector)
        ]

        for name, method in indicators:
            try:
                result = await method(
                    countries=countries,
                    start_period=start_period,
                    end_period=end_period,
                    detail="dataonly"
                )
                results[name] = result
            except Exception as e:
                results[name] = {"success": False, "error": str(e)}

        return {
            "success": True,
            "data": {
                "economic_overview": results,
                "countries": countries or ["all"],
                "period": {
                    "start": start_period or "earliest",
                    "end": end_period or "latest"
                },
                "indicators_requested": len(indicators),
                "indicators_retrieved": len([r for r in results.values() if r.get("success", False)])
            },
            "endpoint": "economic_overview"
        }

# CLI Interface
async def main():
    """CLI interface for BIS API"""
    args = sys.argv[1:]

    if len(args) < 1:
        print(json.dumps({
            "error": "Usage: python bis_data.py <command> [args...]",
            "available_commands": [
                "get_data <flow> [key] [start_period] [end_period]",
                "search_datasets <query>",
            ]
        }))
        return

    command = args[0]

    async with BISAPI() as bis:
        try:
            if command == "fetch":
                # Simplified fetch command for finterm Economics tab
                # Usage: fetch <dataflow> <country_code> [start_period] [end_period]
                # Returns flattened [{date, value}] array for charting
                if len(args) < 3:
                    raise ValueError("Usage: fetch <dataflow> <country_code> [start_period] [end_period]")

                dataflow = args[1]
                country_code = args[2]
                start_period = args[3] if len(args) > 3 else None
                end_period = args[4] if len(args) > 4 else None

                # Key patterns per dataflow — each flow has different dimension layouts
                # Format: dots separate dimension positions, country goes in the right slot
                KEY_PATTERNS = {
                    # FREQ.REF_AREA
                    "WS_CBPOL": "M.{cc}",
                    # FREQ.EER_TYPE.EER_BASKET.REF_AREA
                    "WS_EER": "M.N.B.{cc}",
                    "WS_EER_R": "M.R.B.{cc}",
                    # FREQ.REF_AREA.CURRENCY.COLLECTION
                    "WS_XRU": "M.{cc}..A",          # period average, national currency per USD
                    # FREQ.REF_AREA.UNIT_MEASURE
                    "WS_LONG_CPI": "M.{cc}.771",    # year-on-year % change
                    # FREQ.REF_AREA.COMP_METHOD.UNIT_MEASURE.CURRENCY.TRANSFORMATION
                    "WS_CBTA": "Q.{cc}",
                    # FREQ.BORROWERS_CTY...
                    "WS_CREDIT_GAP": "Q.{cc}",
                    # Private non-financial sector, all lenders, market value, % of GDP, break-adjusted
                    "WS_TC": "Q.{cc}.P.A.M.770.A",
                    "WS_DSR": "Q.{cc}",
                    # FREQ.CURR_DENOM.BORROWERS_CTY...
                    "WS_GLI": "Q..{cc}",
                    # FREQ.REF_AREA.VALUE.UNIT_MEASURE
                    "WS_SPP": "Q.{cc}.R.628",       # real residential property prices, 2010=100
                    "WS_CPP": "Q.{cc}",
                    "WS_DPP": "Q.{cc}",
                    # FREQ.L_MEASURE.L_REP_CTY...
                    "WS_CBS_PUB": "Q..{cc}",
                    "WS_LBS_D_PUB": "Q..{cc}",
                    # FREQ.ISSUER_RES...
                    "WS_DEBT_SEC2_PUB": "Q.{cc}",
                    # FREQ.ADJUSTMENT.REF_AREA...
                    "WS_NA_SEC_DSS": "Q..{cc}",
                    # OTC/exchange-traded derivatives — global, no country filter
                    "WS_OTC_DERIV2": "H",
                    "WS_DER_OTC_TOV": "T",
                    "WS_XTD_DERIV": "Q",
                    # CPMI flows — annual, country in REP_CTY pos 2
                    "WS_CPMI_MACRO": "A..{cc}",
                    "WS_CPMI_CASHLESS": "A..{cc}",
                    "WS_CPMI_CT1": "A..{cc}",
                    "WS_CPMI_CT2": "A..{cc}",
                    "WS_CPMI_DEVICES": "A..{cc}",
                    "WS_CPMI_INSTITUT": "A..{cc}",
                    "WS_CPMI_PARTICIP": "A..{cc}",
                    "WS_CPMI_SYSTEMS": "A..{cc}",
                }

                pattern = KEY_PATTERNS.get(dataflow, "M.{cc}")
                if country_code and country_code != "all":
                    key = pattern.format(cc=country_code)
                else:
                    key = pattern.format(cc="")

                raw = await bis.get_data(dataflow, key, start_period, end_period)

                if not raw.get("success"):
                    result = raw
                else:
                    # Flatten SDMX-JSON into simple [{date, value}] for charting
                    flat_data = []
                    try:
                        resp = raw.get("data", {})
                        sdmx_data = resp.get("data", resp)
                        datasets = sdmx_data.get("dataSets", [])
                        structure = sdmx_data.get("structure", resp.get("structure", {}))
                        obs_dims = structure.get("dimensions", {}).get("observation", [])

                        # Get time period values from observation dimension
                        time_values = {}
                        if obs_dims:
                            time_values = {str(i): v.get("id", v.get("name", str(i)))
                                           for i, v in enumerate(obs_dims[0].get("values", []))}

                        series_count = sum(len(ds.get("series", {})) for ds in datasets)
                        for ds in datasets:
                            series_map = ds.get("series", {})
                            for _series_key, series_val in series_map.items():
                                observations = series_val.get("observations", {})
                                for obs_key, obs_val in observations.items():
                                    period = time_values.get(obs_key, obs_key)
                                    value = obs_val[0] if isinstance(obs_val, list) and obs_val else None
                                    if value is not None:
                                        try:
                                            value = float(value)
                                            import math
                                            if math.isnan(value) or math.isinf(value):
                                                continue
                                        except (ValueError, TypeError):
                                            continue
                                        row = {"date": str(period), "value": value}
                                        if series_count > 1:
                                            # Several series matched: keep them apart.
                                            # Averaging series with different units or
                                            # bases would invent a number.
                                            row["series"] = _series_key
                                        flat_data.append(row)

                        flat_data.sort(key=lambda d: (d.get("series", ""), d["date"]))

                    except Exception as parse_err:
                        result = {"success": False, "error": f"Failed to parse SDMX response: {str(parse_err)}"}
                        print(json.dumps(result, indent=2))
                        return

                    result = {
                        "success": True,
                        "data": flat_data,
                        "metadata": {
                            "dataflow": dataflow,
                            "country": country_code,
                            "key": key,
                            "series_count": series_count,
                            "source": "BIS"
                        }
                    }

            elif command == "get_data":
                if len(args) < 2:
                    raise ValueError("Usage: get_data <flow> [key] [start_period] [end_period]")

                flow = args[1]
                key = args[2] if len(args) > 2 else "all"
                start_period = args[3] if len(args) > 3 else None
                end_period = args[4] if len(args) > 4 else None

                result = await bis.get_data(flow, key, start_period, end_period)

            elif command == "search_datasets":
                if len(args) < 3:
                    raise ValueError("Usage: search_datasets <query>")
                query = args[1]
                result = await bis.search_datasets(query)

            elif command == "get_dataflows":
                agency_id = args[1] if len(args) > 2 else "all"
                resource_id = args[2] if len(args) > 3 else "all"
                version = args[3] if len(args) > 4 else "all"
                references = args[4] if len(args) > 5 else "none"
                detail = sys.argv[6] if len(args) > 6 else "full"
                result = await bis.get_dataflows(agency_id, resource_id, version, references, detail)

            elif command == "get_codelists":
                agency_id = args[1] if len(args) > 2 else "all"
                resource_id = args[2] if len(args) > 3 else "all"
                version = args[3] if len(args) > 4 else "all"
                references = args[4] if len(args) > 5 else "none"
                detail = sys.argv[6] if len(args) > 6 else "full"
                result = await bis.get_codelists(agency_id, resource_id, version, references, detail)

            elif command == "get_categories":
                if len(args) < 4:
                    raise ValueError("Usage: get_categories <agency_id> <resource_id> <version> [item_id] [references] [detail]")
                agency_id = args[1]
                resource_id = args[2]
                version = args[3]
                item_id = args[4] if len(args) > 5 else "all"
                references = sys.argv[6] if len(args) > 6 else "none"
                detail = sys.argv[7] if len(args) > 7 else "full"
                result = await bis.get_categories(agency_id, resource_id, version, item_id, references, detail)

            else:
                result = {
                    "success": False,
                    "error": f"Unknown command: {command}",
                    "available_commands": [
                        "get_data",
                        "search_datasets",
                        "get_dataflows",
                        "get_codelists",
                        "get_categories"
                    ]
                }

            print(json.dumps(result, indent=2))

        except BISError as e:
            error_response = {
                "success": False,
                "error": str(e),
                "endpoint": e.endpoint,
                "status_code": e.status_code
            }
            print(json.dumps(error_response, indent=2))

        except Exception as e:
            error_response = {
                "success": False,
                "error": str(e),
                "command": command
            }
            print(json.dumps(error_response, indent=2))


if __name__ == "__main__":
    asyncio.run(main())