"""
HDX (Humanitarian Data Exchange) Data Wrapper
Direct CKAN API implementation - no external dependencies required
Based on HDX CKAN API Cookbook
"""

import sys
import json
import urllib.request
import urllib.parse
import urllib.error
from typing import Optional, Dict, List, Any


# HDX CKAN API Configuration
HDX_API_BASE = "https://data.humdata.org/api/3"
USER_AGENT = "finterm_GeopoliticsAnalytics/3.0"


def make_api_request(endpoint: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    """
    Make a request to the HDX CKAN API

    Args:
        endpoint: API endpoint (e.g., 'action/package_search')
        params: Query parameters

    Returns:
        Parsed JSON response
    """
    url = f"{HDX_API_BASE}/{endpoint}"

    if params:
        # Filter out None values and convert to strings
        clean_params = {k: str(v) for k, v in params.items() if v is not None}
        query_string = urllib.parse.urlencode(clean_params)
        url = f"{url}?{query_string}"

    # Create request with user agent
    req = urllib.request.Request(url)
    req.add_header('User-Agent', USER_AGENT)

    try:
        with urllib.request.urlopen(req, timeout=30) as response:
            data = json.loads(response.read().decode('utf-8'))

            if not data.get('success', False):
                raise Exception(f"API request failed: {data.get('error', {}).get('message', 'Unknown error')}")

            return data.get('result', {})

    except urllib.error.HTTPError as e:
        raise Exception(f"HTTP Error {e.code}: {e.reason}")
    except urllib.error.URLError as e:
        raise Exception(f"URL Error: {e.reason}")
    except Exception as e:
        raise Exception(f"Request failed: {str(e)}")


def package_search(q: str = "", start: int = 0, rows: int = 10, sort: Optional[str] = None) -> Dict[str, Any]:
    """
    Search for datasets using CKAN package_search

    Args:
        q: Query string (field:value format)
        start: Starting position for pagination
        rows: Number of results per page
        sort: Sort order (e.g., 'metadata_created desc')

    Returns:
        Search results with count and datasets
    """
    params = {
        'q': q,
        'start': start,
        'rows': rows
    }

    if sort:
        params['sort'] = sort

    return make_api_request('action/package_search', params)


def group_list(all_fields: bool = True) -> List[Dict[str, Any]]:
    """
    List all countries/groups available in HDX

    Args:
        all_fields: Return full metadata (not just names)

    Returns:
        List of groups/countries
    """
    params = {'all_fields': 'true' if all_fields else 'false'}
    return make_api_request('action/group_list', params)


def extract_dataset_summary(dataset: Dict[str, Any]) -> Dict[str, Any]:
    """Extract key fields from dataset metadata for summary view"""
    return {
        "id": dataset.get("id", ""),
        "name": dataset.get("name", ""),
        "title": dataset.get("title", ""),
        "organization": dataset.get("organization", {}).get("title", "") if isinstance(dataset.get("organization"), dict) else "",
        "dataset_date": dataset.get("dataset_date", ""),
        "num_resources": dataset.get("num_resources", 0),
        "tags": [tag.get("name", "") if isinstance(tag, dict) else tag for tag in dataset.get("tags", [])],
        "notes": (dataset.get("notes", "")[:200] + "...") if dataset.get("notes") and len(dataset.get("notes", "")) > 200 else dataset.get("notes", "")
    }


def main(args: Optional[List[str]] = None) -> str:
    """Main entry point for script execution"""
    if args is None:
        args = sys.argv[1:]

    if len(args) < 1:
        return json.dumps({"error": "Usage: hdx_data.py <command> [args...]"})

    command = args[0]

    try:
        result = {}

        if command == "search_datasets":
            # Search for datasets by query
            query = args[1] if len(args) > 1 else ""
            limit = int(args[2]) if len(args) > 2 else 10

            if not query:
                return json.dumps({"error": "Query parameter required"})

            search_result = package_search(q=query, rows=limit, sort="last_modified desc")

            result = {
                "success": True,
                "data": {
                    "query": query,
                    "count": search_result.get("count", 0),
                    "datasets": [extract_dataset_summary(ds) for ds in search_result.get("results", [])]
                }
            }

        elif command == "search_conflict":
            # Search for conflict-related datasets
            country = args[1] if len(args) > 1 else ""
            limit = int(args[2]) if len(args) > 2 else 10

            # Build query using CKAN field syntax
            query_parts = []

            # Add country filter if specified
            if country:
                # Try to convert country name to ISO3 code (lowercase)
                country_code = country.lower()[:3]
                query_parts.append(f"groups:{country_code}")

            # Add conflict-related topic tags
            # Using vocab_Topics field for thematic tags
            query_parts.append('vocab_Topics:"conflict, violence and peace"')

            query = " ".join(query_parts)

            search_result = package_search(q=query, rows=limit, sort="last_modified desc")

            result = {
                "success": True,
                "data": {
                    "country": country,
                    "count": search_result.get("count", 0),
                    "datasets": [extract_dataset_summary(ds) for ds in search_result.get("results", [])]
                }
            }

        elif command == "search_humanitarian":
            # Search for humanitarian crisis data
            country = args[1] if len(args) > 1 else ""
            limit = int(args[2]) if len(args) > 2 else 10

            # Build query using CKAN field syntax
            query_parts = []

            if country:
                country_code = country.lower()[:3]
                query_parts.append(f"groups:{country_code}")

            # Add humanitarian-related topic tags
            query_parts.append('vocab_Topics:"humanitarian access-aid-workers"')

            query = " ".join(query_parts) if query_parts else "humanitarian"

            search_result = package_search(q=query, rows=limit, sort="last_modified desc")

            result = {
                "success": True,
                "data": {
                    "country": country,
                    "count": search_result.get("count", 0),
                    "datasets": [extract_dataset_summary(ds) for ds in search_result.get("results", [])]
                }
            }

        elif command == "search_by_country":
            # Search datasets for a specific country
            country_code = args[1] if len(args) > 1 else ""
            limit = int(args[2]) if len(args) > 2 else 10

            if not country_code:
                return json.dumps({"error": "Country code required"})

            query = f"groups:{country_code.lower()}"
            search_result = package_search(q=query, rows=limit, sort="last_modified desc")

            result = {
                "success": True,
                "data": {
                    "country_code": country_code,
                    "count": search_result.get("count", 0),
                    "datasets": [extract_dataset_summary(ds) for ds in search_result.get("results", [])]
                }
            }

        elif command == "search_by_topic":
            # Search datasets by topic tag
            topic = args[1] if len(args) > 1 else ""
            limit = int(args[2]) if len(args) > 2 else 10

            if not topic:
                return json.dumps({"error": "Topic required"})

            # Quote topic if it contains spaces
            if " " in topic:
                query = f'vocab_Topics:"{topic}"'
            else:
                query = f'vocab_Topics:{topic}'

            search_result = package_search(q=query, rows=limit, sort="last_modified desc")

            result = {
                "success": True,
                "data": {
                    "topic": topic,
                    "count": search_result.get("count", 0),
                    "datasets": [extract_dataset_summary(ds) for ds in search_result.get("results", [])]
                }
            }

        elif command == "list_countries":
            # List all countries/groups
            limit = int(args[1]) if len(args) > 1 else 300

            groups = group_list(all_fields=True)

            result = {
                "success": True,
                "data": {
                    "count": len(groups[:limit]),
                    "countries": [
                        {
                            "id": g.get("id", ""),
                            "name": g.get("name", ""),
                            "title": g.get("title", ""),
                            "description": g.get("description", ""),
                            "package_count": g.get("package_count", 0)
                        }
                        for g in groups[:limit]
                    ]
                }
            }

        elif command == "test_connection":
            # Test HDX connection
            try:
                search_result = package_search(q="*:*", rows=1)
                result = {
                    "success": True,
                    "data": {
                        "status": "connected",
                        "message": "HDX CKAN API is accessible",
                        "total_datasets": search_result.get("count", 0)
                    }
                }
            except Exception as e:
                result = {
                    "success": False,
                    "error": f"Connection test failed: {str(e)}"
                }

        else:
            result = {"error": f"Unknown command: {command}"}

        output = json.dumps(result, indent=2)
        print(output)
        return output

    except Exception as e:
        error_result = {"error": str(e)}
        error_output = json.dumps(error_result, indent=2)
        print(error_output)
        return error_output


if __name__ == "__main__":
    main()
