"""
UN Comtrade API Data Fetcher
Comprehensive wrapper for the United Nations Comtrade Database API.
Provides access to global bilateral trade flows for 200+ countries at
HS product code level — the most comprehensive global trade dataset.

API Documentation: https://comtradedeveloper.un.org/
Official Python lib: https://github.com/uncomtrade/comtradeapicall
Base URL (public):  https://comtradeapi.un.org/public/v1/
Base URL (auth):    https://comtradeapi.un.org/data/v1/
Metadata URL:       https://comtradeapi.un.org/files/v1/app/

Authentication:
  - Without key: unlimited calls/day, max 500 records per call (preview endpoints)
  - With subscription key: 500 calls/day, max 100,000 records per call
  - Register free at: https://comtradedeveloper.un.org/

Key Concepts:
  - typeCode:   'C' = Goods/Commodities, 'S' = Services
  - freqCode:   'A' = Annual, 'M' = Monthly
  - clCode:     Classification scheme — 'HS' (Harmonized System), 'S1'/'S2'/'S3'/'S4' (SITC revisions), 'B4'/'B5' (BEC)
  - flowCode:   'X' = Export, 'M' = Import, 'RX' = Re-export, 'RM' = Re-import
  - period:     'YYYY' for annual, 'YYYYMM' for monthly
  - cmdCode:    HS commodity code e.g. '0101' (horses), 'TOTAL' for all
  - reporterCode / partnerCode: UN numeric country codes (e.g. 842=USA, 156=China, 356=India)
"""

import sys
import json
import os
import requests
from datetime import datetime, timezone, timedelta
from typing import Dict, Any, Optional, List, Union


BASE_URL_PUBLIC = "https://comtradeapi.un.org/public/v1"
BASE_URL_DATA   = "https://comtradeapi.un.org/data/v1"
BASE_URL_TOOLS  = "https://comtradeapi.un.org/tools/v1"
BASE_URL_BULK   = "https://comtradeapi.un.org/bulk/v1"
BASE_URL_FILES  = "https://comtradeapi.un.org/files/v1/app/reference"

# Static reference JSON files
REF_LIST_URL     = f"{BASE_URL_FILES}/../ListofReferences.json"
REF_REPORTERS    = f"{BASE_URL_FILES}/Reporters.json"
REF_PARTNERS     = f"{BASE_URL_FILES}/partnerAreas.json"
REF_FLOWS        = f"{BASE_URL_FILES}/tradeRegimes.json"
REF_MOT          = f"{BASE_URL_FILES}/ModeOfTransportCodes.json"
REF_CUSTOMS      = f"{BASE_URL_FILES}/CustomsCodes.json"
REF_ISO_CODES    = f"{BASE_URL_FILES}/country_area_code_iso.json"

# Reference category → URL map (populated after imports)
REFERENCE_URLS = {
    "reporter":          f"{BASE_URL_FILES}/Reporters.json",
    "partner":           f"{BASE_URL_FILES}/partnerAreas.json",
    "flow":              f"{BASE_URL_FILES}/tradeRegimes.json",
    "mot":               f"{BASE_URL_FILES}/ModeOfTransportCodes.json",
    "customs":           f"{BASE_URL_FILES}/CustomsCodes.json",
    "freq":              f"{BASE_URL_FILES}/Frequency.json",
    "qtyunit":           f"{BASE_URL_FILES}/QuantityUnits.json",
    "mos":               f"{BASE_URL_FILES}/ModeOfSupply.json",
    "dataitem":          f"{BASE_URL_FILES}/TradeDataItems.json",
    "HS":                f"{BASE_URL_FILES}/HS.json",
    "H0":                f"{BASE_URL_FILES}/H0.json",
    "H1":                f"{BASE_URL_FILES}/H1.json",
    "H2":                f"{BASE_URL_FILES}/H2.json",
    "H3":                f"{BASE_URL_FILES}/H3.json",
    "H4":                f"{BASE_URL_FILES}/H4.json",
    "H5":                f"{BASE_URL_FILES}/H5.json",
    "H6":                f"{BASE_URL_FILES}/H6.json",
    "S1":                f"{BASE_URL_FILES}/S1.json",
    "S2":                f"{BASE_URL_FILES}/S2.json",
    "S3":                f"{BASE_URL_FILES}/S3.json",
    "S4":                f"{BASE_URL_FILES}/S4.json",
    "B4":                f"{BASE_URL_FILES}/B4.json",
    "B5":                f"{BASE_URL_FILES}/B5.json",
    "EB02":              f"{BASE_URL_FILES}/EB02.json",
    "EB10":              f"{BASE_URL_FILES}/EB10.json",
}

