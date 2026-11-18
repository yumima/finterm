"""
UNESCO Institute for Statistics (UIS) Data Fetcher
Comprehensive wrapper for UNESCO UIS Data API providing access to
global education, science, culture, and demographic statistics

API Documentation:
- Base URL: https://api.uis.unesco.org
- Authentication: No API key required (public API)
- Rate limits: None specified, but be reasonable with requests
- License: Creative Commons Attribution-ShareAlike 4.0 International

Available Data Categories:
- Education: Enrollment, completion rates, literacy, teaching staff
- Science Technology & Innovation: R&D expenditure, researchers, patents
- Culture: Heritage sites, cultural employment, cultural participation
- Demographic & Socio-Economic: Population, economy, social indicators

Data Coverage:
- 200+ countries and regions
- Time series data from 1970s to present
- Multiple disaggregation levels (by sex, age, region, etc.)
- 100,000+ indicators
"""

import sys
import json
import os
import requests
from datetime import datetime, timedelta
from typing import Dict, Any, Optional, List, Union
from urllib.parse import urlencode


class UISError:
    """Error handling wrapper for UNESCO UIS API responses"""
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


class UISWrapper:
    """Comprehensive UNESCO UIS API wrapper with fault tolerance"""

    def __init__(self):
        self.base_url = "https://api.uis.unesco.org/api/public"
        self.session = requests.Session()
        self.session.headers.update({
            'User-Agent': 'finterm-Terminal/1.0',
            'Accept': 'application/json',
            'Accept-Encoding': 'gzip'
        })

    def _make_request(self, endpoint: str, params: Dict[str, Any]) -> Dict[str, Any]:
        """Centralized request handler with comprehensive error handling"""
        try:
            url = f"{self.base_url}{endpoint}?{urlencode(params, doseq=True)}"

            response = self.session.get(url, timeout=60)
            response.raise_for_status()

            # Handle JSON response
            try:
                data = response.json()

                # Check for API errors in response
                if 'error' in data:
                    error_msg = data.get('message', 'Unknown API error')
                    return UISError(endpoint, error_msg, response.status_code).to_dict()

                return {
                    "success": True,
                    "endpoint": endpoint,
                    "data": data,
                    "parameters": params,
                    "timestamp": int(datetime.now().timestamp())
                }

            except json.JSONDecodeError as e:
                return UISError(endpoint, f"JSON decode error: {str(e)}", response.status_code).to_dict()

        except requests.exceptions.RequestException as e:
            return UISError(endpoint, f"Network error: {str(e)}").to_dict()
        except Exception as e:
            return UISError(endpoint, f"Unexpected error: {str(e)}").to_dict()

    # ==================== DATA ENDPOINTS ====================

    def get_indicator_data(self, indicators: List[str] = None, geo_units: List[str] = None,
                           geo_unit_type: str = None, start_year: int = None,
                           end_year: int = None, footnotes: bool = False,
                           indicator_metadata: bool = False, version: str = None) -> Dict[str, Any]:
        """Fetch indicator data records based on filter parameters"""
        try:
            params = {}

            if indicators:
                params['indicator'] = indicators
            if geo_units:
                params['geoUnit'] = geo_units
            if geo_unit_type:
                params['geoUnitType'] = geo_unit_type
            if start_year:
                params['start'] = start_year
            if end_year:
                params['end'] = end_year
            if footnotes:
                params['footnotes'] = footnotes
            if indicator_metadata:
                params['indicatorMetadata'] = indicator_metadata
            if version:
                params['version'] = version

            result = self._make_request('/data/indicators', params)
            return result

        except Exception as e:
            return UISError('get_indicator_data', str(e)).to_dict()

    # ==================== DEFINITIONS ENDPOINTS ====================

    def list_indicators(self, glossary_terms: bool = False, disaggregations: bool = False,
                       version: str = None) -> Dict[str, Any]:
        """Get list of all available indicators with optional metadata"""
        try:
            params = {}
            if glossary_terms:
                params['glossaryTerms'] = glossary_terms
            if disaggregations:
                params['disaggregations'] = disaggregations
            if version:
                params['version'] = version

            result = self._make_request('/definitions/indicators', params)
            return result

        except Exception as e:
            return UISError('list_indicators', str(e)).to_dict()

    # ==================== VERSION ENDPOINTS ====================

    # ==================== CONVENIENCE METHODS ====================

