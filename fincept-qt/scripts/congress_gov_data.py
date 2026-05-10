"""
Congress.gov Data Fetcher
Modular, fault-tolerant wrapper for US Congressional data based on OpenBB congress_gov provider
Sources: Congress.gov API
Each endpoint works independently with isolated error handling
"""

import os
import sys
import json
import requests
import asyncio
import base64
from datetime import datetime, timedelta, date
from typing import Dict, Any, Optional, List, Union
from io import BytesIO
import re
import math

# Congress.gov API Constants
BASE_URL = "https://api.congress.gov/v3/"
BILL_TYPES = ["hr", "s", "hjres", "sjres", "hconres", "sconres", "hres", "sres"]

BILL_TYPE_OPTIONS = [
    {"label": "House Bill", "value": "hr"},
    {"label": "Senate Bill", "value": "s"},
    {"label": "House Joint Resolution", "value": "hjres"},
    {"label": "Senate Joint Resolution", "value": "sjres"},
    {"label": "House Concurrent Resolution", "value": "hconres"},
    {"label": "Senate Concurrent Resolution", "value": "sconres"},
    {"label": "House Simple Resolution", "value": "hres"},
    {"label": "Senate Simple Resolution", "value": "sres"},
]

class CongressGovError:
    """Error handling wrapper for Congress.gov API responses"""
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