# Common UN country codes for convenience
COUNTRY_CODES = {
    "USA": 842, "CHN": 156, "DEU": 276, "JPN": 392, "GBR": 826,
    "FRA": 251, "IND": 356, "ITA": 381, "CAN": 124, "KOR": 410,
    "RUS": 643, "AUS": 36,  "BRA": 76,  "ESP": 724, "MEX": 484,
    "IDN": 360, "NLD": 528, "SAU": 682, "TUR": 792, "CHE": 757,
    "POL": 616, "BEL": 56,  "SWE": 752, "ARG": 32,  "NOR": 578,
    "ARE": 784, "THA": 764, "ZAF": 710, "MYS": 458, "SGP": 702,
    "DNK": 208, "EGY": 818, "PHL": 608, "CHL": 152, "VNM": 704,
    "BGD": 50,  "NGA": 566, "PAK": 586, "IRN": 364, "ISR": 376,
    "AUT": 40,  "PRT": 620, "GRC": 300, "CZE": 203, "ROU": 642,
    "HUN": 348, "FIN": 246, "NZL": 554, "PER": 604, "COL": 170,
}

# Common HS chapter codes
HS_CHAPTERS = {
    "01": "Live animals",
    "02": "Meat and edible meat offal",
    "03": "Fish and crustaceans",
    "04": "Dairy produce; eggs; honey",
    "10": "Cereals",
    "15": "Animal/vegetable fats and oils",
    "22": "Beverages, spirits and vinegar",
    "27": "Mineral fuels and oils",
    "28": "Inorganic chemicals",
    "29": "Organic chemicals",
    "30": "Pharmaceutical products",
    "39": "Plastics",
    "40": "Rubber",
    "44": "Wood and articles of wood",
    "52": "Cotton",
    "61": "Knitted/crocheted clothing",
    "72": "Iron and steel",
    "84": "Nuclear reactors, boilers, machinery",
    "85": "Electrical machinery and equipment",
    "87": "Vehicles",
    "88": "Aircraft and spacecraft",
    "90": "Optical, photographic, medical instruments",
    "TOTAL": "All commodities",
}


class ComtradeError:
    """Error handling wrapper for UN Comtrade API responses"""

    def __init__(self, endpoint: str, error: str, status_code: Optional[int] = None):
        self.endpoint = endpoint
        self.error = error
        self.status_code = status_code
        self.timestamp = int(datetime.now(timezone.utc).timestamp())

    def to_dict(self) -> Dict[str, Any]:
        return {
            "success": False,
            "error": self.error,
            "endpoint": self.endpoint,
            "status_code": self.status_code,
            "timestamp": self.timestamp,
        }


