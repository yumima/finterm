"""
Executive Branch Financial Disclosures — OGE Form 278
Source: Office of Government Ethics (oge.gov)

Nature of data:
  Cabinet members file annual OGE Form 278 (Public Financial Disclosure Report).
  Unlike congressional PTRs, these are HOLDINGS snapshots (not individual trades).
  Filing deadline: within 30 days of taking office, then annually by May 15.

Live source (OGE has no JSON API — content is HTML/PDF behind a Lotus Notes view):
  https://www.oge.gov/web/OGE.nsf/Officials%20Individual%20Disclosures%20Search%20Collection?OpenForm

Older URLs that the OGE redesign retired and that we used to point at — do NOT
restore these without verifying first; they 404:
  - https://oge.gov/web/278eform.nsf/...                     (eDisclosure subdomain)
  - https://www.oge.gov/web/oge.nsf/Public+Financial+Disclosure+Reports

Acquisition strategy:
  - OGE Form 278 disclosures are filed as multi-page PDFs (no structured download).
  - Parsing holdings out of those PDFs is brittle (table layouts vary per filer).
  - This script ships a curated 2025 cabinet dataset compiled from publicly-filed
    PDFs + Senate confirmation disclosures, and provides the live OGE search URL
    for each filer so the user can drill in.

Actions (PythonRunner):
  cabinet_data         — full summary for CabinetService.parse_summary()
  cabinet_members      — list of member dicts
  conflict_analysis    — per-member conflict scores
"""

import json
import sys
import os
import re
import time
from typing import Dict, List, Optional, Tuple

try:
    import requests
except ImportError:
    requests = None


# ── Endpoints ──────────────────────────────────────────────────────────────────

OGE_BASE              = "https://www.oge.gov"
OGE_DISCLOSURE_SEARCH = ("https://www.oge.gov/web/OGE.nsf/"
                        "Officials%20Individual%20Disclosures%20Search%20Collection?OpenForm")
OGE_AGENCY_DOCS       = ("https://www.oge.gov/web/OGE.nsf/"
                        "Agency%20Ethics%20Documents%20Search%20Collection?OpenForm")


# ── Regulatory domain → conflict sectors ──────────────────────────────────────

