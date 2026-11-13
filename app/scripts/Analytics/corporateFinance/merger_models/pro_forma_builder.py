"""Pro Forma Financial Statement Builder"""
from typing import Dict, Any, List, Optional
from dataclasses import dataclass
import numpy as np

@dataclass
class CompanyFinancials:
    """Company financial data structure"""
    revenue: float
    cogs: float
    gross_profit: float
    sg_a: float
    r_d: float
    depreciation: float
    ebitda: float
    ebit: float
    interest_expense: float
    ebt: float
    taxes: float
    net_income: float
    shares_outstanding: float
    eps: float

    total_assets: float = 0
    total_liabilities: float = 0
    shareholders_equity: float = 0
    cash: float = 0
    debt: float = 0
