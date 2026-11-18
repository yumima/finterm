"""
BEA (Bureau of Economic Analysis) Data Fetcher
Comprehensive wrapper for BEA Data Retrieval API providing access to
National, Regional, Industry and International economic data

API Documentation:
- Base URL: https://apps.bea.gov/api/data/
- Authentication: API key required
- Rate limits: None specified but be reasonable with requests
- Registration: https://www.bea.gov/data/api/register

Supported Datasets:
- NIPA: National Income and Product Accounts
- NIUnderlyingDetail: NIPA Underlying Detail
- FixedAssets: Fixed Assets
- MNE: Multinational Enterprises
- GDPbyIndustry: GDP by Industry
- ITA: International Transactions
- IIP: International Investment Position
- InputOutput: Input-Output Accounts
- UnderlyingGDPbyIndustry: GDP by Industry - Underlying Detail
- IntlServTrade: International Services Trade
- Regional: Regional Economic Accounts
"""

import sys
import json
import os
import requests
from datetime import datetime, timedelta
from typing import Dict, Any, Optional, List, Union
from urllib.parse import urlencode


class BEAError:
    """Error handling wrapper for BEA API responses"""
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


class BEAWrapper:
    """Comprehensive BEA API wrapper with fault tolerance"""

    def __init__(self, api_key: Optional[str] = None):
        self.api_key = api_key or os.environ.get('BEA_API_KEY', '')
        self.base_url = "https://apps.bea.gov/api/data/"
        self.session = requests.Session()
        self.session.headers.update({
            'User-Agent': 'finterm-Terminal/1.0'
        })

    def _make_request(self, method: str, params: Dict[str, Any]) -> Dict[str, Any]:
        """Centralized request handler with comprehensive error handling"""
        try:
            # Add API key to all requests
            params['UserID'] = self.api_key
            params['method'] = method
            params['resultformat'] = 'JSON'

            # Add Year parameter if not specified (default to most recent)
            if 'Year' not in params and method.startswith('GetData'):
                current_year = datetime.now().year
                params['Year'] = str(current_year)

            url = f"{self.base_url}?{urlencode(params)}"

            response = self.session.get(url, timeout=30)
            response.raise_for_status()

            data = response.json()

            # Check for BEA API errors
            if 'BEAAPI' in data:
                if 'Error' in data['BEAAPI']:
                    error_desc = data['BEAAPI']['Error'].get('ErrorDesc', 'Unknown BEA API error')
                    return BEAError(method, error_desc).to_dict()

                # Extract actual data
                results = data['BEAAPI'].get('Results', {})

                # Handle different response structures
                if method == 'GetDatasetList':
                    return {
                        "success": True,
                        "endpoint": method,
                        "data": results.get('Dataset', []),
                        "timestamp": int(datetime.now().timestamp())
                    }

                elif method == 'GetParameterList':
                    return {
                        "success": True,
                        "endpoint": method,
                        "data": results.get('Parameter', []),
                        "dataset_name": params.get('DatasetName', ''),
                        "timestamp": int(datetime.now().timestamp())
                    }

                elif method in ['GetParameterValues', 'GetParameterValuesFiltered']:
                    return {
                        "success": True,
                        "endpoint": method,
                        "data": results.get('ParamValue', []),
                        "parameter": params.get('ParameterName', ''),
                        "dataset_name": params.get('DatasetName', ''),
                        "timestamp": int(datetime.now().timestamp())
                    }

                else:  # GetData methods
                    return {
                        "success": True,
                        "endpoint": method,
                        "data": results.get('Data', []),
                        "dataset_name": params.get('DatasetName', ''),
                        "parameters": {
                            k: v for k, v in params.items()
                            if k not in ['UserID', 'method', 'resultformat']
                        },
                        "notes": results.get('Notes', []),
                        "statistics": results.get('Stat', []),
                        "dimensions": results.get('Dimensions', []),
                        "timestamp": int(datetime.now().timestamp())
                    }

            return BEAError(method, "Unexpected response format").to_dict()

        except requests.exceptions.RequestException as e:
            return BEAError(method, f"Network error: {str(e)}").to_dict()
        except json.JSONDecodeError as e:
            return BEAError(method, f"JSON decode error: {str(e)}").to_dict()
        except Exception as e:
            return BEAError(method, f"Unexpected error: {str(e)}").to_dict()

    # ==================== METADATA ENDPOINTS ====================

    # ==================== DATA RETRIEVAL ENDPOINTS ====================

    def get_nipa_data(self, table_name: str, frequency: str = 'A', year: str = None,
                     year_range: str = None) -> Dict[str, Any]:
        """Get National Income and Product Accounts data"""
        try:
            if not table_name:
                return BEAError('NIPA', 'TableName is required').to_dict()

            params = {
                'DatasetName': 'NIPA',
                'TableName': table_name,
                'Frequency': frequency
            }

            if year:
                params['Year'] = year
            elif year_range:
                params['Year'] = year_range
            elif frequency == 'Q':
                # Default to recent quarters for quarterly data
                params['Year'] = f"{datetime.now().year-1}Q1,{datetime.now().year}Q4"

            result = self._make_request('GetData', params)
            return result

        except Exception as e:
            return BEAError('NIPA', str(e)).to_dict()

    def get_gdp_by_industry(self, table_id: str, year: str = None,
                           frequency: str = 'A', industry: str = 'ALL') -> Dict[str, Any]:
        """Get GDP by Industry data"""
        try:
            if not table_id:
                return BEAError('GDPbyIndustry', 'TableID is required').to_dict()

            params = {
                'DatasetName': 'GDPbyIndustry',
                'TableID': table_id,
                'Frequency': frequency,
                'Year': year,
                'Industry': industry
            }

            result = self._make_request('GetData', params)
            return result

        except Exception as e:
            return BEAError('GDPbyIndustry', str(e)).to_dict()

    def get_international_transactions(self, indicator: str = None, area_or_country: str = 'AllCountries',
                                     frequency: str = 'A', year: str = None) -> Dict[str, Any]:
        """Get International Transactions Accounts data"""
        try:
            params = {
                'DatasetName': 'ITA',
                'AreaOrCountry': area_or_country,
                'Frequency': frequency,
                'Year': year
            }

            if indicator:
                params['Indicator'] = indicator

            result = self._make_request('GetData', params)
            return result

        except Exception as e:
            return BEAError('ITA', str(e)).to_dict()

    def get_regional_data(self, table_name: str, line_code: str = 'ALL',
                         geo_fips: str = 'STATE', year: str = None) -> Dict[str, Any]:
        """Get Regional Economic Accounts data"""
        try:
            if not table_name:
                return BEAError('Regional', 'TableName is required').to_dict()

            params = {
                'DatasetName': 'Regional',
                'TableName': table_name,
                'LineCode': line_code,
                'GeoFIPS': geo_fips
            }

            if year:
                params['Year'] = year

            result = self._make_request('GetData', params)
            return result

        except Exception as e:
            return BEAError('Regional', str(e)).to_dict()

    # ==================== COMPOSITE METHODS ====================

    def get_economic_overview(self, year: str = None) -> Dict[str, Any]:
        """Get comprehensive economic overview from multiple datasets"""
        result = {
            "success": True,
            "overview_type": "economic_overview",
            "year": year or str(datetime.now().year),
            "timestamp": int(datetime.now().timestamp()),
            "datasets": {},
            "failed_datasets": []
        }

        # Define datasets to include in overview
        overview_datasets = [
            ('NIPA GDP', lambda: self.get_nipa_data('T10101', 'Q', year)),
            ('GDP by Industry', lambda: self.get_gdp_by_industry('1', year, 'A')),
            ('International Transactions', lambda: self.get_international_transactions('BalGds', 'AllCountries', 'A', year)),
            ('Regional Data', lambda: self.get_regional_data('SAINC1', '1', 'STATE', year))
        ]

        overall_success = False

        for dataset_name, dataset_func in overview_datasets:
            try:
                dataset_result = dataset_func()
                result["datasets"][dataset_name] = dataset_result

                if dataset_result.get("success"):
                    overall_success = True
                else:
                    result["failed_datasets"].append({
                        "dataset": dataset_name,
                        "error": dataset_result.get("error", "Unknown error")
                    })

            except Exception as e:
                result["failed_datasets"].append({
                    "dataset": dataset_name,
                    "error": str(e)
                })

        result["success"] = overall_success
        return result