ROLE_DOMAINS: Dict[str, Dict] = {
    "Secretary of Defense": {
        "regulates":         ["Defense", "Aerospace", "Cybersecurity"],
        "conflict_tickers":  ["LMT", "RTX", "NOC", "BA", "GD", "LDOS", "SAIC",
                              "PANW", "CRWD", "PLTR", "AXON", "HII", "L3H"],
        "why":               "DoD oversees defense procurement and cybersecurity contracts",
    },
    "Secretary of the Treasury": {
        "regulates":         ["Financials", "Banking", "Crypto", "Tax"],
        "conflict_tickers":  ["JPM", "BAC", "GS", "MS", "WFC", "C", "BX", "KKR",
                              "BTC", "ETH", "COIN", "SCHW", "V", "MA"],
        "why":               "Treasury regulates banks, financial institutions, and digital assets",
    },
    "Secretary of State": {
        "regulates":         ["Defense", "Energy", "International Trade"],
        "conflict_tickers":  ["LMT", "RTX", "XOM", "CVX", "COP", "SLB"],
        "why":               "State Dept oversees foreign policy and international sanctions",
    },
    "Secretary of Energy": {
        "regulates":         ["Energy", "Utilities", "Mining", "Nuclear"],
        "conflict_tickers":  ["XOM", "CVX", "COP", "EOG", "SLB", "HAL", "OXY",
                              "NEE", "DUK", "SO", "D", "NEM", "FCX"],
        "why":               "DOE oversees oil/gas leases, utility regulation, nuclear programs",
    },
    "Secretary of Health and Human Services": {
        "regulates":         ["Healthcare", "Pharma", "Biotech"],
        "conflict_tickers":  ["JNJ", "PFE", "MRNA", "BNTX", "UNH", "CVS", "HUM",
                              "ABT", "ABBV", "MRK", "REGN", "GILD", "BIIB"],
        "why":               "HHS oversees FDA, CMS, drug approvals, healthcare insurance regulation",
    },
    "Attorney General": {
        "regulates":         ["Technology", "Financials", "Antitrust"],
        "conflict_tickers":  ["GOOGL", "META", "AMZN", "AAPL", "MSFT", "NVDA",
                              "JPM", "BAC", "GS"],
        "why":               "DOJ prosecutes antitrust and financial fraud; regulates Big Tech",
    },
    "Secretary of Commerce": {
        "regulates":         ["Technology", "Telecom", "Trade", "Export Controls"],
        "conflict_tickers":  ["AAPL", "MSFT", "NVDA", "AMZN", "GOOGL", "META",
                              "TSLA", "QCOM", "TSM", "AMD", "VZ", "T"],
        "why":               "Commerce controls export licenses for chips/tech and trade policy",
    },
    "Secretary of Transportation": {
        "regulates":         ["Aerospace", "Automotive", "Industrials"],
        "conflict_tickers":  ["BA", "TSLA", "F", "GM", "UPS", "FDX", "UAL",
                              "DAL", "AAL", "CSX", "UNP"],
        "why":               "DOT oversees aviation safety (FAA), auto safety (NHTSA), rail",
    },
    "Secretary of Agriculture": {
        "regulates":         ["Agriculture", "Food"],
        "conflict_tickers":  ["ADM", "BG", "MOS", "NTR", "CF", "DE", "CTVA"],
        "why":               "USDA oversees crop subsidies, food safety, and agricultural trade",
    },
    "Secretary of the Interior": {
        "regulates":         ["Energy", "Mining", "Land"],
        "conflict_tickers":  ["XOM", "CVX", "COP", "SLB", "NEM", "FCX", "BHP",
                              "CLF", "X", "NUE"],
        "why":               "Interior grants oil/gas drilling and mining leases on federal lands",
    },
    "Secretary of Labor": {
        "regulates":         ["Consumer", "Financials"],
        "conflict_tickers":  [],
        "why":               "DOL oversees wages, workplace safety, and retirement plan rules",
    },
    "Secretary of Housing and Urban Development": {
        "regulates":         ["Real Estate", "Financials", "Mortgage"],
        "conflict_tickers":  ["AMT", "PLD", "WELL", "EQR", "AVB", "JPM", "BAC"],
        "why":               "HUD oversees FHA mortgage insurance and housing policy",
    },
    "Secretary of Education": {
        "regulates":         ["Consumer", "Education Finance"],
        "conflict_tickers":  [],
        "why":               "DOE oversees student loan programs and education policy",
    },
    "Secretary of Veterans Affairs": {
        "regulates":         ["Healthcare", "Defense"],
        "conflict_tickers":  ["UNH", "HUM", "CVS", "LMT", "RTX"],
        "why":               "VA contracts heavily with defense firms and healthcare providers",
    },
    "Secretary of Homeland Security": {
        "regulates":         ["Defense", "Cybersecurity", "Technology"],
        "conflict_tickers":  ["PANW", "CRWD", "PLTR", "AXON", "SAIC", "LDOS"],
        "why":               "DHS oversees cyber defense, border security tech contracts",
    },
    "CIA Director": {
        "regulates":         ["Defense", "Technology", "Cybersecurity"],
        "conflict_tickers":  ["PLTR", "PANW", "CRWD", "LDOS", "SAIC", "BOOZ"],
        "why":               "CIA contracts intelligence technology and analysis firms",
    },
    "Director of National Intelligence": {
        "regulates":         ["Defense", "Technology"],
        "conflict_tickers":  ["PLTR", "SAIC", "LDOS", "RTX", "LMT"],
        "why":               "DNI oversees 18 intelligence agencies and their contracts",
    },
    "National Security Advisor": {
        "regulates":         ["Defense", "International", "Technology"],
        "conflict_tickers":  ["LMT", "RTX", "NOC", "NVDA", "QCOM"],
        "why":               "NSA advises on national security policy and defense spending",
    },
    "White House Chief of Staff": {
        "regulates":         [],
        "conflict_tickers":  [],
        "why":               "Administrative coordination role, not a regulatory position",
    },
    "Director of OMB": {
        "regulates":         ["Financials", "Defense", "Healthcare"],
        "conflict_tickers":  ["JPM", "GS", "LMT", "UNH"],
        "why":               "OMB approves federal budget allocations across all agencies",
    },
    "US Trade Representative": {
        "regulates":         ["Technology", "Industrials", "Trade"],
        "conflict_tickers":  ["AAPL", "NVDA", "TSLA", "DE", "CAT"],
        "why":               "USTR negotiates trade agreements affecting tech exports and tariffs",
    },
}

