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

# ── JSON contract (MAAnalyticsService "calculate" / ma_lbo_returns) ──────────

def _json_calculate(p: Dict[str, Any]) -> Dict[str, Any]:
    """Sponsor returns from entry equity and exit equity.

    exit equity = exit_equity_value, or exit_valuation (exit EV) - exit_net_debt.
    Optional interim_distributions: list of annual distributions for years
    1..N-1 (dividend recaps); absent = none.
    """
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import num, opt_num, num_list, has, InputError
    equity = num(p, 'equity_invested', gt=0)
    n = num(p, 'holding_period', min=1, max=50, integer=True)
    if has(p, 'exit_equity_value'):
        exit_equity = num(p, 'exit_equity_value')
        exit_ev = None
        exit_net_debt = None
    else:
        exit_ev = num(p, 'exit_valuation', label='exit_valuation (exit enterprise value)', min=0)
        exit_net_debt = num(p, 'exit_net_debt', label='exit_net_debt (debt - cash at exit)')
        exit_equity = exit_ev - exit_net_debt
    interim = num_list(p, 'interim_distributions', min_len=0) if has(p, 'interim_distributions') else []
    if len(interim) > n - 1:
        raise InputError("interim_distributions can cover at most years 1..N-1")
    interim = interim + [0.0] * (n - 1 - len(interim))
    # Equity is worth at most zero to the sponsor at exit if debt exceeds EV.
    proceeds = max(0.0, exit_equity)
    flows = [-equity] + interim + [proceeds]
    irr = ReturnsCalculator.calculate_irr(flows)
    total_back = proceeds + sum(interim)
    out = {
        'irr_pct': irr * 100 if irr is not None else None,
        'moic_x': total_back / equity,
        'equity_invested': equity,
        'exit_equity_value': proceeds,
        'interim_distributions_total': sum(interim),
        'absolute_gain': total_back - equity,
        'holding_period_years': n,
        'cash_flows': [{'year': t, 'cash_flow': cf} for t, cf in enumerate(flows)],
    }
    if exit_ev is not None:
        out['exit_enterprise_value'] = exit_ev
        out['exit_net_debt'] = exit_net_debt
        if exit_equity < 0:
            out['note'] = 'Exit net debt exceeds exit EV: equity is wiped out (proceeds floored at 0).'
    if has(p, 'entry_valuation'):
        entry_ev = num(p, 'entry_valuation', gt=0)
        out['entry_enterprise_value'] = entry_ev
        out['implied_entry_net_debt'] = entry_ev - equity
        out['entry_equity_share_pct'] = equity / entry_ev * 100
    return out


JSON_COMMANDS = {'calculate': _json_calculate, 'returns_json': _json_calculate}


def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json(JSON_COMMANDS)


if __name__ == '__main__':
    main()
