"""Configuration for Corporate Finance Module"""
import os
from pathlib import Path
from finterm_paths import feature_dir

BASE_DIR = Path(__file__).parent
SCRIPTS_DIR = BASE_DIR.parent.parent
RESOURCES_DIR = SCRIPTS_DIR.parent

# The M&A deal database is the user's own data: user/features/ma under the
# app's data folder (finterm_paths), the same place whether the app launched
# the script or it runs by hand.
def get_database_dir() -> Path:
    return feature_dir("ma")

DATABASE_DIR = get_database_dir()
# FINCEPT_MA_DB_PATH overrides the deal-database file itself (tests point it
# at a throwaway file so the user's real database is never opened).
MA_DATABASE = (Path(os.environ["FINCEPT_MA_DB_PATH"]) if os.environ.get("FINCEPT_MA_DB_PATH")
               else DATABASE_DIR / "ma_deals.db")

# Ensure database directory exists
DATABASE_DIR.mkdir(parents=True, exist_ok=True)

DEFAULT_DISCOUNT_RATE = 0.10
DEFAULT_TERMINAL_GROWTH = 0.025
DEFAULT_TAX_RATE = 0.21
DEFAULT_RISK_FREE_RATE = 0.045

MA_FILING_FORMS = ['8-K', 'S-4', 'DEFM14A', 'SC 13D', 'SC 13G', '425', 'PREM14A']
MATERIAL_EVENT_ITEMS = ['1.01', '2.01', '8.01']

INDUSTRIES = [
    'Technology', 'Healthcare', 'Financial Services', 'Industrials',
    'Consumer Discretionary', 'Consumer Staples', 'Energy', 'Materials',
    'Real Estate', 'Utilities', 'Telecommunications', 'Media'
]

DEAL_TYPES = ['Merger', 'Acquisition', 'Takeover', 'Joint Venture', 'Asset Purchase', 'Reverse Merger']
PAYMENT_METHODS = ['Cash', 'Stock', 'Mixed', 'Debt', 'Earnout']
DEAL_STATUS = ['Announced', 'Pending', 'Completed', 'Terminated', 'Withdrawn']
