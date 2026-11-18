"""
ILOSTAT Data Wrapper
Fetches international labour statistics from the ILO ILOSTAT SDMX REST API
and the ILOSTAT Bulk Download Facility.

API Reference:
  Base URL (SDMX):    https://sdmx.ilo.org/rest
  TOC (indicators):   https://rplumber.ilo.org/metadata/toc/indicator
  TOC (ref area):     https://rplumber.ilo.org/metadata/toc/ref_area
  Bulk Download CSV:  SDMX REST API preferred; bulk .rds files at rplumber.ilo.org/files/indicator/
  User Guide:         https://webapps.ilo.org/ilostat-files/Documents/SDMX_User_Guide.pdf

Authentication: None required — ILOSTAT is open data, no API key needed.

Rate Limits: Not formally documented; HTTP 413 is returned when the query
             result set exceeds the server payload limit (~300,000 records).
             Use the bulk download facility or add dimension filters to reduce payload size.

Response Formats: SDMX-JSON (jsondata), SDMX-CSV (csv / csvfilewithlabels), SDMX-ML (XML)
Default format used here: CSV with labels for easy parsing.

SDMX Key dimension order (dot-separated):
    FREQ . REF_AREA . MEASURE . SEX . CLASSIF1 [. CLASSIF2]
    e.g. "CAN.A.UNE_DEAP_RT.SEX_T.AGE_YTHADULT_YGE15"

    Wildcarding: omit a dimension to wildcard it (use just the dots).
    OR logic: use + between values, e.g. "CAN+USA+GBR"

Returns JSON output for C++ integration.
"""

import sys
import json
import io
import requests
import traceback
from typing import Dict, Any, List, Optional
from datetime import datetime


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SDMX_BASE_URL = "https://sdmx.ilo.org/rest"

# Bulk download: the old www.ilo.org URLs redirect to webapps.ilo.org but both
# return 404 for CSV/GZ files as of 2025. The Rilostat R package uses
# rplumber.ilo.org which serves .rds (R binary) files. For Python use the
# SDMX REST API for data; rplumber.ilo.org only for the TOC CSV endpoint.
BULK_BASE_URL = "https://www.ilo.org/ilostat-files/WEB_bulk_download"

# Table of Contents: discovered working endpoint on rplumber.ilo.org
TOC_BASE_URL         = "https://rplumber.ilo.org/metadata/toc"
TOC_BY_INDICATOR_URL = f"{TOC_BASE_URL}/indicator"
TOC_BY_REF_AREA_URL  = f"{TOC_BASE_URL}/ref_area"

DEFAULT_TIMEOUT = 60  # seconds

# ---------------------------------------------------------------------------
# Key indicator codes (measure codes without the DF_ prefix)
# These are the 'flow' identifiers used in the SDMX key.
# The full dataflow ID used in the URL is: ILO,DF_<INDICATOR_CODE>
# ---------------------------------------------------------------------------

# Employment
INDICATOR_EMP_TOTAL_SEX_AGE         = "EMP_TEMP_SEX_AGE_NB"   # Employment by sex & age (number)
INDICATOR_EMP_TOTAL_SEX_STE         = "EMP_TEMP_SEX_STE_NB"   # Employment by sex & status in employment
INDICATOR_EMP_TO_POP_RATIO          = "EMP_DWAP_SEX_AGE_RT"   # Employment-to-population ratio
INDICATOR_EMP_MODELLED              = "EMP_2EMP_SEX_AGE_NB"   # ILO modelled estimates — employment

# Unemployment
INDICATOR_UNE_RATE_SEX_AGE          = "UNE_DEAP_SEX_AGE_RT"   # Unemployment rate by sex & age (used in SDMX key)
INDICATOR_UNE_TOTAL_SEX_AGE_NB      = "UNE_2UNE_SEX_AGE_NB"   # Unemployment count (annual)
INDICATOR_UNE_MODELLED_RATE         = "UNE_DEAP_SEX_AGE_RT"   # ILO modelled unemployment rate

# Labour Force / Working-Age Population
INDICATOR_EAP_TOTAL_SEX_AGE         = "EAP_TEAP_SEX_AGE_NB"   # Labour force by sex & age
INDICATOR_EAP_PARTICIPATION_RATE    = "EAP_DWAP_SEX_AGE_RT"   # Labour force participation rate
INDICATOR_WAP_TOTAL_SEX_AGE         = "WAP_WAPF_SEX_AGE_NB"   # Working-age population by sex & age

# Wages & Earnings (verified dataflow IDs from /dataflow/ILO 2025-02)
INDICATOR_EAR_HOURLY_SEX            = "EAR_4HRL_SEX_CUR_NB"   # Mean hourly earnings by sex & currency (DSD dims: REF_AREA.FREQ.MEASURE.SEX.CUR)
INDICATOR_EAR_MONTHLY_SEX           = "EAR_4MTH_SEX_CUR_NB"   # Mean monthly earnings by sex & currency
INDICATOR_EAR_MIN_WAGE              = "EAR_4MMN_CUR_NB"        # Statutory minimum wage (DSD dims: REF_AREA.FREQ.MEASURE.CUR — no SEX)
INDICATOR_EAR_GENDER_GAP_OCC       = "EAR_GGAP_OCU_RT"        # Gender wage gap by occupation (verified existing)
INDICATOR_EAR_LOW_PAY               = "EAR_XTLP_SEX_RT"        # Low pay incidence by sex (verified existing)
INDICATOR_EAR_SDG_HOURLY            = "SDG_0851_SEX_OCU_NB"    # SDG 8.5.1 average hourly earnings