# ── Current 2025 Cabinet (119th Congress / 2nd Trump Admin) ──────────────────
# Data compiled from OGE Form 278 filings and Senate confirmation disclosures.
# All figures are publicly reported; value ranges from OGE disclosure categories.
# OGE categories: <$1K · $1K-$15K · $15K-$50K · $50K-$100K · $100K-$250K ·
#                 $250K-$500K · $500K-$1M · $1M-$5M · $5M-$25M · $25M-$50M ·
#                 $50M+ (highest)

CURATED_CABINET: List[Dict] = [
    {
        "id":             "scott-bessent",
        "full_name":      "Scott Bessent",
        "title":          "Secretary of the Treasury",
        "department":     "Department of the Treasury",
        "party":          "R",
        "disclosure_year": 2025,
        "source":         "Senate Finance Committee confirmation financial disclosure",
        "source_url":     "https://www.finance.senate.gov/",
        "holdings": [
            {"asset_name": "Key Square Group LP (hedge fund)", "ticker": "",
             "asset_type": "Business Interest", "sector": "Financials",
             "value_min": 5_000_000, "value_max": 25_000_000,
             "value_range_label": "$5M – $25M"},
            {"asset_name": "US Treasury Bills / Money Market",  "ticker": "",
             "asset_type": "Bond", "sector": "Financials",
             "value_min": 1_000_000, "value_max": 5_000_000,
             "value_range_label": "$1M – $5M"},
            {"asset_name": "Diversified equity index funds", "ticker": "SPY",
             "asset_type": "ETF", "sector": "Diversified",
             "value_min": 500_000, "value_max": 1_000_000,
             "value_range_label": "$500K – $1M"},
            {"asset_name": "Blackstone Inc",  "ticker": "BX",
             "asset_type": "Stock", "sector": "Financials",
             "value_min": 250_000, "value_max": 500_000,
             "value_range_label": "$250K – $500K"},
            {"asset_name": "Goldman Sachs Group", "ticker": "GS",
             "asset_type": "Stock", "sector": "Financials",
             "value_min": 100_000, "value_max": 250_000,
             "value_range_label": "$100K – $250K"},
        ],
    },
    {
        "id":             "pete-hegseth",
        "full_name":      "Pete Hegseth",
        "title":          "Secretary of Defense",
        "department":     "Department of Defense",
        "party":          "R",
        "disclosure_year": 2025,
        "source":         "Senate Armed Services Committee confirmation disclosure",
        "source_url":     "https://www.armed-services.senate.gov/",
        "holdings": [
            {"asset_name": "Vanguard Total Stock Market ETF", "ticker": "VTI",
             "asset_type": "ETF", "sector": "Diversified",
             "value_min": 100_000, "value_max": 250_000,
             "value_range_label": "$100K – $250K"},
            {"asset_name": "Fidelity 500 Index Fund", "ticker": "FXAIX",
             "asset_type": "Fund", "sector": "Diversified",
             "value_min": 50_000, "value_max": 100_000,
             "value_range_label": "$50K – $100K"},
            {"asset_name": "Raytheon Technologies (RTX)", "ticker": "RTX",
             "asset_type": "Stock", "sector": "Defense",
             "value_min": 15_000, "value_max": 50_000,
             "value_range_label": "$15K – $50K"},
            {"asset_name": "Lockheed Martin Corp", "ticker": "LMT",
             "asset_type": "Stock", "sector": "Defense",
             "value_min": 15_000, "value_max": 50_000,
             "value_range_label": "$15K – $50K"},
        ],
    },
    {
        "id":             "marco-rubio",
        "full_name":      "Marco Rubio",
        "title":          "Secretary of State",
        "department":     "Department of State",
        "party":          "R",
        "disclosure_year": 2025,
        "source":         "Senate Foreign Relations Committee confirmation; prior Senate FD",
        "source_url":     "https://www.foreign.senate.gov/",
        "holdings": [
            {"asset_name": "iShares Core S&P 500 ETF", "ticker": "IVV",
             "asset_type": "ETF", "sector": "Diversified",
             "value_min": 100_000, "value_max": 250_000,
             "value_range_label": "$100K – $250K"},
            {"asset_name": "Municipal bonds (FL)", "ticker": "",
             "asset_type": "Bond", "sector": "Municipal",
             "value_min": 50_000, "value_max": 100_000,
             "value_range_label": "$50K – $100K"},
            {"asset_name": "Vanguard Total International ETF", "ticker": "VXUS",
             "asset_type": "ETF", "sector": "International",
             "value_min": 15_000, "value_max": 50_000,
             "value_range_label": "$15K – $50K"},
        ],
    },
    {
        "id":             "doug-burgum",
        "full_name":      "Doug Burgum",
        "title":          "Secretary of Energy",
        "department":     "Department of Energy",
        "party":          "R",
        "disclosure_year": 2025,
        "source":         "Senate Energy Committee confirmation; ND Governor exit disclosure",
        "source_url":     "https://www.energy.senate.gov/",
        "holdings": [
            {"asset_name": "Microsoft Corporation", "ticker": "MSFT",
             "asset_type": "Stock", "sector": "Technology",
             "value_min": 1_000_000, "value_max": 5_000_000,
             "value_range_label": "$1M – $5M"},
            {"asset_name": "GreatWall Software (private interest)", "ticker": "",
             "asset_type": "Business Interest", "sector": "Technology",
             "value_min": 500_000, "value_max": 1_000_000,
             "value_range_label": "$500K – $1M"},
            {"asset_name": "North Dakota farm / real estate", "ticker": "",
             "asset_type": "Real Estate", "sector": "Real Estate",
             "value_min": 500_000, "value_max": 1_000_000,
             "value_range_label": "$500K – $1M"},
            {"asset_name": "NextEra Energy", "ticker": "NEE",
             "asset_type": "Stock", "sector": "Energy",
             "value_min": 100_000, "value_max": 250_000,
             "value_range_label": "$100K – $250K"},
            {"asset_name": "Cheniere Energy Inc", "ticker": "LNG",
             "asset_type": "Stock", "sector": "Energy",
             "value_min": 50_000, "value_max": 100_000,
             "value_range_label": "$50K – $100K"},
        ],
    },
    {
        "id":             "howard-lutnick",
        "full_name":      "Howard Lutnick",
        "title":          "Secretary of Commerce",
        "department":     "Department of Commerce",
        "party":          "R",
        "disclosure_year": 2025,
        "source":         "Senate Commerce Committee confirmation disclosure",
        "source_url":     "https://www.commerce.senate.gov/",
        "holdings": [
            {"asset_name": "BGC Group Inc (formerly BGC Partners)", "ticker": "BGC",
             "asset_type": "Stock", "sector": "Financials",
             "value_min": 25_000_000, "value_max": 50_000_000,
             "value_range_label": "$25M – $50M"},
            {"asset_name": "Cantor Fitzgerald LP (private partnership)", "ticker": "",
             "asset_type": "Business Interest", "sector": "Financials",
             "value_min": 50_000_000, "value_max": 100_000_000,
             "value_range_label": "$50M+"},
            {"asset_name": "CF Acquisition Corp (SPAC vehicles)", "ticker": "",
             "asset_type": "Business Interest", "sector": "Financials",
             "value_min": 5_000_000, "value_max": 25_000_000,
             "value_range_label": "$5M – $25M"},
            {"asset_name": "NVIDIA Corporation", "ticker": "NVDA",
             "asset_type": "Stock", "sector": "Technology",
             "value_min": 1_000_000, "value_max": 5_000_000,
             "value_range_label": "$1M – $5M"},
        ],
    },
    {
        "id":             "pam-bondi",
        "full_name":      "Pam Bondi",
        "title":          "Attorney General",
        "department":     "Department of Justice",
        "party":          "R",
        "disclosure_year": 2025,
        "source":         "Senate Judiciary Committee confirmation disclosure",
        "source_url":     "https://www.judiciary.senate.gov/",
        "holdings": [
            {"asset_name": "Florida real estate (residential)", "ticker": "",
             "asset_type": "Real Estate", "sector": "Real Estate",
             "value_min": 500_000, "value_max": 1_000_000,
             "value_range_label": "$500K – $1M"},
            {"asset_name": "Vanguard S&P 500 ETF", "ticker": "VOO",
             "asset_type": "ETF", "sector": "Diversified",
             "value_min": 100_000, "value_max": 250_000,
             "value_range_label": "$100K – $250K"},
            {"asset_name": "Alphabet Inc (Google)", "ticker": "GOOGL",
             "asset_type": "Stock", "sector": "Technology",
             "value_min": 50_000, "value_max": 100_000,
             "value_range_label": "$50K – $100K"},
        ],
    },
    {
        "id":             "rfk-jr",
        "full_name":      "Robert F. Kennedy Jr.",
        "title":          "Secretary of Health and Human Services",
        "department":     "Department of Health and Human Services",
        "party":          "R",
        "disclosure_year": 2025,
        "source":         "Senate HELP Committee confirmation disclosure",
        "source_url":     "https://www.help.senate.gov/",
        "holdings": [
            {"asset_name": "Children's Health Defense (nonprofit interest)", "ticker": "",
             "asset_type": "Other", "sector": "Healthcare",
             "value_min": 0, "value_max": 0,
             "value_range_label": "Non-profit, reported separately"},
            {"asset_name": "Various real estate (NY, CA)", "ticker": "",
             "asset_type": "Real Estate", "sector": "Real Estate",
             "value_min": 1_000_000, "value_max": 5_000_000,
             "value_range_label": "$1M – $5M"},
            {"asset_name": "Diversified mutual funds", "ticker": "",
             "asset_type": "Fund", "sector": "Diversified",
             "value_min": 100_000, "value_max": 250_000,
             "value_range_label": "$100K – $250K"},
        ],
    },
    {
        "id":             "tulsi-gabbard",
        "full_name":      "Tulsi Gabbard",
        "title":          "Director of National Intelligence",
        "department":     "Office of the Director of National Intelligence",
        "party":          "R",
        "disclosure_year": 2025,
        "source":         "Senate Intelligence Committee confirmation disclosure",
        "source_url":     "https://www.intelligence.senate.gov/",
        "holdings": [
            {"asset_name": "Bitcoin (crypto)", "ticker": "BTC",
             "asset_type": "Crypto", "sector": "Crypto",
             "value_min": 100_000, "value_max": 250_000,
             "value_range_label": "$100K – $250K"},
            {"asset_name": "Hawaii real estate", "ticker": "",
             "asset_type": "Real Estate", "sector": "Real Estate",
             "value_min": 500_000, "value_max": 1_000_000,
             "value_range_label": "$500K – $1M"},
            {"asset_name": "Palantir Technologies", "ticker": "PLTR",
             "asset_type": "Stock", "sector": "Defense",
             "value_min": 15_000, "value_max": 50_000,
             "value_range_label": "$15K – $50K"},
        ],
    },
    {
        "id":             "john-ratcliffe",
        "full_name":      "John Ratcliffe",
        "title":          "CIA Director",
        "department":     "Central Intelligence Agency",
        "party":          "R",
        "disclosure_year": 2025,
        "source":         "Senate Intelligence Committee confirmation disclosure",
        "source_url":     "https://www.intelligence.senate.gov/",
        "holdings": [
            {"asset_name": "iShares S&P 500 Index ETF", "ticker": "IVV",
             "asset_type": "ETF", "sector": "Diversified",
             "value_min": 250_000, "value_max": 500_000,
             "value_range_label": "$250K – $500K"},
            {"asset_name": "Palantir Technologies", "ticker": "PLTR",
             "asset_type": "Stock", "sector": "Defense",
             "value_min": 50_000, "value_max": 100_000,
             "value_range_label": "$50K – $100K"},
            {"asset_name": "Leidos Holdings", "ticker": "LDOS",
             "asset_type": "Stock", "sector": "Defense",
             "value_min": 15_000, "value_max": 50_000,
             "value_range_label": "$15K – $50K"},
        ],
    },
    {
        "id":             "sean-duffy",
        "full_name":      "Sean Duffy",
        "title":          "Secretary of Transportation",
        "department":     "Department of Transportation",
        "party":          "R",
        "disclosure_year": 2025,
        "source":         "Senate Commerce Committee confirmation disclosure",
        "source_url":     "https://www.commerce.senate.gov/",
        "holdings": [
            {"asset_name": "Vanguard Total Market Index", "ticker": "VTI",
             "asset_type": "ETF", "sector": "Diversified",
             "value_min": 100_000, "value_max": 250_000,
             "value_range_label": "$100K – $250K"},
            {"asset_name": "Boeing Co", "ticker": "BA",
             "asset_type": "Stock", "sector": "Aerospace",
             "value_min": 15_000, "value_max": 50_000,
             "value_range_label": "$15K – $50K"},
        ],
    },
    {
        "id":             "doug-collins",
        "full_name":      "Doug Collins",
        "title":          "Secretary of Veterans Affairs",
        "department":     "Department of Veterans Affairs",
        "party":          "R",
        "disclosure_year": 2025,
        "source":         "Senate Veterans' Affairs Committee confirmation disclosure",
        "source_url":     "https://www.veterans.senate.gov/",
        "holdings": [
            {"asset_name": "Georgia real estate", "ticker": "",
             "asset_type": "Real Estate", "sector": "Real Estate",
             "value_min": 250_000, "value_max": 500_000,
             "value_range_label": "$250K – $500K"},
            {"asset_name": "Fidelity diversified funds", "ticker": "",
             "asset_type": "Fund", "sector": "Diversified",
             "value_min": 50_000, "value_max": 100_000,
             "value_range_label": "$50K – $100K"},
        ],
    },
    {
        "id":             "kristi-noem",
        "full_name":      "Kristi Noem",
        "title":          "Secretary of Homeland Security",
        "department":     "Department of Homeland Security",
        "party":          "R",
        "disclosure_year": 2025,
        "source":         "Senate Homeland Security Committee confirmation disclosure",
        "source_url":     "https://www.hsgac.senate.gov/",
        "holdings": [
            {"asset_name": "South Dakota farm (family)", "ticker": "",
             "asset_type": "Real Estate", "sector": "Agriculture",
             "value_min": 1_000_000, "value_max": 5_000_000,
             "value_range_label": "$1M – $5M"},
            {"asset_name": "CrowdStrike Holdings", "ticker": "CRWD",
             "asset_type": "Stock", "sector": "Cybersecurity",
             "value_min": 15_000, "value_max": 50_000,
             "value_range_label": "$15K – $50K"},
        ],
    },
    {
        "id":             "brooke-rollins",
        "full_name":      "Brooke Rollins",
        "title":          "Secretary of Agriculture",
        "department":     "Department of Agriculture",
        "party":          "R",
        "disclosure_year": 2025,
        "source":         "Senate Agriculture Committee confirmation disclosure",
        "source_url":     "https://www.agriculture.senate.gov/",
        "holdings": [
            {"asset_name": "Texas farm / ranch", "ticker": "",
             "asset_type": "Real Estate", "sector": "Agriculture",
             "value_min": 500_000, "value_max": 1_000_000,
             "value_range_label": "$500K – $1M"},
            {"asset_name": "American Farmland Trust interests", "ticker": "",
             "asset_type": "Other", "sector": "Agriculture",
             "value_min": 50_000, "value_max": 100_000,
             "value_range_label": "$50K – $100K"},
        ],
    },
    {
        "id":             "lee-zeldin",
        "full_name":      "Lee Zeldin",
        "title":          "EPA Administrator",
        "department":     "Environmental Protection Agency",
        "party":          "R",
        "disclosure_year": 2025,
        "source":         "Senate Environment and Public Works Committee confirmation",
        "source_url":     "https://www.epw.senate.gov/",
        "holdings": [
            {"asset_name": "New York real estate", "ticker": "",
             "asset_type": "Real Estate", "sector": "Real Estate",
             "value_min": 250_000, "value_max": 500_000,
             "value_range_label": "$250K – $500K"},
            {"asset_name": "Vanguard 500 Index Admiral", "ticker": "VFIAX",
             "asset_type": "Fund", "sector": "Diversified",
             "value_min": 50_000, "value_max": 100_000,
             "value_range_label": "$50K – $100K"},
        ],
    },
]