class CongressGovWrapper:
    """Modular Congress.gov API wrapper with fault tolerance"""

    def __init__(self, api_key: Optional[str] = None):
        self.api_key = api_key or os.environ.get('CONGRESS_GOV_API_KEY', '')
        self.session = requests.Session()
        self.session.headers.update({
            'User-Agent': 'Fincept-Terminal/1.0',
            'Accept': 'application/json'
        })

    def _make_request(self, url: str, method: str = 'GET', params: Optional[Dict] = None) -> Dict[str, Any]:
        """Make HTTP request with comprehensive error handling"""
        try:
            if not self.api_key:
                return {"error": "Congress.gov API key not configured. Please set CONGRESS_GOV_API_KEY environment variable.", "api_key_required": True}

            # Add API key to URL
            if '?' in url:
                url += f"&api_key={self.api_key}"
            else:
                url += f"?api_key={self.api_key}"

            response = self.session.request(method=method, url=url, params=params, timeout=30)
            response.raise_for_status()

            try:
                data = response.json()
                return {"success": True, "data": data}
            except json.JSONDecodeError:
                return {"error": "Invalid JSON response", "json_error": True}

        except requests.exceptions.Timeout:
            return {"error": "Request timeout", "timeout": True, "status_code": None}
        except requests.exceptions.ConnectionError:
            return {"error": "Connection error", "connection_error": True, "status_code": None}
        except requests.exceptions.HTTPError as e:
            if response.status_code == 401:
                return {"error": "Invalid API key", "auth_error": True, "status_code": response.status_code}
            elif response.status_code == 403:
                return {"error": "Rate limit exceeded", "rate_limit_error": True, "status_code": response.status_code}
            elif response.status_code == 404:
                return {"error": "Resource not found", "not_found": True, "status_code": response.status_code}
            else:
                return {"error": f"HTTP error: {e}", "http_error": True, "status_code": response.status_code}
        except requests.exceptions.RequestException as e:
            return {"error": f"Request error: {e}", "request_error": True, "status_code": None}
        except Exception as e:
            return {"error": f"Unexpected error: {e}", "general_error": True, "status_code": None}

    def _year_to_congress(self, year: int) -> int:
        """Map a year (1935-present) to the corresponding U.S. Congress number."""
        if year < 1935:
            raise ValueError("Year must be 1935 or later.")
        # 74th Congress started in 1935
        congress_number = 74 + ((year - 1935) // 2)
        return congress_number

    def _get_congress_from_params(self, start_date: Optional[str] = None,
                                end_date: Optional[str] = None,
                                congress: Optional[int] = None) -> int:
        """Determine congress number from various parameters"""
        if congress:
            return congress

        current_year = datetime.now().year
        current_congress = self._year_to_congress(current_year)

        if start_date and not end_date:
            try:
                start_year = datetime.fromisoformat(start_date).year
                return self._year_to_congress(start_year)
            except:
                return current_congress
        elif end_date and not start_date:
            try:
                end_year = datetime.fromisoformat(end_date).year
                return self._year_to_congress(end_year)
            except:
                return current_congress
        else:
            return current_congress

    # ===== CONGRESS BILLS ENDPOINT =====

    def get_congress_bills(self, congress: Optional[int] = None,
                          bill_type: Optional[str] = None,
                          start_date: Optional[str] = None,
                          end_date: Optional[str] = None,
                          limit: Optional[int] = None,
                          offset: Optional[int] = 0,
                          sort_by: str = "desc",
                          get_all: bool = False) -> Dict[str, Any]:
        """
        Get US Congressional bills

        Args:
            congress: Congress number (e.g., 118 for 118th Congress)
            bill_type: Bill type (hr, s, hjres, sjres, hconres, sconres, hres, sres)
            start_date: Start date in ISO format (YYYY-MM-DD)
            end_date: End date in ISO format (YYYY-MM-DD)
            limit: Maximum number of bills to return (default 100, max 250)
            offset: Number of results to skip
            sort_by: Sort order (asc/desc)
            get_all: If True, fetch all bills (ignores limit)
        """
        try:
            if bill_type and bill_type not in BILL_TYPES:
                return CongressGovError('congress_bills', f'Invalid bill_type: {bill_type}. Must be one of: {", ".join(BILL_TYPES)}').to_dict()

            # Determine congress number
            congress_num = self._get_congress_from_params(start_date, end_date, congress)

            # Build URL
            if bill_type:
                url = f"{BASE_URL}bill/{congress_num}/{bill_type}"
            else:
                url = f"{BASE_URL}bill/{congress_num}"

            # Build query parameters
            params = []

            if start_date:
                params.append(f"fromDateTime={start_date}T00:00:00Z")
            if end_date:
                params.append(f"toDateTime={end_date}T23:59:59Z")

            if get_all and bill_type and congress_num:
                # Fetch all bills for this type and congress
                return self._get_all_bills_by_type(congress_num, bill_type, start_date, end_date)

            limit_value = limit if limit is not None else 100
            if limit_value > 250:
                limit_value = 250

            params.append(f"limit={limit_value}")
            params.append(f"offset={offset}")
            params.append(f"sort=updateDate+{sort_by}")
            params.append("format=json")

            if params:
                url += "?" + "&".join(params)

            result = self._make_request(url)

            if "error" in result:
                return CongressGovError('congress_bills', result['error'], result.get('status_code')).to_dict()

            data = result.get('data', {})
            bills = data.get('bills', [])

            # Transform bills data
            transformed_bills = []
            for bill in bills:
                latest_action = bill.get('latestAction', {})
                transformed_bill = {
                    "congress": bill.get('congress'),
                    "bill_type": bill.get('type'),
                    "bill_number": bill.get('number'),
                    "bill_url": bill.get('url'),
                    "title": bill.get('title'),
                    "origin_chamber": bill.get('originChamber'),
                    "origin_chamber_code": bill.get('originChamberCode'),
                    "update_date": bill.get('updateDate'),
                    "update_date_including_text": bill.get('updateDateIncludingText'),
                    "latest_action_date": latest_action.get('actionDate'),
                    "latest_action": latest_action.get('text')
                }
                transformed_bills.append(transformed_bill)

            pagination = data.get('pagination', {})

            return {
                "success": True,
                "endpoint": "congress_bills",
                "congress": congress_num,
                "filters": {
                    "bill_type": bill_type,
                    "start_date": start_date,
                    "end_date": end_date,
                    "limit": limit_value,
                    "offset": offset,
                    "sort_by": sort_by,
                    "get_all": get_all
                },
                "total_bills": len(transformed_bills),
                "pagination": pagination,
                "bills": transformed_bills,
                "timestamp": int(datetime.now().timestamp())
            }

        except Exception as e:
            return CongressGovError('congress_bills', str(e)).to_dict()

    def _get_all_bills_by_type(self, congress: int, bill_type: str,
                             start_date: Optional[str] = None,
                             end_date: Optional[str] = None) -> Dict[str, Any]:
        """Fetch all bills of a specific type for a given congress"""
        try:
            all_bills = []
            limit = 250  # Max per request
            offset = 0
            total_fetched = 0

            while True:
                # Build URL for pagination
                url = f"{BASE_URL}bill/{congress}/{bill_type}"
                params = []

                if start_date:
                    params.append(f"fromDateTime={start_date}T00:00:00Z")
                if end_date:
                    params.append(f"toDateTime={end_date}T23:59:59Z")

                params.append(f"limit={limit}")
                params.append(f"offset={offset}")
                params.append("sort=updateDate+desc")
                params.append("format=json")

                if params:
                    url += "?" + "&".join(params)

                result = self._make_request(url)

                if "error" in result:
                    if total_fetched == 0:
                        return CongressGovError('congress_bills', result['error'], result.get('status_code')).to_dict()
                    break

                data = result.get('data', {})
                bills = data.get('bills', [])

                if not bills:
                    break

                # Transform bills
                for bill in bills:
                    latest_action = bill.get('latestAction', {})
                    transformed_bill = {
                        "congress": bill.get('congress'),
                        "bill_type": bill.get('type'),
                        "bill_number": bill.get('number'),
                        "bill_url": bill.get('url'),
                        "title": bill.get('title'),
                        "origin_chamber": bill.get('originChamber'),
                        "origin_chamber_code": bill.get('originChamberCode'),
                        "update_date": bill.get('updateDate'),
                        "update_date_including_text": bill.get('updateDateIncludingText'),
                        "latest_action_date": latest_action.get('actionDate'),
                        "latest_action": latest_action.get('text')
                    }
                    all_bills.append(transformed_bill)

                total_fetched += len(bills)

                # Check if we have all bills
                pagination = data.get('pagination', {})
                count = pagination.get('count', 0)

                if total_fetched >= count or len(bills) < limit:
                    break

                offset += limit

            return {
                "success": True,
                "endpoint": "congress_bills",
                "congress": congress,
                "bill_type": bill_type,
                "total_bills": len(all_bills),
                "bills": all_bills,
                "timestamp": int(datetime.now().timestamp())
            }

        except Exception as e:
            return CongressGovError('congress_bills', str(e)).to_dict()

    # ===== BILL INFO ENDPOINT =====

    def get_bill_info(self, bill_url: str) -> Dict[str, Any]:
        """
        Get detailed information about a specific bill

        Args:
            bill_url: Bill URL or bill identifier (e.g., "119/s/1947" or full URL)
        """
        try:
            # Normalize bill URL
            if bill_url[0].isdigit() or (bill_url[0] == "/" and len(bill_url) > 1 and bill_url[1].isdigit()):
                # Bill identifier provided (e.g., "119/s/1947")
                clean_identifier = bill_url[1:] if bill_url[0] == '/' else bill_url
                url = f"{BASE_URL}bill/{clean_identifier}?format=json"
            elif not bill_url.startswith(BASE_URL):
                # Relative URL provided
                url = f"{BASE_URL}bill/{bill_url}?format=json"
            else:
                # Full URL provided
                url = bill_url if '?' in bill_url else f"{bill_url}?format=json"

            # Get base bill info
            result = self._make_request(url)

            if "error" in result:
                return CongressGovError('bill_info', result['error'], result.get('status_code')).to_dict()

            data = result.get('data', {})
            bill_data = data.get('bill', {})

            if not bill_data:
                return CongressGovError('bill_info', 'Bill not found', 404).to_dict()

            # Fetch additional related data
            additional_data = {}

            # Fetch cosponsors
            cosponsors_data = bill_data.get('cosponsors', {})
            if cosponsors_data.get('count', 0) > 0:
                cosponsors_url = cosponsors_data.get('url', '').replace('?format=json', f'?format=json&api_key={self.api_key}')
                cosponsors_result = self._make_request(cosponsors_url)
                if cosponsors_result.get('success'):
                    additional_data['cosponsors'] = cosponsors_result['data'].get('cosponsors', [])

            # Fetch subjects
            subjects_data = bill_data.get('subjects', {})
            if subjects_data.get('count', 0) > 0:
                subjects_url = subjects_data.get('url', '').replace('?format=json', f'?format=json&api_key={self.api_key}')
                subjects_result = self._make_request(subjects_url)
                if subjects_result.get('success'):
                    additional_data['subjects'] = subjects_result['data'].get('subjects', {}).get('legislativeSubjects', [])

            # Fetch summaries
            summaries_data = bill_data.get('summaries', {})
            if summaries_data.get('count', 0) > 0:
                summaries_url = summaries_data.get('url', '').replace('?format=json', f'?format=json&api_key={self.api_key}')
                summaries_result = self._make_request(summaries_url)
                if summaries_result.get('success'):
                    additional_data['summaries'] = summaries_result['data'].get('summaries', [])

            # Fetch committees
            committees_data = bill_data.get('committees', {})
            if committees_data.get('count', 0) > 0:
                committees_url = committees_data.get('url', '').replace('?format=json', f'?format=json&api_key={self.api_key}')
                committees_result = self._make_request(committees_url)
                if committees_result.get('success'):
                    additional_data['committees'] = committees_result['data'].get('committees', [])

            # Fetch actions
            actions_data = bill_data.get('actions', {})
            if actions_data.get('count', 0) > 0:
                actions_url = actions_data.get('url', '').replace('?format=json', f'?format=json&api_key={self.api_key}')
                actions_result = self._make_request(actions_url)
                if actions_result.get('success'):
                    additional_data['actions'] = actions_result['data'].get('actions', [])

            # Fetch titles
            titles_data = bill_data.get('titles', {})
            if titles_data.get('count', 0) > 0:
                titles_url = titles_data.get('url', '').replace('?format=json', f'?format=json&api_key={self.api_key}')
                titles_result = self._make_request(titles_url)
                if titles_result.get('success'):
                    additional_data['titles'] = titles_result['data'].get('titles', [])

            # Merge additional data
            bill_data.update(additional_data)

            # Generate markdown content
            markdown_content = self._generate_bill_markdown(bill_data)

            return {
                "success": True,
                "endpoint": "bill_info",
                "bill_url": url,
                "bill_data": bill_data,
                "markdown_content": markdown_content,
                "timestamp": int(datetime.now().timestamp())
            }

        except Exception as e:
            return CongressGovError('bill_info', str(e)).to_dict()

    def _generate_bill_markdown(self, bill_data: Dict[str, Any]) -> str:
        """Generate markdown content for bill info"""
        try:
            title = bill_data.get('title', 'Unknown Title')
            congress = bill_data.get('congress', 'Unknown')
            number = bill_data.get('number', 'Unknown')
            bill_type = bill_data.get('type', 'Unknown')
            chamber = bill_data.get('originChamber', 'Unknown')
            introduced_date = bill_data.get('introducedDate', 'Unknown')
            update_date = bill_data.get('updateDate', 'Unknown')

            latest_action = bill_data.get('latestAction', {})
            latest_action_date = latest_action.get('actionDate', 'Unknown')
            latest_action_text = latest_action.get('text', 'No action taken')

            markdown = f"""# {title}

## Basic Information
- **Congress**: {congress}
- **Bill Number**: {number}
- **Type**: {bill_type}
- **Chamber**: {chamber}
- **Introduced**: {introduced_date}
- **Last Updated**: {update_date}
- **Latest Action**: {latest_action_date} - {latest_action_text}
"""

            # Policy Area
            policy_area = bill_data.get('policyArea', {}).get('name', '')
            if policy_area:
                markdown += f"- **Policy Area**: {policy_area}\n"

            # Sponsors
            sponsors = bill_data.get('sponsors', [])
            if sponsors:
                markdown += "\n## Sponsors\n\n"
                for sponsor in sponsors:
                    sponsor_name = sponsor.get('fullName', 'Unknown')
                    markdown += f"- **{sponsor_name}**\n"

            # Cosponsors
            cosponsors = bill_data.get('cosponsors', [])
            if cosponsors:
                markdown += "\n## Cosponsors\n\n"
                for cosponsor in cosponsors:
                    cosponsor_name = cosponsor.get('fullName', 'Unknown')
                    is_original = cosponsor.get('isOriginalCosponsor', False)
                    sponsorship_date = cosponsor.get('sponsorshipDate', 'Unknown')
                    if is_original:
                        cosponsor_name += " (Original Cosponsor)"
                    markdown += f"- **{cosponsor_name}** ({sponsorship_date})\n"

            # Summaries
            summaries = bill_data.get('summaries', [])
            if summaries:
                markdown += "\n## Summaries\n\n"
                for summary in summaries:
                    summary_text = self._html_to_markdown(summary.get('text', ''))
                    markdown += f"{summary_text}\n\n"

            # Actions
            actions = bill_data.get('actions', [])
            if actions:
                markdown += "\n## Actions\n\n"
                for action in actions:
                    action_date = action.get('actionDate', 'Unknown')
                    action_text = action.get('text', 'Unknown')
                    action_type = action.get('type', 'Unknown')
                    markdown += f"- **{action_date}**: ({action_type})\n  - {action_text}\n"

            return markdown

        except Exception as e:
            return f"Error generating markdown: {str(e)}"

    def _html_to_markdown(self, html_text: str) -> str:
        """Convert HTML to Markdown"""
        if not html_text:
            return ""

        try:
            # Simple HTML to Markdown conversion
            text = re.sub(r'<[^>]+>', '', html_text)  # Remove HTML tags
            text = re.sub(r'\s+', ' ', text).strip()  # Clean up whitespace
            return text
        except:
            return html_text

    # ===== BILL TEXT ENDPOINT =====

    def get_bill_text(self, bill_url: str) -> Dict[str, Any]:
        """
        Get available text versions for a bill

        Args:
            bill_url: Bill URL
        """
        try:
            # Build text endpoint URL
            if '?' in bill_url:
                text_url = bill_url.replace('?', '/text?')
            else:
                text_url = f"{bill_url.rstrip('/')}/text?format=json"

            result = self._make_request(text_url)

            if "error" in result:
                return CongressGovError('bill_text', result['error'], result.get('status_code')).to_dict()

            data = result.get('data', {})
            text_versions = data.get('textVersions', [])

            if not text_versions:
                return {
                    "success": True,
                    "endpoint": "bill_text",
                    "bill_url": bill_url,
                    "message": "No text available for this bill currently.",
                    "text_versions": [],
                    "timestamp": int(datetime.now().timestamp())
                }

            # Process text versions
            processed_versions = []
            for version in text_versions:
                processed_version = {
                    "version_type": version.get('type', ''),
                    "version_date": version.get('date', ''),
                    "formats": {}
                }

                formats = version.get('formats', [])
                for fmt in formats:
                    doc_type = fmt.get('type', '').replace('Formatted ', '').lower()
                    doc_url = fmt.get('url', '')
                    processed_version['formats'][doc_type] = doc_url

                processed_versions.append(processed_version)

            return {
                "success": True,
                "endpoint": "bill_text",
                "bill_url": bill_url,
                "text_versions": processed_versions,
                "timestamp": int(datetime.now().timestamp())
            }

        except Exception as e:
            return CongressGovError('bill_text', str(e)).to_dict()

    def download_bill_text(self, text_url: str) -> Dict[str, Any]:
        """
        Download bill text document

        Args:
            text_url: Direct URL to bill text document
        """
        try:
            if "congress.gov" not in text_url:
                return CongressGovError('download_bill_text', f'Invalid URL: {text_url}. Must be a valid Congress.gov URL.').to_dict()

            # Download the document
            response = self.session.get(text_url, timeout=30)
            response.raise_for_status()

            filename = text_url.split("/")[-1]
            content_type = response.headers.get('content-type', '')

            if 'pdf' in content_type.lower() or filename.lower().endswith('.pdf'):
                # PDF content
                pdf_content = base64.b64encode(response.content).decode('utf-8')
                return {
                    "success": True,
                    "endpoint": "download_bill_text",
                    "content": pdf_content,
                    "filename": filename,
                    "data_format": {
                        "data_type": "pdf",
                        "filename": filename
                    },
                    "timestamp": int(datetime.now().timestamp())
                }
            else:
                # Text content
                text_content = response.text
                return {
                    "success": True,
                    "endpoint": "download_bill_text",
                    "content": text_content,
                    "filename": filename,
                    "data_format": {
                        "data_type": "text",
                        "filename": filename
                    },
                    "timestamp": int(datetime.now().timestamp())
                }

        except Exception as e:
            return CongressGovError('download_bill_text', str(e)).to_dict()

    # ===== COMPOSITE METHODS =====

    def get_comprehensive_bill_data(self, bill_url: str) -> Dict[str, Any]:
        """Get comprehensive data for a bill from multiple endpoints"""
        result = {
            "success": True,
            "bill_url": bill_url,
            "timestamp": int(datetime.now().timestamp()),
            "endpoints": {},
            "failed_endpoints": []
        }

        # Define endpoints to try
        endpoints = [
            ('bill_info', lambda: self.get_bill_info(bill_url)),
            ('bill_text', lambda: self.get_bill_text(bill_url))
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

    def get_bill_summary_by_congress(self, congress: Optional[int] = None,
                                    limit: int = 50) -> Dict[str, Any]:
        """Get a summary of bills by type for a given congress"""
        try:
            if not congress:
                congress = self._year_to_congress(datetime.now().year)

            result = {
                "success": True,
                "endpoint": "bill_summary",
                "congress": congress,
                "bill_types": {},
                "timestamp": int(datetime.now().timestamp())
            }

            total_bills = 0

            for bill_type in BILL_TYPES:
                try:
                    bills_result = self.get_congress_bills(
                        congress=congress,
                        bill_type=bill_type,
                        limit=limit,
                        sort_by="desc"
                    )

                    if bills_result.get("success"):
                        bills_count = bills_result.get("total_bills", 0)
                        result["bill_types"][bill_type] = {
                            "count": bills_count,
                            "latest_bills": bills_result.get("bills", [])[:5]  # Top 5 latest
                        }
                        total_bills += bills_count
                    else:
                        result["bill_types"][bill_type] = {
                            "error": bills_result.get("error", "Unknown error"),
                            "count": 0
                        }

                except Exception as e:
                    result["bill_types"][bill_type] = {
                        "error": str(e),
                        "count": 0
                    }

            result["total_bills"] = total_bills
            return result

        except Exception as e:
            return CongressGovError('bill_summary', str(e)).to_dict()

    # ===== MEMBER ENDPOINTS =====

    def get_members(self, congress: Optional[int] = None,
                    chamber: Optional[str] = None,
                    state: Optional[str] = None,
                    current_only: bool = True,
                    limit: int = 250,
                    offset: int = 0,
                    get_all: bool = False) -> Dict[str, Any]:
        """List Congress members.

        Args:
            congress: e.g. 119
            chamber: "House" or "Senate"
            state: 2-letter postal code (filters server-side)
            current_only: include only currently serving members
            limit: 1-250
            offset: pagination offset
            get_all: ignore offset/limit and paginate through everything
        """
        try:
            congress_num = congress or self._year_to_congress(datetime.now().year)
            url = f"{BASE_URL}member"
            params = []
            params.append(f"congress={congress_num}")
            if chamber:    params.append(f"chamber={chamber}")
            if state:      params.append(f"state={state.upper()}")
            if current_only: params.append("currentMember=true")
            params.append("format=json")
            params.append(f"limit={min(int(limit or 250), 250)}")
            params.append(f"offset={int(offset or 0)}")
            url += "?" + "&".join(params)

            all_members: List[Dict[str, Any]] = []
            page_url = url
            while True:
                result = self._make_request(page_url)
                if "error" in result:
                    return CongressGovError('members', result['error'], result.get('status_code')).to_dict()
                data = result.get('data', {})
                page = data.get('members', []) or []
                for m in page:
                    raw_name = m.get('name', '')
                    if "," in raw_name:
                        parts = raw_name.split(",", 1)
                        full_name = f"{parts[1].strip()} {parts[0].strip()}"
                    else:
                        full_name = raw_name
                    terms_item = (m.get('terms') or {}).get('item') or []
                    latest_term = terms_item[-1] if terms_item else {}
                    all_members.append({
                        "bioguide_id":  m.get('bioguideId', ''),
                        "full_name":    full_name,
                        "raw_name":     raw_name,
                        "party":        m.get('partyName', m.get('party', '')),
                        "state":        m.get('state', ''),
                        "district":     m.get('district', ''),
                        "chamber":      latest_term.get('chamber', ''),
                        "term_start":   latest_term.get('startYear', ''),
                        "term_end":     latest_term.get('endYear', ''),
                        "image_url":    (m.get('depiction') or {}).get('imageUrl', ''),
                        "member_url":   m.get('url', ''),
                        "update_date":  m.get('updateDate', ''),
                    })
                if not get_all:
                    break
                next_url = (data.get('pagination') or {}).get('next')
                if not next_url:
                    break
                # next URL already has the api_key removed; append it back
                page_url = next_url + (f"&api_key={self.api_key}"
                                       if "api_key=" not in next_url else "")

            return {
                "success": True,
                "endpoint": "members",
                "congress": congress_num,
                "filters": {"chamber": chamber, "state": state, "current_only": current_only},
                "total_members": len(all_members),
                "members": all_members,
                "timestamp": int(datetime.now().timestamp())
            }
        except Exception as e:
            return CongressGovError('members', str(e)).to_dict()

    def get_member_detail(self, bioguide_id: str) -> Dict[str, Any]:
        """Detailed member info: party history, terms, depiction, sub-endpoints."""
        try:
            if not bioguide_id:
                return CongressGovError('member_detail', 'bioguide_id required').to_dict()
            url = f"{BASE_URL}member/{bioguide_id}?format=json"
            result = self._make_request(url)
            if "error" in result:
                return CongressGovError('member_detail', result['error'], result.get('status_code')).to_dict()
            data = result.get('data', {}).get('member', {})
            if not data:
                return CongressGovError('member_detail', 'Member not found', 404).to_dict()
            return {
                "success": True,
                "endpoint": "member_detail",
                "bioguide_id": bioguide_id,
                "member": data,
                "timestamp": int(datetime.now().timestamp())
            }
        except Exception as e:
            return CongressGovError('member_detail', str(e)).to_dict()

    def get_member_sponsored(self, bioguide_id: str, limit: int = 50) -> Dict[str, Any]:
        """Bills sponsored by this member."""
        try:
            if not bioguide_id:
                return CongressGovError('member_sponsored', 'bioguide_id required').to_dict()
            url = f"{BASE_URL}member/{bioguide_id}/sponsored-legislation?format=json&limit={min(int(limit or 50), 250)}"
            result = self._make_request(url)
            if "error" in result:
                return CongressGovError('member_sponsored', result['error'], result.get('status_code')).to_dict()
            data = result.get('data', {})
            bills = data.get('sponsoredLegislation', []) or []
            return {
                "success": True,
                "endpoint": "member_sponsored",
                "bioguide_id": bioguide_id,
                "total_bills": len(bills),
                "bills": bills,
                "timestamp": int(datetime.now().timestamp())
            }
        except Exception as e:
            return CongressGovError('member_sponsored', str(e)).to_dict()

    def get_member_cosponsored(self, bioguide_id: str, limit: int = 50) -> Dict[str, Any]:
        """Bills cosponsored by this member."""
        try:
            if not bioguide_id:
                return CongressGovError('member_cosponsored', 'bioguide_id required').to_dict()
            url = f"{BASE_URL}member/{bioguide_id}/cosponsored-legislation?format=json&limit={min(int(limit or 50), 250)}"
            result = self._make_request(url)
            if "error" in result:
                return CongressGovError('member_cosponsored', result['error'], result.get('status_code')).to_dict()
            data = result.get('data', {})
            bills = data.get('cosponsoredLegislation', []) or []
            return {
                "success": True,
                "endpoint": "member_cosponsored",
                "bioguide_id": bioguide_id,
                "total_bills": len(bills),
                "bills": bills,
                "timestamp": int(datetime.now().timestamp())
            }
        except Exception as e:
            return CongressGovError('member_cosponsored', str(e)).to_dict()

    # ===== COMMITTEE ENDPOINTS =====

    def get_committees(self, congress: Optional[int] = None,
                       chamber: Optional[str] = None,
                       limit: int = 250) -> Dict[str, Any]:
        """List committees. chamber must be 'house', 'senate', or 'joint' (lowercase)."""
        try:
            congress_num = congress or self._year_to_congress(datetime.now().year)
            ch = (chamber or "").lower()
            if ch and ch not in ("house", "senate", "joint"):
                return CongressGovError('committees',
                    "chamber must be 'house', 'senate', or 'joint'").to_dict()
            url = f"{BASE_URL}committee/{congress_num}"
            if ch:
                url += f"/{ch}"
            url += f"?format=json&limit={min(int(limit or 250), 250)}"
            result = self._make_request(url)
            if "error" in result:
                return CongressGovError('committees', result['error'], result.get('status_code')).to_dict()
            data = result.get('data', {})
            return {
                "success": True,
                "endpoint": "committees",
                "congress": congress_num,
                "chamber": ch,
                "total_committees": len(data.get('committees', []) or []),
                "committees": data.get('committees', []),
                "timestamp": int(datetime.now().timestamp())
            }
        except Exception as e:
            return CongressGovError('committees', str(e)).to_dict()

    def get_committee_detail(self, congress: int, chamber: str, system_code: str) -> Dict[str, Any]:
        """Committee detail: subcommittees, history, bills/communications counts."""
        try:
            ch = (chamber or "").lower()
            if ch not in ("house", "senate", "joint"):
                return CongressGovError('committee_detail',
                    "chamber must be 'house', 'senate', or 'joint'").to_dict()
            url = f"{BASE_URL}committee/{int(congress)}/{ch}/{system_code}?format=json"
            result = self._make_request(url)
            if "error" in result:
                return CongressGovError('committee_detail', result['error'], result.get('status_code')).to_dict()
            cmte = result.get('data', {}).get('committee', {})
            return {
                "success": True,
                "endpoint": "committee_detail",
                "congress": congress,
                "chamber": ch,
                "system_code": system_code,
                "committee": cmte,
                "timestamp": int(datetime.now().timestamp())
            }
        except Exception as e:
            return CongressGovError('committee_detail', str(e)).to_dict()

    # ===== VOTING RECORDS =====

    def get_votes(self, chamber: str, congress: Optional[int] = None,
                  session: Optional[int] = None, limit: int = 50) -> Dict[str, Any]:
        """List recent roll-call votes for a chamber. chamber: 'house' or 'senate'."""
        try:
            ch = (chamber or "").lower()
            if ch not in ("house", "senate"):
                return CongressGovError('votes', "chamber must be 'house' or 'senate'").to_dict()
            congress_num = congress or self._year_to_congress(datetime.now().year)
            # Default session = current (1 for odd year, 2 for even year of congress)
            year = datetime.now().year
            sess = session or ((year - (1789 + 2*(congress_num-1))) % 2 + 1)
            url = (f"{BASE_URL}{ch}-vote/{congress_num}/{sess}"
                   f"?format=json&limit={min(int(limit or 50), 250)}")
            result = self._make_request(url)
            if "error" in result:
                return CongressGovError('votes', result['error'], result.get('status_code')).to_dict()
            data = result.get('data', {})
            votes_key = f"{ch}RollCallVotes"
            votes = data.get(votes_key, data.get('votes', [])) or []
            return {
                "success": True,
                "endpoint": "votes",
                "chamber": ch,
                "congress": congress_num,
                "session": sess,
                "total_votes": len(votes),
                "votes": votes,
                "timestamp": int(datetime.now().timestamp())
            }
        except Exception as e:
            return CongressGovError('votes', str(e)).to_dict()

    # ===== HEARINGS / NOMINATIONS / TREATIES =====

    def get_hearings(self, congress: Optional[int] = None,
                     chamber: Optional[str] = None, limit: int = 50) -> Dict[str, Any]:
        """Committee hearings (current congress by default)."""
        try:
            congress_num = congress or self._year_to_congress(datetime.now().year)
            ch = (chamber or "").lower() if chamber else ""
            url = f"{BASE_URL}hearing/{congress_num}"
            if ch in ("house", "senate", "joint"):
                url += f"/{ch}"
            url += f"?format=json&limit={min(int(limit or 50), 250)}"
            result = self._make_request(url)
            if "error" in result:
                return CongressGovError('hearings', result['error'], result.get('status_code')).to_dict()
            data = result.get('data', {})
            return {
                "success": True,
                "endpoint": "hearings",
                "congress": congress_num,
                "chamber": ch,
                "total_hearings": len(data.get('hearings', []) or []),
                "hearings": data.get('hearings', []),
                "timestamp": int(datetime.now().timestamp())
            }
        except Exception as e:
            return CongressGovError('hearings', str(e)).to_dict()

    def get_nominations(self, congress: Optional[int] = None, limit: int = 50) -> Dict[str, Any]:
        """Presidential nominations sent to the Senate."""
        try:
            congress_num = congress or self._year_to_congress(datetime.now().year)
            url = f"{BASE_URL}nomination/{congress_num}?format=json&limit={min(int(limit or 50), 250)}"
            result = self._make_request(url)
            if "error" in result:
                return CongressGovError('nominations', result['error'], result.get('status_code')).to_dict()
            data = result.get('data', {})
            return {
                "success": True,
                "endpoint": "nominations",
                "congress": congress_num,
                "total_nominations": len(data.get('nominations', []) or []),
                "nominations": data.get('nominations', []),
                "timestamp": int(datetime.now().timestamp())
            }
        except Exception as e:
            return CongressGovError('nominations', str(e)).to_dict()

    def get_treaties(self, congress: Optional[int] = None, limit: int = 50) -> Dict[str, Any]:
        """Treaties received by the Senate."""
        try:
            congress_num = congress or self._year_to_congress(datetime.now().year)
            url = f"{BASE_URL}treaty/{congress_num}?format=json&limit={min(int(limit or 50), 250)}"
            result = self._make_request(url)
            if "error" in result:
                return CongressGovError('treaties', result['error'], result.get('status_code')).to_dict()
            data = result.get('data', {})
            return {
                "success": True,
                "endpoint": "treaties",
                "congress": congress_num,
                "total_treaties": len(data.get('treaties', []) or []),
                "treaties": data.get('treaties', []),
                "timestamp": int(datetime.now().timestamp())
            }
        except Exception as e:
            return CongressGovError('treaties', str(e)).to_dict()

# ===== CLI INTERFACE =====

def main():
    if len(sys.argv) < 2:
        print(json.dumps({
            "success": False,
            "error": "Usage: python congress_gov_data.py <command> <args>",
            "available_commands": [
                "congress_bills [congress] [bill_type] [start_date] [end_date] [limit] [offset] [sort_by] [get_all]",
                "bill_info <bill_url>",
                "bill_text <bill_url>",
                "download_text <text_url>",
                "comprehensive <bill_url>",
                "summary [congress] [limit]"
            ],
            "note": "Requires CONGRESS_GOV_API_KEY environment variable"
        }))
        sys.exit(1)

    command = sys.argv[1]

    # Aliases used by the C++ GovDataCongressPanel
    if command == "bills":
        command = "congress_bills"

    wrapper = CongressGovWrapper()

    try:
        if command == "congress_bills":
            congress = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] else None
            bill_type = sys.argv[3] if len(sys.argv) > 3 else None
            start_date = sys.argv[4] if len(sys.argv) > 4 else None
            end_date = sys.argv[5] if len(sys.argv) > 5 else None
            limit = int(sys.argv[6]) if len(sys.argv) > 6 and sys.argv[6] else None
            offset = int(sys.argv[7]) if len(sys.argv) > 7 and sys.argv[7] else 0
            sort_by = sys.argv[8] if len(sys.argv) > 8 else "desc"
            get_all = sys.argv[9].lower() == "true" if len(sys.argv) > 9 else False

            result = wrapper.get_congress_bills(
                congress=congress,
                bill_type=bill_type,
                start_date=start_date,
                end_date=end_date,
                limit=limit,
                offset=offset,
                sort_by=sort_by,
                get_all=get_all
            )
            print(json.dumps(result, indent=2))

        elif command == "bill_info":
            if len(sys.argv) < 3:
                print(json.dumps({"success": False, "error": "Usage: python congress_gov_data.py bill_info <bill_url_or_congress> [type] [number]"}))
                sys.exit(1)

            # C++ sends: bill_info <congress> <bill_type> <bill_number>
            # Script expects: bill_info <bill_url>
            # Build the API URL from the 3-arg form if the first arg is a number
            if sys.argv[2].isdigit() and len(sys.argv) >= 5:
                congress_num = sys.argv[2]
                bill_type = sys.argv[3].lower()
                bill_number = sys.argv[4]
                bill_url = f"https://api.congress.gov/v3/bill/{congress_num}/{bill_type}/{bill_number}"
            else:
                bill_url = sys.argv[2]
            result = wrapper.get_bill_info(bill_url)
            print(json.dumps(result, indent=2))

        elif command == "bill_text":
            if len(sys.argv) < 3:
                print(json.dumps({"success": False, "error": "Usage: python congress_gov_data.py bill_text <bill_url>"}))
                sys.exit(1)

            bill_url = sys.argv[2]
            result = wrapper.get_bill_text(bill_url)
            print(json.dumps(result, indent=2))

        elif command == "download_text":
            if len(sys.argv) < 3:
                print(json.dumps({"success": False, "error": "Usage: python congress_gov_data.py download_text <text_url>"}))
                sys.exit(1)

            text_url = sys.argv[2]
            result = wrapper.download_bill_text(text_url)
            print(json.dumps(result, indent=2))

        elif command == "comprehensive":
            if len(sys.argv) < 3:
                print(json.dumps({"success": False, "error": "Usage: python congress_gov_data.py comprehensive <bill_url>"}))
                sys.exit(1)

            bill_url = sys.argv[2]
            result = wrapper.get_comprehensive_bill_data(bill_url)
            print(json.dumps(result, indent=2))

        elif command == "summary":
            congress = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] else None
            limit = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3] else 50

            result = wrapper.get_bill_summary_by_congress(congress=congress, limit=limit)
            print(json.dumps(result, indent=2))

        elif command == "members":
            # members [congress] [chamber] [state] [current_only:true|false] [limit] [offset] [get_all:true|false]
            congress = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] else None
            chamber  = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else None
            state    = sys.argv[4] if len(sys.argv) > 4 and sys.argv[4] else None
            current  = sys.argv[5].lower() != "false" if len(sys.argv) > 5 else True
            limit    = int(sys.argv[6]) if len(sys.argv) > 6 and sys.argv[6] else 250
            offset   = int(sys.argv[7]) if len(sys.argv) > 7 and sys.argv[7] else 0
            get_all  = sys.argv[8].lower() == "true" if len(sys.argv) > 8 else False
            result = wrapper.get_members(congress=congress, chamber=chamber, state=state,
                                         current_only=current, limit=limit, offset=offset,
                                         get_all=get_all)
            print(json.dumps(result, indent=2))

        elif command == "member_detail":
            if len(sys.argv) < 3:
                print(json.dumps({"success": False, "error": "Usage: member_detail <bioguide_id>"}))
                sys.exit(1)
            result = wrapper.get_member_detail(sys.argv[2])
            print(json.dumps(result, indent=2))

        elif command == "member_sponsored":
            if len(sys.argv) < 3:
                print(json.dumps({"success": False, "error": "Usage: member_sponsored <bioguide_id> [limit]"}))
                sys.exit(1)
            limit = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3] else 50
            result = wrapper.get_member_sponsored(sys.argv[2], limit=limit)
            print(json.dumps(result, indent=2))

        elif command == "member_cosponsored":
            if len(sys.argv) < 3:
                print(json.dumps({"success": False, "error": "Usage: member_cosponsored <bioguide_id> [limit]"}))
                sys.exit(1)
            limit = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3] else 50
            result = wrapper.get_member_cosponsored(sys.argv[2], limit=limit)
            print(json.dumps(result, indent=2))

        elif command == "committees":
            # committees [congress] [chamber] [limit]
            congress = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] else None
            chamber  = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else None
            limit    = int(sys.argv[4]) if len(sys.argv) > 4 and sys.argv[4] else 250
            result = wrapper.get_committees(congress=congress, chamber=chamber, limit=limit)
            print(json.dumps(result, indent=2))

        elif command == "committee_detail":
            # committee_detail <congress> <chamber> <system_code>
            if len(sys.argv) < 5:
                print(json.dumps({"success": False, "error": "Usage: committee_detail <congress> <chamber> <system_code>"}))
                sys.exit(1)
            result = wrapper.get_committee_detail(int(sys.argv[2]), sys.argv[3], sys.argv[4])
            print(json.dumps(result, indent=2))

        elif command == "votes":
            # votes <chamber> [congress] [session] [limit]
            if len(sys.argv) < 3:
                print(json.dumps({"success": False, "error": "Usage: votes <chamber=house|senate> [congress] [session] [limit]"}))
                sys.exit(1)
            chamber = sys.argv[2]
            congress = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3] else None
            session  = int(sys.argv[4]) if len(sys.argv) > 4 and sys.argv[4] else None
            limit    = int(sys.argv[5]) if len(sys.argv) > 5 and sys.argv[5] else 50
            result = wrapper.get_votes(chamber=chamber, congress=congress, session=session, limit=limit)
            print(json.dumps(result, indent=2))

        elif command == "hearings":
            # hearings [congress] [chamber] [limit]
            congress = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] else None
            chamber  = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] else None
            limit    = int(sys.argv[4]) if len(sys.argv) > 4 and sys.argv[4] else 50
            result = wrapper.get_hearings(congress=congress, chamber=chamber, limit=limit)
            print(json.dumps(result, indent=2))

        elif command == "nominations":
            # nominations [congress] [limit]
            congress = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] else None
            limit    = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3] else 50
            result = wrapper.get_nominations(congress=congress, limit=limit)
            print(json.dumps(result, indent=2))

        elif command == "treaties":
            # treaties [congress] [limit]
            congress = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] else None
            limit    = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3] else 50
            result = wrapper.get_treaties(congress=congress, limit=limit)
            print(json.dumps(result, indent=2))

        else:
            print(json.dumps({
                "success": False,
                "error": f"Unknown command: {command}",
                "available_commands": [
                    "congress_bills [congress] [bill_type] [start_date] [end_date] [limit] [offset] [sort_by] [get_all]",
                    "bill_info <bill_url>",
                    "bill_text <bill_url>",
                    "download_text <text_url>",
                    "comprehensive <bill_url>",
                    "summary [congress] [limit]",
                    "members [congress] [chamber] [state] [current_only] [limit] [offset] [get_all]",
                    "member_detail <bioguide_id>",
                    "member_sponsored <bioguide_id> [limit]",
                    "member_cosponsored <bioguide_id> [limit]",
                    "committees [congress] [chamber] [limit]",
                    "committee_detail <congress> <chamber> <system_code>",
                    "votes <chamber> [congress] [session] [limit]",
                    "hearings [congress] [chamber] [limit]",
                    "nominations [congress] [limit]",
                    "treaties [congress] [limit]"
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
    import os
    main()