# Labour Income / Productivity
INDICATOR_LAP_INCOME_SHARE          = "LAP_2GDP_NOC_RT"        # Labour income share (% of GDP, modelled)
INDICATOR_LAP_INCOME_DIST           = "LAP_2LID_QTL_RT"        # Income distribution by decile
INDICATOR_LAP_GENDER_INCOME_GAP     = "LAP_2FTM_NOC_RT"        # Gender income gap (women-to-men ratio)

# Hours of Work
INDICATOR_HOW_MEAN_WEEKLY_SEX       = "HOW_TEMP_SEX_ECO_NB"    # Mean weekly hours by sex & economic activity
INDICATOR_HOW_EXCESSIVE_48H         = "HOW_XEES_SEX_ECO_RT"    # Share working > 48h/week

# SDG Labour Market Indicators
INDICATOR_SDG_WORKING_POVERTY       = "SDG_0111_SEX_AGE_RT"    # SDG 1.1.1 working poverty rate (%)
INDICATOR_SDG_SOCIAL_PROTECTION     = "SDG_0131_SEX_SOC_RT"    # SDG 1.3.1 social protection coverage
INDICATOR_SDG_LABOUR_INCOME_SHARE   = "SDG_1041_NOC_RT"        # SDG 10.4.1 labour income share

# Exchange rates (for currency conversion context)
INDICATOR_CCF_EXCHANGE_RATES        = "CCF_XOXR_CUR_RT"        # Official exchange rates (LCU per USD)
INDICATOR_CCF_PPP                   = "CCF_XPPP_CUR_RT"        # PPP conversion factors

# ---------------------------------------------------------------------------
# Frequency codes
# ---------------------------------------------------------------------------
FREQ_ANNUAL    = "A"
FREQ_QUARTERLY = "Q"
FREQ_MONTHLY   = "M"

# ---------------------------------------------------------------------------
# Common ISO-2 country code groups
# ---------------------------------------------------------------------------
G7_COUNTRIES  = "CAN+USA+GBR+DEU+FRA+ITA+JPN"
G20_COUNTRIES = "CAN+USA+GBR+DEU+FRA+ITA+JPN+AUS+BRA+CHN+IND+IDN+MEX+RUS+SAU+ZAF+KOR+TUR+ARG+EUN"
EU_COUNTRIES  = "AUT+BEL+BGR+HRV+CYP+CZE+DNK+EST+FIN+FRA+DEU+GRC+HUN+IRL+ITA+LVA+LTU+LUX+MLT+NLD+POL+PRT+ROU+SVK+SVN+ESP+SWE"


# ---------------------------------------------------------------------------
# Helper utilities
# ---------------------------------------------------------------------------

def _build_dataflow_id(indicator_code: str) -> str:
    """Build the SDMX dataflow ID from a plain indicator code."""
    if indicator_code.startswith("DF_"):
        return indicator_code
    return f"DF_{indicator_code}"


def _session() -> requests.Session:
    """Return a pre-configured requests Session."""
    s = requests.Session()
    s.headers.update({
        "User-Agent": "finterm-Terminal/4.0.2 (ILOSTAT-wrapper)",
        "Accept-Language": "en",
    })
    return s


def _parse_csv_response(text: str) -> List[Dict[str, Any]]:
    """
    Parse a CSV text body into a list of dicts.
    Uses the stdlib csv module to avoid requiring pandas.
    """
    import csv
    reader = csv.DictReader(io.StringIO(text))
    rows = []
    for row in reader:
        cleaned = {}
        for k, v in row.items():
            k = k.strip().strip('"')
            v = v.strip()
            # Try numeric conversion
            try:
                cleaned[k] = float(v) if "." in v else int(v)
            except (ValueError, TypeError):
                cleaned[k] = v if v != "" else None
        rows.append(cleaned)
    return rows


def _safe_float(val: Any) -> Optional[float]:
    try:
        return float(val)
    except (TypeError, ValueError):
        return None


# ---------------------------------------------------------------------------
# Core SDMX REST query builder
# ---------------------------------------------------------------------------

class ILOStatError:
    """Structured error container for host JSON consumption."""

    def __init__(self, endpoint: str, error: str, status_code: Optional[int] = None):
        self.endpoint    = endpoint
        self.error       = error
        self.status_code = status_code
        self.timestamp   = int(datetime.now().timestamp())

    def to_dict(self) -> Dict[str, Any]:
        return {
            "endpoint":    self.endpoint,
            "error":       self.error,
            "status_code": self.status_code,
            "timestamp":   self.timestamp,
            "type":        "ILOStatError",
        }