def main():
    """CLI interface for UNESCO UIS Data Fetcher"""
    if len(sys.argv) < 2:
        print(json.dumps({
            "error": "Usage: python unesco_data.py <command> <args>",
            "available_commands": [
                "list_indicators [options]",
            ],
            "themes": ["EDUCATION", "SCIENCE_TECHNOLOGY_INNOVATION", "CULTURE", "DEMOGRAPHIC_SOCIOECONOMIC"],
            "formats": ["csv", "excel", "json"]
        }))
        sys.exit(1)

    command = sys.argv[1]
    wrapper = UISWrapper()

    try:
        if command == "fetch":
            # Frontend integration command: fetch <indicator_code> <country_code> [start_year] [end_year]
            # Returns normalized { success, data: [{date, value}], metadata }
            if len(sys.argv) < 4:
                print(json.dumps({"error": "Usage: fetch <indicator_code> <country_code> [start_year] [end_year]"}))
                sys.exit(1)

            indicator_code = sys.argv[2]
            country_code = sys.argv[3]  # 3-letter ISO code (e.g. USA, GBR, IND)
            start_year = int(sys.argv[4]) if len(sys.argv) > 4 else None
            end_year = int(sys.argv[5]) if len(sys.argv) > 5 else None

            # Fetch directly with correct lowercase 'true' for indicatorMetadata
            params = {'indicator': [indicator_code], 'geoUnit': [country_code], 'indicatorMetadata': 'true'}
            if start_year:
                params['start'] = start_year
            if end_year:
                params['end'] = end_year
            result = wrapper._make_request('/data/indicators', params)

            if not result.get("success"):
                print(json.dumps(result))
                sys.exit(1)

            records = result.get("data", {}).get("records", []) if isinstance(result.get("data"), dict) else result.get("data", [])

            # Check for hints (API warnings about invalid codes)
            hints = []
            if isinstance(result.get("data"), dict):
                hints = result["data"].get("hints", [])
            if not records and hints:
                hint_msgs = [h.get("message", "") for h in hints]
                print(json.dumps({"success": False, "error": "API warning: " + "; ".join(hint_msgs)}))
                sys.exit(1)

            # Normalize records to [{date, value}]
            data_points = []
            indicator_name = indicator_code
            for rec in records:
                year = rec.get("year")
                value = rec.get("value")
                if year is not None and value is not None:
                    try:
                        val = float(value)
                    except (ValueError, TypeError):
                        continue
                    data_points.append({"date": str(year), "value": val})
                if not indicator_name or indicator_name == indicator_code:
                    indicator_name = rec.get("indicatorId", indicator_code)

            # Get indicator name from metadata if available
            ind_metadata = result.get("data", {}).get("indicatorMetadata", []) if isinstance(result.get("data"), dict) else []
            if ind_metadata and len(ind_metadata) > 0:
                meta_name = ind_metadata[0].get("name")
                if meta_name:
                    indicator_name = meta_name

            # Sort by date
            data_points.sort(key=lambda x: x["date"])

            print(json.dumps({
                "success": True,
                "data": data_points,
                "metadata": {
                    "indicator": indicator_code,
                    "indicator_name": indicator_name,
                    "country": country_code,
                    "source": "UNESCO UIS"
                }
            }))

        elif command == "list_indicators":
            glossary_terms = False
            disaggregations = False
            version = None

            i = 2
            while i < len(sys.argv):
                arg = sys.argv[i]
                if arg == '--glossary':
                    glossary_terms = True
                elif arg == '--disaggregations':
                    disaggregations = True
                elif arg == '--version' and i + 1 < len(sys.argv):
                    version = sys.argv[i + 1]
                    i += 1
                i += 1

            result = wrapper.list_indicators(
                glossary_terms=glossary_terms,
                disaggregations=disaggregations,
                version=version
            )
            print(json.dumps(result, indent=2))

        else:
            print(json.dumps({
                "error": f"Unknown command: {command}",
                "available_commands": [
                    "list_indicators [options]",
                ]
            }))
            sys.exit(1)

    except KeyboardInterrupt:
        print(json.dumps({"error": "Operation cancelled by user"}))
        sys.exit(1)
    except Exception as e:
        print(json.dumps({"error": f"Unexpected error: {str(e)}"}))
        sys.exit(1)


if __name__ == "__main__":
    main()