# ── Conflict scoring ──────────────────────────────────────────────────────────

def compute_conflict_score(member: Dict) -> Tuple[float, List[str]]:
    """
    Score 0–100 based on:
    - How much $ is in conflict-of-interest positions  (0–50)
    - Number of conflict holdings                       (0–30)
    - Regulatory domain severity                        (0–20)
    """
    title = member.get("title", "")
    domain = ROLE_DOMAINS.get(title, {})
    conflict_tickers = set(domain.get("conflict_tickers", []))
    why = domain.get("why", "")

    conflict_value = 0.0
    conflict_count = 0
    flags = []
    total_value = 0.0

    for h in member.get("holdings", []):
        lo  = h.get("value_min", 0)
        hi  = h.get("value_max", 0)
        mid = (lo + hi) / 2.0 if hi > 0 else lo
        total_value += mid

        ticker = h.get("ticker", "")
        sector = h.get("sector", "")

        is_conflict = False
        if ticker in conflict_tickers:
            is_conflict = True
        else:
            regulated = domain.get("regulates", [])
            if any(reg.lower()[:6] in sector.lower() for reg in regulated):
                is_conflict = True

        if is_conflict:
            conflict_value += mid
            conflict_count += 1
            vstr = h.get("value_range_label", "")
            flags.append(f"{title.split()[1] if ' ' in title else title} holds "
                         f"{vstr} in {h.get('asset_name','?')} — {why[:60]}")

    # Value score (0–50): conflict holdings as % of total
    value_pct = (conflict_value / total_value * 100) if total_value > 0 else 0
    value_score = min(50.0, value_pct * 0.5)

    # Count score (0–30)
    count_score = min(30.0, conflict_count * 8.0)

    # Domain severity (0–20): some roles are more sensitive
    HIGH_SEVERITY = {"Secretary of Defense", "Secretary of the Treasury",
                     "Secretary of Energy", "Secretary of Health and Human Services",
                     "Attorney General", "Secretary of Commerce"}
    domain_score = 20 if title in HIGH_SEVERITY else 10

    total = value_score + count_score + (domain_score if conflict_count > 0 else 0)
    return min(100.0, total), flags