def main(args=None):
    
    if args is None:
        args = sys.argv[1:]
    """CLI interface for BEA Data Fetcher"""
    if len(args) + 1 < 2:
        print(json.dumps({
            "error": "Usage: python bea_data.py <command> <args>",
            "available_commands": [
                "regional <table_name> [line_code] [geo_fips] [year]",
                "economic_overview [year]",
            ]
        }))
        sys.exit(1)

    command = args[0]
    wrapper = BEAWrapper()

    try:
        if command == "fetch":
            # Frontend integration command: fetch <indicator_id> <start_date> <end_date>
            if len(args) < 4:
                print(json.dumps({"success": False, "error": "Usage: bea_data.py fetch <indicator_id> <start_date> <end_date>"}))
                sys.exit(1)

            indicator_id = args[1]
            start_date = args[2]  # YYYY-MM-DD
            end_date = args[3]    # YYYY-MM-DD

            # Map indicator IDs to NIPA table + line number
            INDICATOR_MAP = {
                "gdp_growth": {"table": "T10101", "line": "1", "name": "Real GDP Growth (% Change)"},
                "nominal_gdp": {"table": "T10105", "line": "1", "name": "Nominal GDP (Billions $)"},
                "real_gdp": {"table": "T10106", "line": "1", "name": "Real GDP (Chained 2017 $, Billions)"},
                "gdp_deflator": {"table": "T10104", "line": "1", "name": "GDP Price Index"},
                "gdp_price_change": {"table": "T10107", "line": "1", "name": "GDP Price Change (%)"},
                "pce": {"table": "T10105", "line": "2", "name": "Personal Consumption Expenditures (Billions $)"},
                "pce_goods": {"table": "T10105", "line": "3", "name": "PCE Goods (Billions $)"},
                "pce_services": {"table": "T10105", "line": "6", "name": "PCE Services (Billions $)"},
                "gross_investment": {"table": "T10105", "line": "7", "name": "Gross Private Domestic Investment (Billions $)"},
                "fixed_investment": {"table": "T10105", "line": "8", "name": "Fixed Investment (Billions $)"},
                "net_exports": {"table": "T10105", "line": "15", "name": "Net Exports (Billions $)"},
                "exports": {"table": "T10105", "line": "16", "name": "Exports of Goods & Services (Billions $)"},
                "imports": {"table": "T10105", "line": "19", "name": "Imports of Goods & Services (Billions $)"},
                "govt_spending": {"table": "T10105", "line": "22", "name": "Government Spending (Billions $)"},
                "federal_spending": {"table": "T10105", "line": "23", "name": "Federal Government Spending (Billions $)"},
                "defense_spending": {"table": "T10105", "line": "24", "name": "National Defense Spending (Billions $)"},
                "personal_income": {"table": "T20100", "line": "1", "name": "Personal Income (Billions $)"},
                "compensation": {"table": "T20100", "line": "2", "name": "Compensation of Employees (Billions $)"},
                "wages_salaries": {"table": "T20100", "line": "3", "name": "Wages and Salaries (Billions $)"},
                "disposable_income": {"table": "T20100", "line": "27", "name": "Disposable Personal Income (Billions $)"},
                "personal_saving": {"table": "T20100", "line": "34", "name": "Personal Saving (Billions $)"},
                "saving_rate": {"table": "T20100", "line": "35", "name": "Personal Saving Rate (%)"},
                "pce_inflation": {"table": "T20301", "line": "1", "name": "PCE Price Index (% Change)"},
                "core_pce_inflation": {"table": "T20301", "line": "25", "name": "Core PCE Price Index (% Change, ex Food & Energy)"},
                "gdp_per_capita": {"table": "T70100", "line": "1", "name": "GDP per Capita (Current $)"},
                "govt_receipts": {"table": "T30100", "line": "1", "name": "Government Current Receipts (Billions $)"},
                "personal_taxes": {"table": "T30100", "line": "3", "name": "Personal Current Taxes (Billions $)"},
                "corporate_taxes": {"table": "T30100", "line": "5", "name": "Taxes on Corporate Income (Billions $)"},
                "current_account": {"table": "T40100", "line": "33", "name": "Current Account Balance (Billions $)"},
                "gross_saving": {"table": "T50100", "line": "1", "name": "Gross Saving (Billions $)"},
                "net_saving": {"table": "T50100", "line": "2", "name": "Net Saving (Billions $)"},
                "gdi": {"table": "T11000", "line": "1", "name": "Gross Domestic Income (Billions $)"},
            }

            if indicator_id not in INDICATOR_MAP:
                print(json.dumps({"success": False, "error": f"Unknown indicator: {indicator_id}", "available": list(INDICATOR_MAP.keys())}))
                sys.exit(1)

            config = INDICATOR_MAP[indicator_id]
            start_year = int(start_date[:4])
            end_year = int(end_date[:4])

            # Build year list
            years = ','.join(str(y) for y in range(start_year, end_year + 1))

            params = {
                'DatasetName': 'NIPA',
                'TableName': config['table'],
                'Frequency': 'A',
                'Year': years,
            }
            result = wrapper._make_request('GetData', params)

            if not result.get('success'):
                print(json.dumps(result))
                sys.exit(1)

            # Filter by LineNumber
            target_line = config['line']
            data_points = []
            for row in result.get('data', []):
                if row.get('LineNumber') == target_line:
                    try:
                        val_str = row.get('DataValue', '').replace(',', '')
                        if val_str and val_str not in ('...', '(NA)', 'n.a.'):
                            value = float(val_str)
                            period = row.get('TimePeriod', '')
                            data_points.append({"date": period, "value": value})
                    except (ValueError, TypeError):
                        continue

            # Sort by date
            data_points.sort(key=lambda x: x['date'])

            print(json.dumps({
                "success": True,
                "data": data_points,
                "metadata": {
                    "indicator": indicator_id,
                    "indicator_name": config['name'],
                    "country": "United States",
                    "source": "BEA NIPA",
                    "table": config['table'],
                    "line": config['line'],
                }
            }))
            sys.exit(0)

        elif command == "regional":
            if len(args) + 1 < 3:
                print(json.dumps({"error": "Usage: python bea_data.py regional <table_name> [line_code] [geo_fips] [year]"}))
                sys.exit(1)

            table_name = args[1]
            line_code = args[2] if len(args) + 1 > 3 else 'ALL'
            geo_fips = args[3] if len(args) + 1 > 4 else 'STATE'
            year = args[4] if len(args) + 1 > 5 else None
            result = wrapper.get_regional_data(table_name, line_code, geo_fips, year)
            print(json.dumps(result, indent=2))

        elif command == "economic_overview":
            year = args[1] if len(args) + 1 > 2 else None
            result = wrapper.get_economic_overview(year)
            print(json.dumps(result, indent=2))

        else:
            print(json.dumps({
                "error": f"Unknown command: {command}",
                "available_commands": [
                    "regional <table_name> [line_code] [geo_fips] [year]",
                    "economic_overview [year]",
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