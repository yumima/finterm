#!/usr/bin/env python3
"""
Data.gov.hk API Wrapper
A comprehensive wrapper for Hong Kong Government Data Portal APIs

Provides access to:
1. CKAN Catalog API - for dataset metadata and categories
2. Historical Archive API - for historical data files

Features:
- Multi-language support with English translation
- Modular endpoint design
- Comprehensive error handling
- Independent endpoint failure handling
- Rich metadata and response formatting
"""

import sys
import json
import requests
import os
from datetime import datetime
from typing import Dict, List, Optional, Any, Union
from urllib.parse import urlencode

class DataGovHKWrapper:
    """
    Comprehensive wrapper for Data.gov.hk APIs
    """

    def __init__(self, language: str = "en"):
        """
        Initialize the Data.gov.hk API wrapper

        Args:
            language: Language preference - 'en' (English), 'tc' (Traditional Chinese), 'sc' (Simplified Chinese)
        """
        self.language = language.lower()

        # Base URLs for different languages
        self.base_urls = {
            'en': 'https://data.gov.hk/en-data/api/3/action/',
            'tc': 'https://data.gov.hk/tc-data/api/3/action/',
            'sc': 'https://data.gov.hk/sc-data/api/3/action/'
        }

        # Historical Archive API (language independent)
        self.historical_base_url = 'https://app.data.gov.hk'

        # Validate language
        if self.language not in self.base_urls:
            raise ValueError(f"Unsupported language: {language}. Use 'en', 'tc', or 'sc'")

        self.base_url = self.base_urls[self.language]

        # Session configuration
        self.session = requests.Session()
        self.session.headers.update({
            'User-Agent': 'finterm-Terminal/1.0',
            'Accept': 'application/json',
            'Accept-Language': f'{self.language}-HK,{self.language};q=0.9,en;q=0.8'
        })

        self.timeout = 30

    def _make_ckan_request(self, endpoint: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """
        Make a request to CKAN API endpoint

        Args:
            endpoint: CKAN action endpoint name
            params: Query parameters

        Returns:
            Standardized response dictionary
        """
        try:
            url = f"{self.base_url}{endpoint}"

            # Clean up params (remove None values)
            clean_params = {k: v for k, v in (params or {}).items() if v is not None}

            response = self.session.get(url, params=clean_params, timeout=self.timeout)
            response.raise_for_status()

            data = response.json()

            # CKAN API standard response format: {success: bool, result: any, help: string}
            if data.get('success'):
                return {
                    "data": data.get('result', []),
                    "metadata": {
                        "source": "Data.gov.hk CKAN API",
                        "endpoint": endpoint,
                        "language": self.language,
                        "last_updated": datetime.now().isoformat(),
                        "request_params": clean_params,
                        "help": data.get('help', '')
                    },
                    "error": None
                }
            else:
                return {
                    "data": [],
                    "metadata": {
                        "source": "Data.gov.hk CKAN API",
                        "endpoint": endpoint,
                        "language": self.language,
                        "last_updated": datetime.now().isoformat(),
                        "request_params": clean_params
                    },
                    "error": data.get('error', {}).get('message', 'Unknown CKAN API error')
                }

        except requests.exceptions.Timeout:
            return {
                "data": [],
                "metadata": {"source": "Data.gov.hk CKAN API", "endpoint": endpoint},
                "error": "Request timeout. The API took too long to respond."
            }
        except requests.exceptions.ConnectionError:
            return {
                "data": [],
                "metadata": {"source": "Data.gov.hk CKAN API", "endpoint": endpoint},
                "error": "Connection error. Unable to connect to Data.gov.hk API."
            }
        except requests.exceptions.HTTPError as e:
            status_code = e.response.status_code
            error_messages = {
                400: "Bad request. Invalid parameters.",
                404: "Endpoint not found.",
                429: "Rate limit exceeded. Please try again later.",
                500: "Internal server error."
            }
            error_msg = error_messages.get(status_code, f"HTTP Error {status_code}")
            return {
                "data": [],
                "metadata": {"source": "Data.gov.hk CKAN API", "endpoint": endpoint},
                "error": error_msg
            }
        except json.JSONDecodeError:
            return {
                "data": [],
                "metadata": {"source": "Data.gov.hk CKAN API", "endpoint": endpoint},
                "error": "Invalid JSON response from API."
            }
        except Exception as e:
            return {
                "data": [],
                "metadata": {"source": "Data.gov.hk CKAN API", "endpoint": endpoint},
                "error": f"Unexpected error: {str(e)}"
            }

    def _translate_text_if_needed(self, text: str) -> str:
        """
        Simple translation for common Chinese terms to English
        In a real implementation, you might use a translation service

        Args:
            text: Text to translate

        Returns:
            Translated text if it's Chinese, original text if English
        """
        if self.language == 'en':
            return text

        # Common translations for data.gov.hk
        translations = {
            # Traditional Chinese
            '環境': 'Environment',
            '交通': 'Transportation',
            '政府': 'Government',
            '經濟': 'Economy',
            '社會': 'Social',
            '教育': 'Education',
            '醫療': 'Healthcare',
            '房屋': 'Housing',
            '規劃': 'Planning',
            '統計': 'Statistics',
            # Simplified Chinese
            '环境': 'Environment',
            '交通': 'Transportation',
            '政府': 'Government',
            '经济': 'Economy',
            '社会': 'Social',
            '教育': 'Education',
            '医疗': 'Healthcare',
            '房屋': 'Housing',
            '规划': 'Planning',
            '统计': 'Statistics'
        }

        return translations.get(text, text)

    # === CATALOGUE/CATEGORY ENDPOINTS ===

    def get_categories_list(self) -> Dict[str, Any]:
        """
        Get list of all available categories (groups)

        Returns:
            List of category names
        """
        return self._make_ckan_request('group_list')

    def get_category_details(self, category_id: str) -> Dict[str, Any]:
        """
        Get detailed information about a specific category including datasets

        Args:
            category_id: Unique category identifier (e.g., 'environment', 'transport')

        Returns:
            Category details with datasets list
        """
        if not category_id:
            return {
                "data": {},
                "metadata": {"source": "Data.gov.hk CKAN API"},
                "error": "Category ID is required"
            }

        return self._make_ckan_request('group_show', {'id': category_id})

    # === DATASET ENDPOINTS ===

    def get_datasets_list(self, limit: Optional[int] = None, offset: Optional[int] = None) -> Dict[str, Any]:
        """
        Get list of all datasets (packages) with pagination

        Args:
            limit: Maximum number of datasets to return
            offset: Number of datasets to skip (for pagination)

        Returns:
            List of dataset names
        """
        params = {}
        if limit is not None:
            params['limit'] = limit
        if offset is not None:
            params['offset'] = offset

        return self._make_ckan_request('package_list', params)

    def get_dataset_details(self, dataset_id: str) -> Dict[str, Any]:
        """
        Get full metadata for a specific dataset

        Args:
            dataset_id: Unique dataset identifier or name

        Returns:
            Complete dataset metadata including resources, tags, etc.
        """
        if not dataset_id:
            return {
                "data": {},
                "metadata": {"source": "Data.gov.hk CKAN API"},
                "error": "Dataset ID is required"
            }

        return self._make_ckan_request('package_show', {'id': dataset_id})

    # === HISTORICAL ARCHIVE ENDPOINTS ===

    # === COMPOSITE METHODS ===

    def get_all_category_datasets(self, category_id: str) -> Dict[str, Any]:
        """
        Get all datasets within a specific category

        Args:
            category_id: Category identifier

        Returns:
            All datasets in the category with enhanced metadata
        """
        result = {
            "success": True,
            "timestamp": datetime.now().isoformat(),
            "category_id": category_id,
            "datasets": {},
            "failed_datasets": []
        }

        try:
            # Get category details
            category_result = self.get_category_details(category_id)
            if category_result.get('error'):
                return {
                    "success": False,
                    "error": category_result['error'],
                    "category_id": category_id
                }

            category_data = category_result.get('data', {})
            packages = category_data.get('packages', [])

            result["category_info"] = {
                "name": self._translate_text_if_needed(category_id),
                "title": category_data.get('title', category_id),
                "description": category_data.get('description', ''),
                "dataset_count": len(packages)
            }

            # Get details for each dataset
            for package in packages:
                package_name = package.get('name', '')
                if package_name:
                    try:
                        dataset_result = self.get_dataset_details(package_name)
                        if not dataset_result.get('error'):
                            result["datasets"][package_name] = {
                                "name": package_name,
                                "title": dataset_result['data'].get('title', ''),
                                "notes": dataset_result['data'].get('notes', ''),
                                "resources": dataset_result['data'].get('resources', []),
                                "tags": [tag.get('display_name', '') for tag in dataset_result['data'].get('tags', [])],
                                "metadata": dataset_result['data']
                            }
                        else:
                            result["failed_datasets"].append({
                                "dataset": package_name,
                                "error": dataset_result['error']
                            })
                    except Exception as e:
                        result["failed_datasets"].append({
                            "dataset": package_name,
                            "error": str(e)
                        })

            result["success"] = len(result["failed_datasets"]) < len(packages)

        except Exception as e:
            return {
                "success": False,
                "error": f"Failed to retrieve category datasets: {str(e)}",
                "category_id": category_id
            }

        result["metadata"] = {
            "source": "Data.gov.hk API",
            "language": self.language,
            "operation": "get_all_category_datasets",
            "last_updated": datetime.now().isoformat()
        }

        return result


def main():
    """
    CLI interface for Data.gov.hk API wrapper
    """
    if len(sys.argv) < 2:
        print(json.dumps({
            "error": "Usage: python data_gov_hk_api.py <command> [args]",
            "available_commands": [
                "categories_list",
                "datasets_list [limit] [offset]",
                "dataset_details <dataset_id>",
                "category_datasets <category_id>",
            ]
        }, indent=2))
        sys.exit(1)

    command = sys.argv[1]

    # Aliases used by the C++ GovDataProviderPanel
    _hk_alias_org_id = None
    _hk_alias_dataset_id = None
    if command == "publishers":
        command = "categories_list"
    elif command == "datasets":
        # datasets <org_id> <limit> -> category_datasets <category_id>
        command = "category_datasets"
        if len(sys.argv) > 2:
            _hk_alias_org_id = sys.argv[2]
    elif command == "resources":
        # resources <dataset_id> -> dataset_details <dataset_id>
        command = "dataset_details"
        if len(sys.argv) > 2:
            _hk_alias_dataset_id = sys.argv[2]
    elif command == "search":
        # search <query> <limit> -> datasets_list (HK has no text search, use datasets_list)
        command = "datasets_list"

    # Initialize wrapper with English language by default
    language = os.environ.get('DATA_GOV_HK_LANGUAGE', 'en')
    wrapper = DataGovHKWrapper(language=language)

    try:
        if command == "categories_list":
            result = wrapper.get_categories_list()

        elif command == "datasets_list":
            limit = int(sys.argv[2]) if len(sys.argv) > 2 else None
            offset = int(sys.argv[3]) if len(sys.argv) > 3 else None
            result = wrapper.get_datasets_list(limit=limit, offset=offset)

        elif command == "dataset_details":
            dataset_id = _hk_alias_dataset_id or (sys.argv[2] if len(sys.argv) > 2 else None)
            if not dataset_id:
                print(json.dumps({"error": "Dataset ID is required"}))
                sys.exit(1)
            result = wrapper.get_dataset_details(dataset_id)

        elif command == "category_datasets":
            category_id = _hk_alias_org_id or (sys.argv[2] if len(sys.argv) > 2 else None)
            if not category_id:
                print(json.dumps({"error": "Category ID is required"}))
                sys.exit(1)
            result = wrapper.get_all_category_datasets(category_id)

        else:
            result = {"error": f"Unknown command: {command}"}

        # Normalize categories_list: convert plain string list to objects expected by C++ populate_orgs
        if command == "categories_list" and isinstance(result, dict):
            raw_data = result.get("data", [])
            if raw_data and isinstance(raw_data[0], str):
                result["data"] = [{"id": s, "name": s, "display_name": s.replace("-", " ").title(), "dataset_count": -1}
                                  for s in raw_data]

        # Normalize category_datasets: convert {datasets:{id:obj,...}} to {data:[...], metadata:{}}
        if command == "category_datasets" and isinstance(result, dict) and "datasets" in result:
            ds_map = result.get("datasets", {})
            items = []
            for ds_id, ds_obj in ds_map.items():
                if isinstance(ds_obj, dict):
                    ds_obj.setdefault("id", ds_id)
                    ds_obj.setdefault("name", ds_id)
                    ds_obj.setdefault("num_resources", len(ds_obj.get("resources", [])))
                    ds_obj.setdefault("metadata_modified", ds_obj.get("last_modified", ""))
                    items.append(ds_obj)
            result = {"data": items, "metadata": {"total_count": len(items)}, "error": None}

        # Normalize dataset_details: wrap resources list into {data:[...]}
        if command == "dataset_details" and isinstance(result, dict):
            resources = result.get("resources", [])
            normalized = []
            for r in resources:
                if isinstance(r, dict):
                    r.setdefault("name", r.get("id", ""))
                    r.setdefault("format", r.get("format", ""))
                    r.setdefault("url", r.get("url", ""))
                    r.setdefault("last_modified", r.get("last_modified", ""))
                    normalized.append(r)
            result = {"data": normalized, "metadata": {"total_count": len(normalized)}, "error": None}

        sys.stdout = open(sys.stdout.fileno(), mode='w', encoding='utf-8', errors='replace', closefd=False)
        print(json.dumps(result, indent=2, ensure_ascii=False))

    except ValueError as e:
        print(json.dumps({"error": f"Invalid parameter: {str(e)}"}))
        sys.exit(1)
    except Exception as e:
        print(json.dumps({"error": f"Command execution failed: {str(e)}"}))
        sys.exit(1)


if __name__ == "__main__":
    main()