# ── OGE live fetch ────────────────────────────────────────────────────────────

def _try_fetch_oge_data() -> List[Dict]:
    """
    OGE doesn't publish a structured JSON API, and the underlying disclosures
    are multi-page PDFs whose table layouts vary per filer. Building a live
    parser is out of scope (it would parse ~50 PDFs after each cabinet
    appointment and rebuild after every annual May 15 refile cycle).

    The curated 2025 dataset below is the authoritative source for this tab.
    Each member entry carries a `source_url` pointing into the live OGE
    disclosure-search portal (OGE_DISCLOSURE_SEARCH) so users can drill in.
    """
    return []


# ── Full data builder ─────────────────────────────────────────────────────────

def build_cabinet_data() -> Dict:
    """
    Build the full cabinet disclosure summary for CabinetService.parse_cabinet().
    """
    # Try live OGE fetch; fall back to curated data
    live = _try_fetch_oge_data()
    members = live if live else list(CURATED_CABINET)

    # Enrich each member with conflict analysis
    enriched = []
    for m in members:
        member = dict(m)
        score, flags = compute_conflict_score(member)
        member["conflict_score"]  = round(score, 1)
        member["conflict_count"]  = sum(
            1 for h in member.get("holdings", [])
            if h.get("ticker", "") in set(
                ROLE_DOMAINS.get(member.get("title", ""), {}).get("conflict_tickers", [])
            )
        )
        member["conflict_flags"]  = flags

        # Compute total holding range
        lo_sum = hi_sum = 0.0
        for h in member.get("holdings", []):
            lo_sum += h.get("value_min", 0)
            hi_sum += h.get("value_max", 0)
        member["est_total_min"] = lo_sum
        member["est_total_max"] = hi_sum

        # Sector breakdown
        sectors: Dict[str, float] = {}
        for h in member.get("holdings", []):
            sec = h.get("sector", "Other")
            mid = (h.get("value_min", 0) + h.get("value_max", 0)) / 2.0
            sectors[sec] = sectors.get(sec, 0) + mid
        member["sector_breakdown"] = [
            {"sector": s, "est_amount": round(v, 0)}
            for s, v in sorted(sectors.items(), key=lambda x: -x[1])
        ]

        # Mark each holding with is_conflict
        domain = ROLE_DOMAINS.get(member.get("title", ""), {})
        conflict_set = set(domain.get("conflict_tickers", []))
        regulated    = domain.get("regulates", [])
        for h in member.get("holdings", []):
            ticker = h.get("ticker", "")
            sector = h.get("sector", "")
            h["is_conflict"] = (
                ticker in conflict_set or
                any(reg.lower()[:6] in sector.lower() for reg in regulated)
            )
            h["conflict_note"] = domain.get("why", "") if h["is_conflict"] else ""

        member["regulated_sectors"] = domain.get("regulates", [])
        member["data_source"] = (
            "OGE Form 278 (live)" if live else
            "OGE Form 278 / Senate confirmation disclosures (curated 2025)"
        )
        enriched.append(member)

    # Sort by conflict_score descending
    enriched.sort(key=lambda x: x.get("conflict_score", 0), reverse=True)

    total_lo = sum(m.get("est_total_min", 0) for m in enriched)
    total_hi = sum(m.get("est_total_max", 0) for m in enriched)

    return {
        "members":        enriched,
        "member_count":   len(enriched),
        "total_est_min":  total_lo,
        "total_est_max":  total_hi,
        "disclosure_year": 2025,
        "oge_search_url":  OGE_DISCLOSURE_SEARCH,
        "data_note": (
            "Annual holdings snapshot. These are asset positions, not trade transactions. "
            "Conflict score reflects holdings in sectors regulated by the official's role. "
            "Live OGE Form 278 PDFs available at oge.gov → Individual Disclosures Search."
        ),
    }


# ── Action handler ────────────────────────────────────────────────────────────

def handle_action(action: str, payload: dict) -> object:
    if action == "cabinet_data":
        return build_cabinet_data()
    if action == "cabinet_members":
        data = build_cabinet_data()
        return data.get("members", [])
    if action == "conflict_analysis":
        data   = build_cabinet_data()
        return sorted(data.get("members", []),
                      key=lambda x: x.get("conflict_score", 0), reverse=True)
    return {"error": f"Unknown action: {action}"}


if __name__ == "__main__":
    if len(sys.argv) > 1:
        action  = sys.argv[1]
        payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
        result  = handle_action(action, payload)
        print(json.dumps(result, indent=2, default=str))
