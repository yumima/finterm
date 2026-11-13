"""LBO Module - Leveraged Buyout Models"""
import sys
from pathlib import Path

# Add Analytics path for absolute imports
analytics_path = Path(__file__).parent.parent.parent
sys.path.insert(0, str(analytics_path))

# Use absolute imports instead of relative imports
from corporateFinance.lbo.debt_schedule import lbo_waterfall, tranches_from_params
from corporateFinance.lbo.returns_calculator import ReturnsCalculator

__all__ = ['lbo_waterfall', 'tranches_from_params', 'ReturnsCalculator']