class UNComtradeWrapper:
    """
    Comprehensive UN Comtrade API wrapper.
    Uses public (no-key) preview endpoints by default.
    Pass a subscription_key for full data access (500 calls/day, 100k records/call).
    """

    def __init__(self, subscription_key: Optional[str] = None):
        self.subscription_key = subscription_key or os.environ.get("UN_COMTRADE_API_KEY", None)
        self.session = requests.Session()
        self.session.headers.update({
            "User-Agent": "finterm-Terminal/1.0",
            "Accept": "application/json",
        })

    # ==================== INTERNAL HELPERS ====================

    def _make_request(self, url: str, params: Dict[str, Any]) -> Dict[str, Any]:
        """Centralized GET request with error handling"""
        try:
            # Strip None values
            clean_params = {k: v for k, v in params.items() if v is not None}

            # Inject subscription key if available
            if self.subscription_key:
                clean_params["subscription-key"] = self.subscription_key

            response = self.session.get(url, params=clean_params, timeout=60)

            if response.status_code == 401:
                return ComtradeError(url, "Unauthorized — invalid or missing subscription key.", 401).to_dict()
            if response.status_code == 429:
                return ComtradeError(url, "Rate limit exceeded. 500 calls/day limit reached.", 429).to_dict()
            if response.status_code == 403:
                return ComtradeError(url, "Forbidden — subscription key may be expired or insufficient.", 403).to_dict()

            response.raise_for_status()
            data = response.json()

            # Comtrade wraps results in {"data": [...], "count": N, "error": null}
            if isinstance(data, dict) and data.get("error"):
                return ComtradeError(url, str(data["error"])).to_dict()

            records = data.get("data", data) if isinstance(data, dict) else data
            count   = data.get("count", len(records) if isinstance(records, list) else 0)

            return {
                "success": True,
                "endpoint": url,
                "data": records,
                "count": count,
                "timestamp": int(datetime.now(timezone.utc).timestamp()),
            }

        except requests.exceptions.HTTPError as e:
            return ComtradeError(url, f"HTTP error: {str(e)}", getattr(e.response, "status_code", None)).to_dict()
        except requests.exceptions.Timeout:
            return ComtradeError(url, "Request timed out after 60 seconds.").to_dict()
        except requests.exceptions.RequestException as e:
            return ComtradeError(url, f"Network error: {str(e)}").to_dict()
        except json.JSONDecodeError as e:
            return ComtradeError(url, f"JSON decode error: {str(e)}").to_dict()
        except Exception as e:
            return ComtradeError(url, f"Unexpected error: {str(e)}").to_dict()

    def _trade_url(self, endpoint: str, type_code: str, freq_code: str, cl_code: str) -> str:
        """Build the correct base URL depending on whether we have a subscription key"""
        if self.subscription_key:
            return f"{BASE_URL_DATA}/{endpoint}/{type_code}/{freq_code}/{cl_code}"
        else:
            preview_ep = "preview" if endpoint == "get" else "previewTariffline"
            return f"{BASE_URL_PUBLIC}/{preview_ep}/{type_code}/{freq_code}/{cl_code}"

    # ==================== TRADE DATA ENDPOINTS ====================

    def get_trade_data(
        self,
        reporter_code: Union[str, int],
        period: str,
        type_code: str = "C",
        freq_code: str = "A",
        cl_code: str = "HS",
        cmd_code: str = "TOTAL",
        flow_code: str = "X,M",
        partner_code: Optional[Union[str, int]] = None,
        partner2_code: Optional[Union[str, int]] = None,
        customs_code: Optional[str] = None,
        mot_code: Optional[str] = None,
        max_records: int = 500,
        include_desc: bool = True,
        aggregate_by: Optional[str] = None,
        breakdown_mode: str = "classic",
        count_only: bool = False,
    ) -> Dict[str, Any]:
        """
        Retrieve trade data (final aggregated).
        Without subscription key: limited to 500 records (preview).
        With subscription key: up to 100,000 records.

        Args:
            reporter_code: UN country code of the reporting country (e.g. 842 = USA)
                           Use 'all' for all reporters.
            period: 'YYYY' for annual or 'YYYYMM' for monthly. Comma-separate multiple.
                    E.g. '2023', '202301,202302', '2020,2021,2022'
            type_code: 'C' = Goods, 'S' = Services
            freq_code: 'A' = Annual, 'M' = Monthly
            cl_code: 'HS' = Harmonized System, 'S1'/'S2'/'S3'/'S4' = SITC revisions, 'B4'/'B5' = BEC
            cmd_code: Commodity code(s). 'TOTAL' = all. Comma-separate multiples.
                      E.g. '27' (mineral fuels), '84,85' (machinery+electronics)
            flow_code: 'X' = exports, 'M' = imports, 'RX' = re-exports, 'RM' = re-imports
                       Comma-separate e.g. 'X,M'
            partner_code: Trading partner country code. None = all partners. '0' = world total.
            partner2_code: Second partner code (for re-export/re-import flows)
            customs_code: Customs procedure code. None = all.
            mot_code: Mode of transport code. None = all.
            max_records: Max records to return (500 for preview, up to 100000 with key)
            include_desc: Include human-readable descriptions for codes
            aggregate_by: Aggregation field
            breakdown_mode: 'classic' or 'plus'
            count_only: If True, return only record count
        """
        url = self._trade_url("get", type_code, freq_code, cl_code)
        params = {
            "reporterCode": reporter_code,
            "period": period,
            "cmdCode": cmd_code,
            "flowCode": flow_code,
            "partnerCode": partner_code,
            "partner2Code": partner2_code,
            "customsCode": customs_code,
            "motCode": mot_code,
            "maxRecords": max_records,
            "includeDesc": str(include_desc).lower(),
            "aggregateBy": aggregate_by,
            "breakdownMode": breakdown_mode,
            "countOnly": str(count_only).lower() if count_only else None,
        }
        result = self._make_request(url, params)
        if result.get("success"):
            result["reporter_code"] = reporter_code
            result["period"] = period
            result["cmd_code"] = cmd_code
            result["flow_code"] = flow_code
        return result

    def get_trade_balance(
        self,
        reporter_code: Union[str, int],
        period: str,
        type_code: str = "C",
        freq_code: str = "A",
        cl_code: str = "HS",
        cmd_code: str = "TOTAL",
        partner_code: Optional[Union[str, int]] = None,
        include_desc: bool = True,
    ) -> Dict[str, Any]:
        """
        Get trade balance — exports and imports side by side for a reporter.
        Requires subscription key.

        Args:
            reporter_code: Reporting country UN code
            period: Period(s) e.g. '2023' or '2020,2021,2022,2023'
            type_code: 'C' or 'S'
            freq_code: 'A' or 'M'
            cl_code: Classification
            cmd_code: Commodity code
            partner_code: Partner country (None = world)
            include_desc: Include descriptions
        """
        if not self.subscription_key:
            return ComtradeError(
                "/tools/v1/getTradeBalance",
                "Trade balance endpoint requires a subscription key. Register free at https://comtradedeveloper.un.org/"
            ).to_dict()

        url = f"{BASE_URL_TOOLS}/getTradeBalance/{type_code}/{freq_code}/{cl_code}"
        params = {
            "reporterCode": reporter_code,
            "period": period,
            "cmdCode": cmd_code,
            "partnerCode": partner_code,
            "includeDesc": str(include_desc).lower(),
        }
        result = self._make_request(url, params)
        if result.get("success"):
            result["reporter_code"] = reporter_code
            result["period"] = period
        return result

    def get_trade_matrix(
        self,
        reporter_code: Union[str, int],
        period: str,
        type_code: str = "C",
        freq_code: str = "A",
    ) -> Dict[str, Any]:
        """
        Get the world trade matrix with estimates for missing data.
        Useful for constructing a complete bilateral trade network.
        Requires subscription key.

        Args:
            reporter_code: Country UN code
            period: 'YYYY' or 'YYYYMM'
            type_code: 'C' or 'S'
            freq_code: 'A' or 'M'
        """
        if not self.subscription_key:
            return ComtradeError(
                "/data/v1/getTradeMatrix",
                "Trade matrix endpoint requires a subscription key."
            ).to_dict()

        url = f"{BASE_URL_DATA}/getTradeMatrix/{type_code}/{freq_code}/TM"
        params = {
            "reporterCode": reporter_code,
            "period": period,
        }
        return self._make_request(url, params)

    # ==================== DATA AVAILABILITY ====================

    # ==================== REFERENCE / METADATA ====================

    def get_reference(self, category: str) -> Dict[str, Any]:
        """
        Get a specific reference/lookup table.

        Args:
            category: Reference table name. Common values:
                'reporter'         — All reporting countries with UN codes
                'partner'          — All partner countries with UN codes
                'HS'               — Harmonized System commodity codes (latest)
                'H0'...'H6'        — HS revisions 1992–2022
                'S1','S2','S3','S4'— SITC revision commodity codes
                'B4','B5'          — BEC commodity codes
                'flow'             — Trade flow types (X, M, RX, RM)
                'mot'              — Modes of transport
                'customs'          — Customs procedure codes
                'freq'             — Frequency codes
                'qtyunit'          — Quantity unit codes
        """
        url = REFERENCE_URLS.get(category)
        if not url:
            return ComtradeError(
                f"Unknown category '{category}'. Valid: {list(REFERENCE_URLS.keys())}"
            ).to_dict()
        result = self._make_request(url, {})
        if result.get("success"):
            result["category"] = category
        return result

    def get_metadata(
        self,
        reporter_code: Optional[Union[str, int]] = None,
        type_code: str = "C",
        freq_code: str = "A",
        cl_code: str = "HS",
    ) -> Dict[str, Any]:
        """
        Get metadata and publication notes for a dataset.

        Args:
            reporter_code: Country code (None = all)
            type_code: 'C' or 'S'
            freq_code: 'A' or 'M'
            cl_code: Classification
        """
        base = BASE_URL_DATA if self.subscription_key else BASE_URL_PUBLIC
        url = f"{base}/getMetadata/{type_code}/{freq_code}/{cl_code}"
        params = {"reporterCode": reporter_code}
        return self._make_request(url, params)

    # ==================== HIGH-LEVEL CONVENIENCE METHODS ====================

    def get_country_codes(self) -> Dict[str, Any]:
        """
        Convenience: Get UN numeric country codes for all reporters.
        Returns the full reporter reference table including ISO3 codes.
        Also includes the built-in quick-reference dict for common countries.
        """
        result = self.get_reference("reporter")
        if result.get("success"):
            result["note"] = "Use the 'id' field as reporterCode in API calls"
            result["quick_reference"] = COUNTRY_CODES
        return result

    def get_hs_codes(self, level: str = "HS") -> Dict[str, Any]:
        """
        Convenience: Get Harmonized System commodity codes reference table.

        Args:
            level: 'HS' for full HS, or specific revision e.g. 'H0','H1','H2','H3','H4','H5','H6'
        """
        return self.get_reference(level)