class ILOStatWrapper:
    """
    Complete Python wrapper for the ILOSTAT SDMX REST API and Bulk Download Facility.

    SDMX REST API base URL:  https://sdmx.ilo.org/rest
    Agency ID:               ILO  (always)
    Dataflow pattern:        ILO,DF_<DSD_ID>[,<version>]

    Supported resource types:
        data            — actual observations
        dataflow        — list / describe dataflows (indicators)
        datastructure   — DSD (dimensions, attributes, measures)
        codelist        — code lists (e.g. CL_AREA, CL_SEX)
        conceptscheme   — concept schemes
        categoryscheme  — category schemes
        categorisation  — categorisations

    Data URL format:
        {base}/data/{agency},{flow}[,{version}]/{key}
            ?startPeriod=YYYY[-MM[-DD]]
            &endPeriod=YYYY[-MM[-DD]]
            &detail=dataonly|serieskeysonly|full
            &updatedAfter=YYYY-MM-DDThh:mm:ss
            &firstNObservations=N
            &lastNObservations=N
            &dimensionAtObservation=AllDimensions|TIME_PERIOD
        Accept: text/csv  →  returns SDMX-CSV
        Accept: application/json  →  returns SDMX-JSON (complex nested structure)

    Key format (dot-separated dimension values, in DSD-defined order):
        FREQ.REF_AREA.MEASURE.SEX.CLASSIF1[.CLASSIF2]
        Omit a dimension to wildcard (consecutive dots mean wildcard).
        Use + for OR:  CAN+USA+GBR
        Use ALL to wildcard every dimension.

    Example calls:
        # Canada monthly unemployment rate, all sexes, adults 15+
        GET https://sdmx.ilo.org/rest/data/ILO,DF_UNE_DEAP_SEX_AGE_RT,1.0/CAN.M.UNE_DEAP_RT.SEX_T.AGE_YTHADULT_YGE15
            ?startPeriod=2010&format=csv

        # All countries, annual employment by sex & age
        GET https://sdmx.ilo.org/rest/data/ILO,DF_EMP_TEMP_SEX_AGE_NB/ALL
            ?startPeriod=2000&endPeriod=2023&format=csv

    Bulk Download:
        Indicator CSV:  https://www.ilo.org/ilostat-files/WEB_bulk_download/indicator/{INDICATOR_CODE}.csv.gz
        TOC:            https://www.ilo.org/ilostat-files/WEB_bulk_download/indicator/table_of_contents_en.csv
    """

    def __init__(self):
        self.sdmx_base   = SDMX_BASE_URL
        self.bulk_base   = BULK_BASE_URL
        self.session     = _session()

    # ------------------------------------------------------------------
    # Internal request helpers
    # ------------------------------------------------------------------

    def _get_csv(self, url: str, params: Optional[Dict] = None) -> str:
        """Perform a GET request expecting CSV; return raw text."""
        headers = {"Accept": "text/csv"}
        resp = self.session.get(url, params=params, headers=headers, timeout=DEFAULT_TIMEOUT)
        resp.raise_for_status()
        return resp.text

    def _get_json(self, url: str, params: Optional[Dict] = None) -> Dict[str, Any]:
        """Perform a GET request expecting JSON."""
        headers = {"Accept": "application/json"}
        resp = self.session.get(url, params=params, headers=headers, timeout=DEFAULT_TIMEOUT)
        resp.raise_for_status()
        return resp.json()

    def _get_xml(self, url: str, params: Optional[Dict] = None) -> str:
        """Perform a GET request expecting XML text."""
        headers = {"Accept": "application/xml"}
        resp = self.session.get(url, params=params, headers=headers, timeout=DEFAULT_TIMEOUT)
        resp.raise_for_status()
        return resp.text

    # ------------------------------------------------------------------
    # Structural / metadata endpoints
    # ------------------------------------------------------------------

    def get_codelist(self, codelist_id: str, agency: str = "ILO") -> Dict[str, Any]:
        """
        Retrieve a specific codelist by ID.

        Calls: GET {base}/codelist/{agency}/{codelist_id}

        Common codelist IDs:
            CL_AREA  — ISO country / region codes used by ILO
            CL_SEX   — Sex disaggregation codes (SEX_T, SEX_M, SEX_F)
            CL_FREQ  — Frequency codes (A, Q, M)
            CL_ECO   — Economic activity classification (ISIC)
            CL_AGE   — Age group classification
            CL_OCU   — Occupation classification (ISCO)

        Returns dict with list of code entries.
        """
        try:
            url = f"{self.sdmx_base}/codelist/{agency}/{codelist_id}"
            xml_text = self._get_xml(url)

            import re
            # Extract Code elements
            codes = []
            code_blocks = re.findall(
                r'<(?:str:|structure:)?Code\b([^>]*)>(.*?)</(?:str:|structure:)?Code>',
                xml_text,
                re.DOTALL | re.IGNORECASE,
            )
            for attrs_str, body in code_blocks:
                code_id_m = re.search(r'\bid=["\']([^"\']+)["\']', attrs_str)
                name_m    = re.search(r'<(?:com:|common:)?Name[^>]*>([^<]+)</(?:com:|common:)?Name>', body, re.IGNORECASE)
                code_id   = code_id_m.group(1) if code_id_m else "unknown"
                code_name = name_m.group(1).strip() if name_m else code_id
                codes.append({"id": code_id, "label": code_name})

            return {
                "success":      True,
                "codelist_id":  codelist_id,
                "agency":       agency,
                "data":         codes,
                "count":        len(codes),
            }

        except requests.exceptions.HTTPError as e:
            return {"error": ILOStatError("get_codelist", str(e), e.response.status_code).to_dict()}
        except Exception as e:
            return {"error": ILOStatError("get_codelist", str(e)).to_dict()}

    def get_data_structure(self, dsd_id: str) -> Dict[str, Any]:
        """
        Retrieve the Data Structure Definition (DSD) for a given indicator.

        Calls: GET {base}/datastructure/ILO/{dsd_id}

        The DSD describes all dimensions, their order, and the codelists they reference.
        The dsd_id is the same as the indicator code (without the DF_ prefix).

        Example:  dsd_id = "EMP_TEMP_SEX_AGE_NB"
        """
        try:
            url = f"{self.sdmx_base}/datastructure/ILO/{dsd_id}"
            xml_text = self._get_xml(url)
            return {
                "success":  True,
                "dsd_id":   dsd_id,
                "raw_xml":  xml_text,
            }
        except requests.exceptions.HTTPError as e:
            return {"error": ILOStatError("get_data_structure", str(e), e.response.status_code).to_dict()}
        except Exception as e:
            return {"error": ILOStatError("get_data_structure", str(e)).to_dict()}

    # ------------------------------------------------------------------
    # Table of Contents (bulk download)
    # ------------------------------------------------------------------

    def get_table_of_contents(
        self,
        segment:  str = "indicator",
        language: str = "en",
        search:   Optional[str] = None,
    ) -> Dict[str, Any]:
        """
        Download and parse the ILOSTAT Table of Contents CSV.

        This is the definitive catalogue of ~500 indicator datasets. Each row
        contains the dataset id, frequency, time range, last update, and file size.

        Args:
            segment:  "indicator" (default) or "ref_area"
            language: "en" (default), "fr", or "es"
            search:   Optional case-insensitive substring to filter indicator rows.

        TOC URL:  https://rplumber.ilo.org/metadata/toc/{segment}
                  (CSV response; the old www.ilo.org bulk-download CSV URLs are no longer publicly accessible)

        Returns dict:
            {
              "success": True,
              "data": [{"id": "EMP_TEMP_SEX_AGE_NB_A", "indicator": "...", ...}, ...],
              "count": N
            }
        """
        try:
            # rplumber.ilo.org returns CSV directly for both segments
            # The language parameter is not supported by this endpoint (English only)
            url = f"{TOC_BASE_URL}/{segment}"
            text = self._get_csv(url)
            rows = _parse_csv_response(text)

            if search:
                search_lower = search.lower()
                rows = [
                    r for r in rows
                    if any(search_lower in str(v).lower() for v in r.values())
                ]

            return {
                "success":    True,
                "data":       rows,
                "count":      len(rows),
                "parameters": {"segment": segment, "language": language, "search": search},
            }

        except requests.exceptions.HTTPError as e:
            return {"error": ILOStatError("get_table_of_contents", str(e), e.response.status_code).to_dict()}
        except Exception as e:
            return {"error": ILOStatError("get_table_of_contents", str(e)).to_dict()}

    # ------------------------------------------------------------------
    # Data retrieval — SDMX REST API
    # ------------------------------------------------------------------

    def get_data(
        self,
        indicator_code: str,
        key:            str = "ALL",
        start_period:   Optional[str] = None,
        end_period:     Optional[str] = None,
        detail:         str = "dataonly",
        last_n_obs:     Optional[int] = None,
        first_n_obs:    Optional[int] = None,
        version:        str = "1.0",
    ) -> Dict[str, Any]:
        """
        Core data query against the ILOSTAT SDMX REST API.

        Calls:
            GET {base}/data/ILO,DF_{indicator_code},{version}/{key}
                ?startPeriod=...&endPeriod=...&detail=...

        Args:
            indicator_code: The indicator/DSD code without the DF_ prefix.
                            E.g. "UNE_DEAP_SEX_AGE_RT" or "EMP_TEMP_SEX_AGE_NB"
            key:            Dot-separated dimension key. "ALL" wildcards everything.
                            Format: FREQ.REF_AREA.MEASURE.SEX.CLASSIF1[.CLASSIF2]
                            Example: "A.DEU.UNE_DEAP_RT.SEX_T.AGE_YTHADULT_YGE15"
                            Use + for OR: "CAN+USA+GBR.A..."
            start_period:   Start year/date in ISO format (e.g. "2010" or "2010-01").
            end_period:     End year/date in ISO format.
            detail:         Level of detail:
                              "dataonly"       — observations only (fastest)
                              "full"           — observations + all attributes
                              "serieskeysonly" — no observations, just dimension keys
                              "nodata"         — structure only, no obs values
            last_n_obs:     Return only the last N observations per series.
            first_n_obs:    Return only the first N observations per series.
            version:        Dataflow version (default "1.0").

        Returns:
            {
              "success": True,
              "indicator": "UNE_DEAP_SEX_AGE_RT",
              "dataflow":  "ILO,DF_UNE_DEAP_SEX_AGE_RT,1.0",
              "key":       "ALL",
              "data": [
                {
                  "FREQ": "A", "REF_AREA": "USA", "MEASURE": "UNE_DEAP_RT",
                  "SEX": "SEX_T", "AGE": "AGE_YTHADULT_YGE15",
                  "TIME_PERIOD": "2022", "OBS_VALUE": 3.6
                }, ...
              ],
              "count": N,
              "parameters": {...}
            }
        """
        try:
            # Build the dataflow reference
            df_id    = _build_dataflow_id(indicator_code)
            flow_ref = f"ILO,{df_id},{version}"
            url      = f"{self.sdmx_base}/data/{flow_ref}/{key}"

            params: Dict[str, Any] = {"detail": detail}
            if start_period:
                params["startPeriod"] = start_period
            if end_period:
                params["endPeriod"] = end_period
            if last_n_obs is not None:
                params["lastNObservations"] = last_n_obs
            if first_n_obs is not None:
                params["firstNObservations"] = first_n_obs

            csv_text = self._get_csv(url, params=params)
            rows     = _parse_csv_response(csv_text)

            # Coerce OBS_VALUE to float where possible
            for row in rows:
                if "OBS_VALUE" in row and row["OBS_VALUE"] is not None:
                    row["OBS_VALUE"] = _safe_float(row["OBS_VALUE"])

            return {
                "success":    True,
                "indicator":  indicator_code,
                "dataflow":   flow_ref,
                "key":        key,
                "data":       rows,
                "count":      len(rows),
                "parameters": {
                    "start_period": start_period,
                    "end_period":   end_period,
                    "detail":       detail,
                    "last_n_obs":   last_n_obs,
                    "first_n_obs":  first_n_obs,
                },
            }

        except requests.exceptions.HTTPError as e:
            sc = e.response.status_code if e.response is not None else None
            msg = str(e)
            if sc == 413:
                msg = (
                    "HTTP 413 Payload Too Large: query returned too many results. "
                    "Narrow the key (add country, frequency, or classification filters)."
                )
            return {"error": ILOStatError("get_data", msg, sc).to_dict()}
        except Exception as e:
            return {"error": ILOStatError("get_data", str(e)).to_dict()}

    # ------------------------------------------------------------------
    # High-level, domain-specific convenience methods
    # ------------------------------------------------------------------

    def get_unemployment(
        self,
        countries:    str = "ALL",
        frequency:    str = FREQ_ANNUAL,
        sex:          str = "SEX_T",
        age_group:    str = "AGE_YTHADULT_YGE15",
        start_period: Optional[str] = None,
        end_period:   Optional[str] = None,
    ) -> Dict[str, Any]:
        """
        Fetch unemployment rate data.

        Indicator:  DF_UNE_DEAP_SEX_AGE_RT  (ILO modelled estimates — unemployment rate)
        Dataflow:   https://sdmx.ilo.org/rest/data/ILO,DF_UNE_DEAP_SEX_AGE_RT,1.0/{key}

        Dimension key order:  REF_AREA . FREQ . MEASURE . SEX . AGE

        Args:
            countries:    ISO-2 country code(s). Use + for multiple: "USA+DEU+FRA"
                          Use "ALL" to get all countries (may be slow / 413).
            frequency:    "A" (annual), "Q" (quarterly), "M" (monthly)
            sex:          "SEX_T" (total), "SEX_M" (male), "SEX_F" (female)
            age_group:    Common values:
                              "AGE_YTHADULT_YGE15"   — 15 and over (standard)
                              "AGE_YTHADULT_Y15-24"  — youth 15-24
                              "AGE_YTHADULT_YGE25"   — adult 25 and over
            start_period: e.g. "2000"
            end_period:   e.g. "2023"

        Example URL:
            https://sdmx.ilo.org/rest/data/ILO,DF_UNE_DEAP_SEX_AGE_RT,1.0/
                USA.A.UNE_DEAP_RT.SEX_T.AGE_YTHADULT_YGE15?startPeriod=2010

        Returns standard data dict.
        """
        # Key: REF_AREA.FREQ.MEASURE.SEX.AGE
        measure = "UNE_DEAP_RT"
        key = f"{countries}.{frequency}.{measure}.{sex}.{age_group}"

        result = self.get_data(
            indicator_code = "UNE_DEAP_SEX_AGE_RT",
            key            = key,
            start_period   = start_period,
            end_period     = end_period,
        )
        result["topic"] = "unemployment_rate"
        return result

    def get_employment_to_population_ratio(
        self,
        countries:    str = "ALL",
        frequency:    str = FREQ_ANNUAL,
        sex:          str = "SEX_T",
        age_group:    str = "AGE_YTHADULT_YGE15",
        start_period: Optional[str] = None,
        end_period:   Optional[str] = None,
    ) -> Dict[str, Any]:
        """
        Fetch employment-to-population ratio.

        Indicator: DF_EMP_DWAP_SEX_AGE_RT
        """
        measure = "EMP_DWAP_RT"
        key = f"{countries}.{frequency}.{measure}.{sex}.{age_group}"
        result = self.get_data(
            indicator_code = "EMP_DWAP_SEX_AGE_RT",
            key            = key,
            start_period   = start_period,
            end_period     = end_period,
        )
        result["topic"] = "employment_to_population_ratio"
        return result

    def get_labour_force_participation(
        self,
        countries:    str = "ALL",
        frequency:    str = FREQ_ANNUAL,
        sex:          str = "SEX_T",
        age_group:    str = "AGE_YTHADULT_YGE15",
        start_period: Optional[str] = None,
        end_period:   Optional[str] = None,
    ) -> Dict[str, Any]:
        """
        Fetch labour force participation rate.

        Indicator: DF_EAP_DWAP_SEX_AGE_RT  (labour force participation rate)
        """
        measure = "EAP_DWAP_RT"
        key = f"{countries}.{frequency}.{measure}.{sex}.{age_group}"
        result = self.get_data(
            indicator_code = "EAP_DWAP_SEX_AGE_RT",
            key            = key,
            start_period   = start_period,
            end_period     = end_period,
        )
        result["topic"] = "labour_force_participation_rate"
        return result

    def get_wages(
        self,
        countries:        str = "ALL",
        frequency:        str = FREQ_ANNUAL,
        wage_type:        str = "monthly",
        sex:              str = "SEX_T",
        currency:         str = "CUR_TYPE_LCU",
        start_period:     Optional[str] = None,
        end_period:       Optional[str] = None,
    ) -> Dict[str, Any]:
        """
        Fetch mean earnings data (hourly or monthly).

        Verified dataflow IDs (from the live SDMX /dataflow/ILO endpoint):
            hourly  → DF_EAR_4HRL_SEX_CUR_NB
                      DSD dims: REF_AREA.FREQ.MEASURE.SEX.CUR
                      Measure:  EAR_4HRL_NB
            monthly → DF_EAR_4MTH_SEX_CUR_NB
                      DSD dims: REF_AREA.FREQ.MEASURE.SEX.CUR
                      Measure:  EAR_4MTH_NB
            minimum → DF_EAR_4MMN_CUR_NB  (statutory minimum wage, no sex dimension)
                      DSD dims: REF_AREA.FREQ.MEASURE.CUR
                      Measure:  EAR_4MMN_NB

        Args:
            wage_type: "hourly" | "monthly" (default) | "minimum"
            currency:  CL_CUR codelist value:
                           "CUR_TYPE_LCU"  — Local currency units [default]
                           "CUR_TYPE_USD"  — US Dollars
                           "CUR_TYPE_EUR"  — Euros (where applicable)
                           "CUR_TYPE_PPP"  — PPP-adjusted international dollars

        Example (Germany monthly wages in LCU):
            https://sdmx.ilo.org/rest/data/ILO,DF_EAR_4MTH_SEX_CUR_NB,1.0/
                DEU.A.EAR_4MTH_NB.SEX_T.CUR_TYPE_LCU?startPeriod=2010
        """
        if wage_type == "hourly":
            indicator_code = "EAR_4HRL_SEX_CUR_NB"
            measure        = "EAR_4HRL_NB"
            key            = f"{countries}.{frequency}.{measure}.{sex}.{currency}"
        elif wage_type == "minimum":
            indicator_code = "EAR_4MMN_CUR_NB"
            measure        = "EAR_4MMN_NB"
            # Minimum wage DSD has no SEX dimension: REF_AREA.FREQ.MEASURE.CUR
            key            = f"{countries}.{frequency}.{measure}.{currency}"
        else:  # monthly (default)
            indicator_code = "EAR_4MTH_SEX_CUR_NB"
            measure        = "EAR_4MTH_NB"
            key            = f"{countries}.{frequency}.{measure}.{sex}.{currency}"

        result = self.get_data(
            indicator_code = indicator_code,
            key            = key,
            start_period   = start_period,
            end_period     = end_period,
        )
        result["topic"] = f"wages_{wage_type}"
        return result

    def get_hours_of_work(
        self,
        countries:      str = "ALL",
        frequency:      str = FREQ_ANNUAL,
        sex:            str = "SEX_T",
        economic_act:   str = "ECO_ISIC4_TOTAL",
        start_period:   Optional[str] = None,
        end_period:     Optional[str] = None,
    ) -> Dict[str, Any]:
        """
        Fetch mean weekly hours of work.

        Indicator:       DF_HOW_TEMP_SEX_ECO_NB  (mean weekly hours by sex & economic activity)
        DSD dimension order: REF_AREA . FREQ . MEASURE . SEX . ECO

        Common ECO codes (same CL_ECO codelist as employment by sector):
            "ECO_ISIC4_TOTAL"  — Total (all sectors) [default]
            "ECO_ISIC4_A"      — Agriculture, forestry and fishing
            "ECO_ISIC4_C"      — Manufacturing
            "ECO_ISIC4_F"      — Construction
            "ECO_ISIC4_G"      — Wholesale and retail trade
            "ECO_ISIC4_K"      — Financial and insurance activities
            "ECO_ISIC4_O"      — Public administration and defence
            "ECO_ISIC4_P"      — Education
            "ECO_ISIC4_Q"      — Human health and social work

        Pass economic_act="" to wildcard (all sector breakdowns).
        """
        measure = "HOW_TEMP_NB"
        if economic_act:
            key = f"{countries}.{frequency}.{measure}.{sex}.{economic_act}"
        else:
            key = f"{countries}.{frequency}.{measure}.{sex}."
        result = self.get_data(
            indicator_code = "HOW_TEMP_SEX_ECO_NB",
            key            = key,
            start_period   = start_period,
            end_period     = end_period,
        )
        result["topic"] = "hours_of_work_mean_weekly"
        return result

    def get_labour_income_share(
        self,
        countries:    str = "ALL",
        frequency:    str = FREQ_ANNUAL,
        start_period: Optional[str] = None,
        end_period:   Optional[str] = None,
    ) -> Dict[str, Any]:
        """
        Fetch labour income share as % of GDP (ILO modelled estimates).

        Indicator: DF_LAP_2GDP_NOC_RT
        SDG 10.4.1 proxy indicator.
        """
        measure = "LAP_2GDP_RT"
        key = f"{countries}.{frequency}.{measure}"
        result = self.get_data(
            indicator_code = "LAP_2GDP_NOC_RT",
            key            = key,
            start_period   = start_period,
            end_period     = end_period,
        )
        result["topic"] = "labour_income_share"
        return result

    def get_country_profile(
        self,
        country:      str,
        start_period: Optional[str] = None,
        end_period:   Optional[str] = None,
    ) -> Dict[str, Any]:
        """
        Fetch a comprehensive labour market profile for a single country.

        Runs multiple queries and bundles results into one response.
        Each sub-query falls back gracefully on failure.

        Args:
            country:      ISO-2 country code, e.g. "DEU", "IND", "BRA"
            start_period: e.g. "2010"
            end_period:   e.g. "2023"

        Returns:
            {
              "success": True,
              "country": "DEU",
              "data": {
                "unemployment_rate": {...},
                "employment_to_pop": {...},
                "labour_force_participation": {...},
                "hours_of_work": {...},
                "wages_monthly": {...},
                "labour_income_share": {...},
              },
              "parameters": {...}
            }
        """
        try:
            profile: Dict[str, Any] = {}

            # Unemployment rate
            try:
                profile["unemployment_rate"] = self.get_unemployment(
                    countries=country, start_period=start_period, end_period=end_period
                )
            except Exception as exc:
                profile["unemployment_rate"] = {"error": str(exc)}

            # Employment-to-population ratio
            try:
                profile["employment_to_pop"] = self.get_employment_to_population_ratio(
                    countries=country, start_period=start_period, end_period=end_period
                )
            except Exception as exc:
                profile["employment_to_pop"] = {"error": str(exc)}

            # Labour force participation rate
            try:
                profile["labour_force_participation"] = self.get_labour_force_participation(
                    countries=country, start_period=start_period, end_period=end_period
                )
            except Exception as exc:
                profile["labour_force_participation"] = {"error": str(exc)}

            # Mean weekly hours of work
            try:
                profile["hours_of_work"] = self.get_hours_of_work(
                    countries=country, start_period=start_period, end_period=end_period
                )
            except Exception as exc:
                profile["hours_of_work"] = {"error": str(exc)}

            # Monthly wages
            try:
                profile["wages_monthly"] = self.get_wages(
                    countries=country, wage_type="monthly",
                    start_period=start_period, end_period=end_period
                )
            except Exception as exc:
                profile["wages_monthly"] = {"error": str(exc)}

            # Labour income share
            try:
                profile["labour_income_share"] = self.get_labour_income_share(
                    countries=country, start_period=start_period, end_period=end_period
                )
            except Exception as exc:
                profile["labour_income_share"] = {"error": str(exc)}

            return {
                "success":    True,
                "country":    country,
                "data":       profile,
                "parameters": {
                    "country":      country,
                    "start_period": start_period,
                    "end_period":   end_period,
                },
            }

        except Exception as e:
            return {"error": ILOStatError("get_country_profile", str(e)).to_dict()}

    # ------------------------------------------------------------------
    # Bulk download method (for large datasets)
    # ------------------------------------------------------------------

    # ------------------------------------------------------------------
    # Search / discovery helpers
    # ------------------------------------------------------------------

    def search_indicators(self, query: str, language: str = "en") -> Dict[str, Any]:
        """
        Search the ILOSTAT table of contents for matching indicators.

        Wraps get_table_of_contents() with a search filter.

        Args:
            query:    Free-text search string (case-insensitive substring match).
            language: "en", "fr", or "es"

        Returns standard data dict with matching TOC rows.
        """
        return self.get_table_of_contents(segment="indicator", language=language, search=query)

    def list_countries(self) -> Dict[str, Any]:
        """
        Return the ILO area code list (ISO-2 + ILO region aggregates).

        Calls: GET {base}/codelist/ILO/CL_AREA
        """
        return self.get_codelist("CL_AREA")

