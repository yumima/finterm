"""Returns Calculator - IRR and MOIC"""
from typing import List, Dict, Any, Optional
import numpy as np
try:
    import numpy_financial as npf
except ImportError:
    npf = None

class ReturnsCalculator:
    """Calculate LBO returns metrics"""

    @staticmethod
    def _irr_bisect(cash_flows: List[float]) -> Optional[float]:
        """IRR by bisection on NPV (numpy_financial is not bundled and
        np.irr was removed in numpy 1.20). Returns None when no sign change
        brackets a root."""
        def npv(r: float) -> float:
            return sum(cf / (1.0 + r) ** t for t, cf in enumerate(cash_flows))

        lo, hi = -0.9999, 1.0
        f_lo = npv(lo)
        f_hi = npv(hi)
        while f_lo * f_hi > 0 and hi < 1e4:
            hi *= 4.0
            f_hi = npv(hi)
        if f_lo * f_hi > 0:
            return None
        for _ in range(200):
            mid = (lo + hi) / 2.0
            f_mid = npv(mid)
            if abs(f_mid) < 1e-10 or (hi - lo) < 1e-12:
                return mid
            if f_lo * f_mid < 0:
                hi, f_hi = mid, f_mid
            else:
                lo, f_lo = mid, f_mid
        return (lo + hi) / 2.0

    @staticmethod
    def calculate_irr(cash_flows: List[float]) -> Optional[float]:
        """Calculate Internal Rate of Return.

        Returns None when the IRR is undefined (never a fabricated 0%). A
        total loss (outflows only, all later flows zero) is -100%.
        """
        if not cash_flows or not any(cf < 0 for cf in cash_flows):
            return None
        if not any(cf > 0 for cf in cash_flows):
            return -1.0
        try:
            if npf is not None:
                irr = npf.irr(cash_flows)
                if irr is not None and not np.isnan(irr):
                    return float(irr)
            return ReturnsCalculator._irr_bisect(cash_flows)
        except Exception:
            return None

    @staticmethod
    def calculate_moic(initial_investment: float, exit_proceeds: float) -> float:
        """Calculate Multiple on Invested Capital"""
        if initial_investment <= 0:
            return 0
        return exit_proceeds / initial_investment

    @staticmethod
    def calculate_cash_on_cash(initial_equity: float, exit_equity_value: float,
                              interim_distributions: float = 0) -> float:
        """Calculate cash-on-cash return"""
        if initial_equity <= 0:
            return 0

        total_cash_received = exit_equity_value + interim_distributions
        return total_cash_received / initial_equity

    def comprehensive_returns(self, initial_equity: float, exit_equity_value: float,
                            holding_period_years: int,
                            interim_distributions: List[float] = None) -> Dict[str, Any]:
        """Calculate comprehensive return metrics"""

        if interim_distributions is None:
            interim_distributions = []

        cash_flows = [-initial_equity] + interim_distributions + [exit_equity_value]

        irr = self.calculate_irr(cash_flows)
        moic = self.calculate_moic(initial_equity, exit_equity_value)
        coc = self.calculate_cash_on_cash(initial_equity, exit_equity_value, sum(interim_distributions))

        annualized_return = ((moic ** (1 / holding_period_years)) - 1) if holding_period_years > 0 else 0

        return {
            'initial_equity_investment': initial_equity,
            'exit_equity_value': exit_equity_value,
            'holding_period_years': holding_period_years,
            'irr': irr * 100 if irr is not None else None,
            'moic': moic,
            'cash_on_cash_multiple': coc,
            'annualized_return': annualized_return * 100,
            'absolute_gain': exit_equity_value - initial_equity,
            'absolute_gain_pct': ((exit_equity_value - initial_equity) / initial_equity * 100) if initial_equity else 0
        }

    def returns_by_exit_year(self, initial_equity: float, exit_values: Dict[int, float]) -> Dict[int, Dict[str, float]]:
        """Calculate returns for multiple exit scenarios"""

        returns_by_year = {}

        for year, exit_value in exit_values.items():
            returns = self.comprehensive_returns(initial_equity, exit_value, year)
            returns_by_year[year] = returns

        return returns_by_year

    def calculate_hurdle_metrics(self, returns: Dict[str, Any],
                                hurdle_irr: float = 0.20) -> Dict[str, Any]:
        """Calculate excess returns vs hurdle rate"""

        actual_irr = returns['irr'] / 100 if returns['irr'] is not None else None
        excess_irr = actual_irr - hurdle_irr if actual_irr is not None else None

        return {
            'hurdle_irr': hurdle_irr * 100,
            'actual_irr': returns['irr'],
            'excess_irr': excess_irr * 100 if excess_irr is not None else None,
            'meets_hurdle': actual_irr >= hurdle_irr if actual_irr is not None else None,
            'hurdle_moic': (1 + hurdle_irr) ** returns['holding_period_years'],
            'actual_moic': returns['moic'],
            'excess_moic': returns['moic'] - ((1 + hurdle_irr) ** returns['holding_period_years'])
        }

def main():
    """CLI entry point - outputs JSON for C++ integration"""
    import sys
    import json

    if len(sys.argv) < 2:
        result = {"success": False, "error": "No command specified"}
        print(json.dumps(result))
        sys.exit(1)

    command = sys.argv[1]

    try:
        if command == "returns":
            if len(sys.argv) < 6:
                raise ValueError("Entry valuation, exit valuation, equity invested, and holding period required")

            entry_valuation = float(sys.argv[2])  # Not directly used, but enterprise value at entry
            exit_valuation = float(sys.argv[3])    # Not directly used, but enterprise value at exit
            equity_invested = float(sys.argv[4])   # Initial equity investment
            holding_period = int(sys.argv[5])

            # Calculate exit equity value from exit enterprise valuation
            # In an LBO, equity value at exit = exit EV - remaining debt
            # For simplification, we'll use exit_valuation directly as exit equity
            # In real scenario, you'd subtract remaining debt

            calc = ReturnsCalculator()
            analysis = calc.comprehensive_returns(equity_invested, exit_valuation, holding_period)

            result = {"success": True, "data": analysis}
            print(json.dumps(result))

        else:
            result = {"success": False, "error": f"Unknown command: {command}"}
            print(json.dumps(result))
            sys.exit(1)

    except Exception as e:
        result = {"success": False, "error": str(e)}
        print(json.dumps(result))
        sys.exit(1)

if __name__ == '__main__':
    main()