# ==================== MAIN CLI ENTRYPOINT ====================

def main():
    if len(sys.argv) < 2:
        print(json.dumps({
            "success": False,
            "error": "No command provided",
            "available_commands": [
                "trade_data <reporter_code> <period> [flow_code] [cmd_code] [freq_code] [cl_code] [max_records]",
                "trade_balance <reporter_code> <period> [cmd_code]",
                "metadata [reporter_code] [type_code] [freq_code] [cl_code]",
                "country_codes",
                "hs_codes [classification]",
            ],
            "notes": [
                "Without UN_COMTRADE_API_KEY: max 500 records per call (preview endpoints)",
                "With UN_COMTRADE_API_KEY: 500 calls/day, up to 100,000 records per call",
                "Register free at: https://comtradedeveloper.un.org/",
                "flow_code: X=exports, M=imports, RX=re-exports, RM=re-imports",
                "freq_code: A=annual, M=monthly",
                "cl_code: HS, S1, S2, S3, S4, B4, B5",
                "period: YYYY for annual, YYYYMM for monthly",
            ],
            "common_country_codes": COUNTRY_CODES,
            "hs_chapter_reference": HS_CHAPTERS,
        }, indent=2))
        sys.exit(1)

    command = sys.argv[1]
    wrapper = UNComtradeWrapper()

    try:
        if command == "trade_data":
            if len(sys.argv) < 4:
                print(json.dumps({"success": False, "error": "Usage: un_comtrade_data.py trade_data <reporter_code> <period> [flow_code] [cmd_code] [freq_code] [cl_code] [max_records]"}))
                sys.exit(1)
            reporter_code = sys.argv[2]
            period        = sys.argv[3]
            flow_code     = sys.argv[4] if len(sys.argv) > 4 and sys.argv[4] else "X,M"
            cmd_code      = sys.argv[5] if len(sys.argv) > 5 and sys.argv[5] else "TOTAL"
            freq_code     = sys.argv[6] if len(sys.argv) > 6 and sys.argv[6] else "A"
            cl_code       = sys.argv[7] if len(sys.argv) > 7 and sys.argv[7] else "HS"
            max_records   = int(sys.argv[8]) if len(sys.argv) > 8 and sys.argv[8] else 500
            result = wrapper.get_trade_data(
                reporter_code=reporter_code, period=period, flow_code=flow_code,
                cmd_code=cmd_code, freq_code=freq_code, cl_code=cl_code, max_records=max_records,
            )
            print(json.dumps(result, indent=2))

        elif command == "trade_balance":
            if len(sys.argv) < 4:
                print(json.dumps({"success": False, "error": "Usage: un_comtrade_data.py trade_balance <reporter_code> <period> [cmd_code]"}))
                sys.exit(1)
            reporter_code = sys.argv[2]
            period        = sys.argv[3]
            cmd_code      = sys.argv[4] if len(sys.argv) > 4 and sys.argv[4] else "TOTAL"
            result = wrapper.get_trade_balance(reporter_code=reporter_code, period=period, cmd_code=cmd_code)
            print(json.dumps(result, indent=2))

        elif command == "metadata":
            reporter_code = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] else None
            type_code     = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else "C"
            freq_code     = sys.argv[4] if len(sys.argv) > 4 and sys.argv[4] else "A"
            cl_code       = sys.argv[5] if len(sys.argv) > 5 and sys.argv[5] else "HS"
            result = wrapper.get_metadata(reporter_code=reporter_code, type_code=type_code, freq_code=freq_code, cl_code=cl_code)
            print(json.dumps(result, indent=2))

        elif command == "country_codes":
            result = wrapper.get_country_codes()
            print(json.dumps(result, indent=2))

        elif command == "hs_codes":
            classification = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] else "HS"
            result = wrapper.get_hs_codes(level=classification)
            print(json.dumps(result, indent=2))

        else:
            print(json.dumps({
                "success": False,
                "error": f"Unknown command: {command}",
                "tip": "Run without arguments to see all available commands",
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