# ---------------------------------------------------------------------------
# CLI entry point (invoked by C++ Python bridge)
# ---------------------------------------------------------------------------

COMMANDS = {
    # Short aliases (preferred)
    "unemployment":             "[countries] [frequency] [sex] [age_group] [start] [end]",
    "employment":               "[countries] [frequency] [sex] [age_group] [start] [end]",
    "emp_to_pop":               "[countries] [frequency] [sex] [age_group] [start] [end]",
    "lfpr":                     "[countries] [frequency] [sex] [age_group] [start] [end]",
    "wages":                    "[countries] [frequency] [wage_type] [sex] [currency] [start] [end]",
    "gender_wage_gap":          "[countries] [frequency] [occupation] [start] [end]",
    "hours_of_work":            "[countries] [frequency] [sex] [economic_act] [start] [end]",
    "labour_income_share":      "[countries] [frequency] [start] [end]",
    "working_poverty":          "[countries] [frequency] [sex] [age_group] [start] [end]",
    "emp_by_sector":            "[countries] [frequency] [sex] [sector] [start] [end]",
    "emp_by_occupation":        "[countries] [frequency] [sex] [occupation] [start] [end]",
    "emp_by_status":            "[countries] [frequency] [sex] [status] [start] [end]",
    "informal_employment":      "[countries] [frequency] [sex] [start] [end]",
    "social_protection":        "[countries] [frequency] [sex] [start] [end]",
    "country_profile":          "<country> [start] [end]",
    "sdg_indicator":            "<sdg_code> [countries] [frequency] [start] [end]",
    "bulk_data":                "<indicator_id> [ref_area] [start] [end] [max_rows]",
    "search":                   "<query> [language]",
    "dataflows":                "(topic_prefix?)",
    "codelist":                 "<codelist_id> [agency]",
    "toc":                      "[segment] [language] [search]",
    "data":                     "<indicator_code> [key] [start] [end] [detail]",
    "countries":                "(no args)",
    "available":                "(no args)",
    # Verbose aliases (also supported)
    "get_data":                         "<indicator_code> [key] [start] [end] [detail]",
    "get_unemployment":                 "[countries] [frequency] [sex] [age_group] [start] [end]",
    "get_employment_to_population":     "[countries] [frequency] [sex] [age_group] [start] [end]",
    "get_labour_force_participation":   "[countries] [frequency] [sex] [age_group] [start] [end]",
    "list_countries":                   "(no args)",
}


