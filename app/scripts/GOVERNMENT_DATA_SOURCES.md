# Government Open Data Sources

> Official government data portals for economic statistics, company data, and public datasets

## Overview

finterm integrates with official government data portals worldwide providing authoritative, free, and regularly updated data including economic indicators, trade statistics, company registrations, and geographic data.

**Universal CKAN Wrapper**: Use `universal_ckan_api.py` for unified access to 8+ CKAN portals with a single interface. See `universal_ckan_api.md` for documentation.

## Individual Country Scripts

| Country/Region | File Name | API Key | CKAN | Portal |
|----------------|-----------|---------|------|--------|
| 🇺🇸 **US Treasury** | `government_us_data.py` | ❌ No | ❌ | [TreasuryDirect.gov](https://www.treasurydirect.gov) |
| 🇺🇸 **US Congress** | `congress_gov_data.py` | ❌ No | ❌ | [Congress.gov](https://www.congress.gov) |
| 🇺🇸 **Federal Reserve** | `federal_reserve_data.py` | ❌ No | ❌ | [FederalReserve.gov](https://www.federalreserve.gov) |
| 🇨🇦 **Canada** | `canada_gov_api.py` | 🔑 Optional | ✅ | [open.canada.ca](https://open.canada.ca) |
| 🇬🇧 **United Kingdom** | `datagovuk_api.py` | 🔑 Optional | ✅ | [data.gov.uk](https://data.gov.uk) |
| 🇫🇷 **France** | `french_gov_api.py` | 🔑 Partial* | ❌ | [data.gouv.fr](https://data.gouv.fr) |
| 🇨🇭 **Switzerland** | `swiss_gov_api.py` | 🔑 Optional | ✅ | [opendata.swiss](https://opendata.swiss) |
| 🇩🇪 **Germany** | `govdata_de_api_complete.py` | 🔑 Optional | ✅ | [govdata.de](https://www.govdata.de) |
| 🇸🇬 **Singapore** | `datagovsg_data.py` | 🔑 Optional | ❌ | [data.gov.sg](https://data.gov.sg) |
| 🇭🇰 **Hong Kong** | `data_gov_hk_api.py` | ❌ No | ❌ | [data.gov.hk](https://data.gov.hk) |
| 🇦🇺 **Australia** | `datagov_au_api.py` | 🔑 Optional | ✅ | [data.gov.au](https://data.gov.au) |

*France: API Entreprise (company data) requires authentication; other endpoints are open.

## Universal CKAN Portal Access

Single interface via `universal_ckan_api.py` - no API keys needed:

| Country | Code | Portal URL |
|---------|------|------------|
| 🇺🇸 United States | `us` | [catalog.data.gov](https://catalog.data.gov) |
| 🇬🇧 United Kingdom | `uk` | [data.gov.uk](https://data.gov.uk) |
| 🇦🇺 Australia | `au` | [data.gov.au](https://data.gov.au) |
| 🇮🇹 Italy | `it` | [dati.gov.it](https://www.dati.gov.it) |
| 🇧🇷 Brazil | `br` | [dados.gov.br](https://dados.gov.br) |
| 🇱🇻 Latvia | `lv` | [data.gov.lv](https://data.gov.lv) |
| 🇸🇮 Slovenia | `si` | [podatki.gov.si](https://podatki.gov.si) |
| 🇺🇾 Uruguay | `uy` | [catalogodatos.gub.uy](https://catalogodatos.gub.uy) |

## Data Categories

| Category | Examples |
|----------|----------|
| **Economic & Financial** | GDP, inflation, employment, trade balance, treasury operations, currency rates |
| **Geographic** | Administrative boundaries, population data, postal codes, coordinates |
| **Company & Business** | Registrations, filings, business statistics, corporate governance |
| **Legislative & Policy** | Bills, voting records (US Congress), regulations, parliamentary data |

## Usage

```python
# Individual portal
from canada_gov_api import search_datasets
results = search_datasets(query="GDP", organization="statistics-canada", limit=10)

# Universal CKAN
from universal_ckan_api import search_datasets
results = search_datasets('us', query='climate', rows=10)
```

```bash
# CLI
python universal_ckan_api.py --country us --action search_datasets --query "climate"
```

---

**Total Sources**: 19 countries/portals | **CKAN Portals**: 8 via universal wrapper | **Last Updated**: 2025-12-28