def _arg(args: List[str], index: int, default: Any = None) -> Any:
    """Safe list index access with default."""
    try:
        val = args[index]
        return val if val else default
    except IndexError:
        return default


def main(args: Optional[List[str]] = None) -> None:
    if args is None:
        args = sys.argv[1:]

    if not args:
        print(json.dumps({
            "error": "No command provided.",
            "usage": "python ilostat_data.py <command> [args...]",
            "commands": COMMANDS,
        }, indent=2))
        sys.exit(1)

    command = args[0]
    rest    = args[1:]
    wrapper = ILOStatWrapper()

    # Normalize short aliases to canonical names
    _ALIASES = {
        "unemployment":        "get_unemployment",
        "emp_to_pop":          "get_employment_to_population",
        "lfpr":                "get_labour_force_participation",
        "data":                "get_data",
        "countries":           "list_countries",
    }
    command = _ALIASES.get(command, command)

    try:
        if command == "get_data":
            if not rest:
                result = {"error": "get_data requires <indicator_code>"}
            else:
                result = wrapper.get_data(
                    indicator_code = rest[0],
                    key            = _arg(rest, 1, "ALL"),
                    start_period   = _arg(rest, 2),
                    end_period     = _arg(rest, 3),
                    detail         = _arg(rest, 4, "dataonly"),
                )

        elif command == "get_unemployment":
            result = wrapper.get_unemployment(
                countries    = _arg(rest, 0, "ALL"),
                frequency    = _arg(rest, 1, FREQ_ANNUAL),
                sex          = _arg(rest, 2, "SEX_T"),
                age_group    = _arg(rest, 3, "AGE_YTHADULT_YGE15"),
                start_period = _arg(rest, 4),
                end_period   = _arg(rest, 5),
            )

        elif command == "get_employment_to_population":
            result = wrapper.get_employment_to_population_ratio(
                countries    = _arg(rest, 0, "ALL"),
                frequency    = _arg(rest, 1, FREQ_ANNUAL),
                sex          = _arg(rest, 2, "SEX_T"),
                age_group    = _arg(rest, 3, "AGE_YTHADULT_YGE15"),
                start_period = _arg(rest, 4),
                end_period   = _arg(rest, 5),
            )

        elif command == "get_labour_force_participation":
            result = wrapper.get_labour_force_participation(
                countries    = _arg(rest, 0, "ALL"),
                frequency    = _arg(rest, 1, FREQ_ANNUAL),
                sex          = _arg(rest, 2, "SEX_T"),
                age_group    = _arg(rest, 3, "AGE_YTHADULT_YGE15"),
                start_period = _arg(rest, 4),
                end_period   = _arg(rest, 5),
            )

        elif command == "list_countries":
            result = wrapper.list_countries()

        else:
            result = {
                "error":    f"Unknown command: {command}",
                "commands": COMMANDS,
            }

        print(json.dumps(result, indent=2, ensure_ascii=False))

    except Exception as exc:
        print(json.dumps({
            "error":     str(exc),
            "traceback": traceback.format_exc(),
        }, indent=2))
        sys.exit(1)


if __name__ == "__main__":
    main()
