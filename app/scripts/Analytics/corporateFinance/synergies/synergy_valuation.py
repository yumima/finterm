"""Comprehensive Synergy Valuation"""
from typing import Dict, Any, List, Optional
import numpy as np

class SynergyValuation:
    """Comprehensive synergy valuation combining revenue and cost synergies"""

    def __init__(self, wacc: float = 0.10,
                 terminal_growth_rate: float = 0.02,
                 tax_rate: float = 0.25):
        self.wacc = wacc
        self.terminal_growth_rate = terminal_growth_rate
        self.tax_rate = tax_rate

    def value_synergies_dcf(self, annual_revenue_synergies: List[float],
                           annual_cost_synergies: List[float],
                           integration_costs_by_year: List[float],
                           projection_years: int = 10) -> Dict[str, Any]:
        """Value synergies using DCF methodology"""

        if len(annual_revenue_synergies) < projection_years:
            last_revenue = annual_revenue_synergies[-1] if annual_revenue_synergies else 0
            annual_revenue_synergies.extend([last_revenue] * (projection_years - len(annual_revenue_synergies)))

        if len(annual_cost_synergies) < projection_years:
            last_cost = annual_cost_synergies[-1] if annual_cost_synergies else 0
            annual_cost_synergies.extend([last_cost] * (projection_years - len(annual_cost_synergies)))

        if len(integration_costs_by_year) < projection_years:
            integration_costs_by_year.extend([0] * (projection_years - len(integration_costs_by_year)))

        yearly_cash_flows = []
        total_pv = 0

        for year in range(1, projection_years + 1):
            idx = year - 1

            revenue_synergy = annual_revenue_synergies[idx]
            cost_synergy = annual_cost_synergies[idx]
            integration_cost = integration_costs_by_year[idx]

            gross_synergy = revenue_synergy + cost_synergy
            after_tax_synergy = gross_synergy * (1 - self.tax_rate)
            net_cash_flow = after_tax_synergy - integration_cost

            discount_factor = (1 + self.wacc) ** year
            pv = net_cash_flow / discount_factor

            total_pv += pv

            yearly_cash_flows.append({
                'year': year,
                'revenue_synergy': revenue_synergy,
                'cost_synergy': cost_synergy,
                'integration_cost': integration_cost,
                'gross_synergy': gross_synergy,
                'after_tax_synergy': after_tax_synergy,
                'net_cash_flow': net_cash_flow,
                'discount_factor': discount_factor,
                'present_value': pv
            })

        if self.terminal_growth_rate >= self.wacc:
            raise ValueError("terminal growth rate must be below the discount rate (WACC)")
        terminal_year_synergy = gross_synergy * (1 - self.tax_rate)
        terminal_value = terminal_year_synergy * (1 + self.terminal_growth_rate) / (self.wacc - self.terminal_growth_rate)
        terminal_pv = terminal_value / ((1 + self.wacc) ** projection_years)

        total_synergy_value = total_pv + terminal_pv

        return {
            'explicit_forecast_pv': total_pv,
            'terminal_value': terminal_value,
            'terminal_value_pv': terminal_pv,
            'total_synergy_value': total_synergy_value,
            'terminal_value_pct': (terminal_pv / total_synergy_value * 100) if total_synergy_value > 0 else 0,
            'yearly_projections': yearly_cash_flows,
            'valuation_assumptions': {
                'wacc': self.wacc * 100,
                'terminal_growth_rate': self.terminal_growth_rate * 100,
                'tax_rate': self.tax_rate * 100,
                'projection_years': projection_years
            }
        }

    def calculate_synergy_multiple(self, total_synergy_value: float,
                                   purchase_price: float,
                                   standalone_target_value: Optional[float] = None) -> Dict[str, Any]:
        """Calculate synergy contribution to deal value"""

        if standalone_target_value:
            premium_paid = purchase_price - standalone_target_value
            synergy_captured_by_acquirer = total_synergy_value - premium_paid
            synergy_split_acquirer = (synergy_captured_by_acquirer / total_synergy_value * 100) if total_synergy_value > 0 else 0
            synergy_split_target = 100 - synergy_split_acquirer
        else:
            premium_paid = None
            synergy_captured_by_acquirer = None
            synergy_split_acquirer = None
            synergy_split_target = None

        synergy_to_price_ratio = (total_synergy_value / purchase_price * 100) if purchase_price > 0 else 0

        return {
            'total_synergy_value': total_synergy_value,
            'purchase_price': purchase_price,
            'standalone_target_value': standalone_target_value,
            'premium_paid': premium_paid,
            'synergy_captured_by_acquirer': synergy_captured_by_acquirer,
            'synergy_split': {
                'acquirer_pct': synergy_split_acquirer,
                'target_pct': synergy_split_target
            },
            'synergy_to_price_ratio': synergy_to_price_ratio,
            'value_creation_analysis': {
                'creates_value': total_synergy_value > (premium_paid if premium_paid else 0),
                'npv_of_deal': total_synergy_value - (premium_paid if premium_paid else 0) if premium_paid else None
            }
        }

    def synergy_sensitivity_analysis(self, base_revenue_synergy: float,
                                    base_cost_synergy: float,
                                    revenue_scenarios: List[float],
                                    cost_scenarios: List[float]) -> Dict[str, Any]:
        """Two-way sensitivity analysis for synergy assumptions"""

        sensitivity_matrix = []

        for revenue_mult in revenue_scenarios:
            for cost_mult in cost_scenarios:
                revenue_synergy = base_revenue_synergy * revenue_mult
                cost_synergy = base_cost_synergy * cost_mult

                total_synergy = revenue_synergy + cost_synergy
                after_tax = total_synergy * (1 - self.tax_rate)

                perpetuity_value = after_tax / self.wacc

                sensitivity_matrix.append({
                    'revenue_multiplier': revenue_mult,
                    'cost_multiplier': cost_mult,
                    'revenue_synergy': revenue_synergy,
                    'cost_synergy': cost_synergy,
                    'total_synergy': total_synergy,
                    'synergy_value': perpetuity_value
                })

        min_value = min(s['synergy_value'] for s in sensitivity_matrix)
        max_value = max(s['synergy_value'] for s in sensitivity_matrix)
        base_case = [s for s in sensitivity_matrix if s['revenue_multiplier'] == 1.0 and s['cost_multiplier'] == 1.0][0]

        return {
            'sensitivity_matrix': sensitivity_matrix,
            'value_range': {
                'min': min_value,
                'max': max_value,
                'base': base_case['synergy_value']
            },
            'base_assumptions': {
                'revenue_synergy': base_revenue_synergy,
                'cost_synergy': base_cost_synergy
            }
        }

    def calculate_probability_adjusted_synergies(self, synergy_scenarios: List[Dict[str, Any]]) -> Dict[str, Any]:
        """Calculate probability-weighted synergy value"""

        total_probability = sum(s['probability'] for s in synergy_scenarios)
        if abs(total_probability - 1.0) > 0.01:
            raise ValueError(f"Probabilities must sum to 1.0, got {total_probability}")

        scenario_analysis = []
        expected_value = 0

        for scenario in synergy_scenarios:
            synergy_value = scenario['synergy_value']
            probability = scenario['probability']
            weighted_value = synergy_value * probability

            expected_value += weighted_value

            scenario_analysis.append({
                'scenario_name': scenario['name'],
                'probability': probability * 100,
                'synergy_value': synergy_value,
                'weighted_value': weighted_value
            })

        values = [s['synergy_value'] for s in synergy_scenarios]
        std_dev = np.std(values)
        cv = (std_dev / expected_value * 100) if expected_value > 0 else 0

        return {
            'expected_synergy_value': expected_value,
            'scenarios': scenario_analysis,
            'value_range': {
                'min': min(values),
                'max': max(values)
            },
            'standard_deviation': std_dev,
            'coefficient_of_variation': cv
        }

    def synergy_realization_schedule(self, total_synergies: float,
                                    realization_curve: str = 'linear',
                                    full_realization_years: int = 3) -> Dict[str, Any]:
        """Model synergy realization over time"""

        yearly_realization = []

        for year in range(1, full_realization_years + 4):
            if realization_curve == 'linear':
                if year <= full_realization_years:
                    pct = year / full_realization_years
                else:
                    pct = 1.0

            elif realization_curve == 's_curve':
                if year <= full_realization_years:
                    pct = (year / full_realization_years) ** 0.7
                else:
                    pct = 1.0

            elif realization_curve == 'front_loaded':
                if year <= full_realization_years:
                    pct = 1 - (1 - year / full_realization_years) ** 2
                else:
                    pct = 1.0

            else:
                pct = year / full_realization_years if year <= full_realization_years else 1.0

            synergy_realized = total_synergies * pct

            # Calculate incremental before appending current entry
            prev_synergy = yearly_realization[-1]['synergies_realized'] if yearly_realization else 0
            yearly_realization.append({
                'year': year,
                'realization_pct': pct * 100,
                'synergies_realized': synergy_realized,
                'incremental_synergies': synergy_realized - prev_synergy
            })

        return {
            'total_synergies': total_synergies,
            'realization_curve': realization_curve,
            'full_realization_years': full_realization_years,
            'yearly_schedule': yearly_realization
        }

    def compare_deal_with_without_synergies(self, purchase_price: float,
                                           standalone_target_value: float,
                                           synergy_value: float,
                                           acquirer_market_cap: float) -> Dict[str, Any]:
        """Compare deal economics with and without synergies"""

        premium_paid = purchase_price - standalone_target_value
        premium_pct = (premium_paid / standalone_target_value * 100) if standalone_target_value > 0 else 0

        without_synergies_npv = -premium_paid
        with_synergies_npv = synergy_value - premium_paid

        value_creation = with_synergies_npv > 0

        implied_acquirer_value_change = with_synergies_npv
        implied_stock_impact = (implied_acquirer_value_change / acquirer_market_cap * 100) if acquirer_market_cap > 0 else 0

        return {
            'purchase_price': purchase_price,
            'standalone_target_value': standalone_target_value,
            'premium_paid': premium_paid,
            'premium_pct': premium_pct,
            'synergy_value': synergy_value,
            'without_synergies': {
                'npv': without_synergies_npv,
                'destroys_value': without_synergies_npv < 0
            },
            'with_synergies': {
                'npv': with_synergies_npv,
                'creates_value': value_creation
            },
            'synergy_impact': {
                'value_swing': with_synergies_npv - without_synergies_npv,
                'synergy_justifies_premium': synergy_value > premium_paid
            },
            'acquirer_impact': {
                'market_cap': acquirer_market_cap,
                'implied_value_change': implied_acquirer_value_change,
                'implied_stock_impact_pct': implied_stock_impact
            }
        }

# ── JSON contract (MAAnalyticsService "value") ───────────────────────────────
# Every assumption is a caller input; nothing below substitutes a default rate.
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
from corporateFinance._cli import (is_json_call, run_json, num, opt_num, has,  # noqa: E402
                                   InputError, pct)


def _first(p: Dict[str, Any], *keys: str) -> Optional[str]:
    """First key (of aliases) present in p."""
    for k in keys:
        if has(p, k):
            return k
    return None


def projection_inputs(p: Dict[str, Any]) -> Dict[str, Any]:
    """Shared timing/discounting inputs for every synergy projection."""
    out = {
        'tax_rate': num(p, 'tax_rate', label='tax_rate (decimal)', min=0, max=0.99),
        'discount_rate': num(p, 'discount_rate', label='discount_rate (decimal)', gt=0, max=1),
        'ramp_years': num(p, 'ramp_years', label='ramp_years (years to full run-rate)', min=1, max=30, integer=True),
        'projection_years': num(p, 'projection_years', label='projection_years', min=1, max=50, integer=True),
        'terminal_growth': opt_num(p, 'terminal_growth', label='terminal_growth (decimal)', min=-0.5, max=0.5),
    }
    if out['terminal_growth'] is not None and out['terminal_growth'] >= out['discount_rate']:
        raise InputError("terminal_growth must be below discount_rate")
    out['integration_cost_years'] = int(opt_num(p, 'integration_cost_years', out['ramp_years'],
                                                label='integration_cost_years', min=1, max=30, integer=True))
    return out


def project_synergies(run_rate_revenue_synergy: float,
                      revenue_synergy_margin: Optional[float],
                      run_rate_cost_synergy: float,
                      one_time_cost: float,
                      tax_rate: float,
                      discount_rate: float,
                      ramp_years: int,
                      projection_years: int,
                      terminal_growth: Optional[float] = None,
                      integration_cost_years: Optional[int] = None) -> Dict[str, Any]:
    """Year-by-year synergy DCF.

    - Run-rate synergies phase in linearly: year y realises min(1, y / ramp_years).
    - Revenue synergies count only at `revenue_synergy_margin` (incremental
      EBITDA per $ of synergy revenue); cost synergies are EBITDA directly.
    - Pre-tax synergy EBITDA is taxed at tax_rate; one-time integration costs
      are tax-deductible, spread evenly over `integration_cost_years`.
    - End-of-year discounting at discount_rate. A Gordon terminal value on the
      final-year after-tax run-rate synergy is added only when terminal_growth
      is supplied.
    """
    if run_rate_revenue_synergy and revenue_synergy_margin is None:
        raise InputError("revenue_synergy_margin is required when revenue synergies are non-zero")
    margin = revenue_synergy_margin or 0.0
    n_cost_years = integration_cost_years or ramp_years
    per_year_cost = one_time_cost / n_cost_years if one_time_cost else 0.0

    rows = []
    explicit_pv = 0.0
    cumulative = 0.0
    payback_year = None
    for year in range(1, projection_years + 1):
        realization = min(1.0, year / ramp_years)
        rev = run_rate_revenue_synergy * realization
        rev_ebitda = rev * margin
        cost = run_rate_cost_synergy * realization
        pretax = rev_ebitda + cost
        after_tax = pretax * (1 - tax_rate)
        integ = per_year_cost if year <= n_cost_years else 0.0
        integ_after_tax = integ * (1 - tax_rate)
        net = after_tax - integ_after_tax
        df = (1 + discount_rate) ** year
        pv = net / df
        explicit_pv += pv
        cumulative += net
        if payback_year is None and cumulative >= 0 and (one_time_cost or 0) > 0:
            payback_year = year
        rows.append({
            'year': year,
            'realization_pct': realization * 100,
            'revenue_synergy': rev,
            'revenue_synergy_ebitda': rev_ebitda,
            'cost_synergy': cost,
            'pretax_synergy': pretax,
            'after_tax_synergy': after_tax,
            'integration_cost': integ,
            'net_cash_flow': net,
            'present_value': pv,
        })

    run_rate_pretax = run_rate_revenue_synergy * margin + run_rate_cost_synergy
    run_rate_after_tax = run_rate_pretax * (1 - tax_rate)
    terminal_value = terminal_pv = None
    if terminal_growth is not None:
        final_after_tax = rows[-1]['after_tax_synergy']
        terminal_value = final_after_tax * (1 + terminal_growth) / (discount_rate - terminal_growth)
        terminal_pv = terminal_value / (1 + discount_rate) ** projection_years
    total = explicit_pv + (terminal_pv or 0.0)
    integration_pv = sum(r['integration_cost'] * (1 - tax_rate) / (1 + discount_rate) ** r['year'] for r in rows)
    return {
        'total_synergy_value': total,
        'explicit_period_pv': explicit_pv,
        'terminal_value': terminal_value,
        'terminal_value_pv': terminal_pv,
        'terminal_share_pct': (terminal_pv / total * 100) if (terminal_pv is not None and total) else None,
        'run_rate_revenue_synergy': run_rate_revenue_synergy,
        'run_rate_cost_synergy': run_rate_cost_synergy,
        'run_rate_pretax_synergy': run_rate_pretax,
        'run_rate_after_tax_synergy': run_rate_after_tax,
        'one_time_integration_cost': one_time_cost,
        'integration_cost_after_tax_pv': integration_pv,
        'payback_year': payback_year,
        'discount_rate_pct': pct(discount_rate),
        'tax_rate_pct': pct(tax_rate),
        'revenue_synergy_margin_pct': pct(revenue_synergy_margin) if revenue_synergy_margin is not None else None,
        'terminal_growth_pct': pct(terminal_growth),
        'ramp_years': ramp_years,
        'projection_years': projection_years,
        'integration_cost_years': n_cost_years,
        'yearly_projections': rows,
    }


def _run_rate(p: Dict[str, Any], amount_keys, pct_key: str, base_keys, what: str) -> float:
    """Run-rate synergy from an explicit amount, or pct x base."""
    k = _first(p, *amount_keys)
    if k:
        return num(p, k, min=0)
    if has(p, pct_key):
        frac = num(p, pct_key, label=f"{pct_key} (decimal)", min=0, max=1)
        if frac == 0:
            return 0.0
        b = _first(p, *base_keys)
        if not b:
            raise InputError(f"{pct_key} needs the base it applies to: {' or '.join(base_keys)}")
        return frac * num(p, b, min=0)
    return 0.0


def json_value(p: Dict[str, Any]) -> Dict[str, Any]:
    """Synergy DCF (revenue + cost - integration costs).

    Inputs: revenue_synergy ($ run-rate) or revenue_synergy_pct x combined_revenue;
    cost_synergy / annual_synergies ($ run-rate, EBITDA-level) or
    cost_synergy_pct x combined_cost_base; revenue_synergy_margin (needed when
    revenue synergies > 0); integration_cost ($, optional); tax_rate,
    discount_rate, ramp_years, projection_years (required); terminal_growth,
    integration_cost_years (optional).
    """
    rev = _run_rate(p, ('revenue_synergy', 'annual_revenue_synergy'), 'revenue_synergy_pct',
                    ('combined_revenue', 'revenue_base'), 'revenue')
    cost = _run_rate(p, ('cost_synergy', 'annual_cost_synergy', 'annual_synergies'), 'cost_synergy_pct',
                     ('combined_cost_base', 'combined_opex', 'cost_base'), 'cost')
    if rev == 0 and cost == 0:
        raise InputError("No synergies supplied: give revenue_synergy(_pct) and/or cost_synergy(_pct)")
    margin = opt_num(p, 'revenue_synergy_margin', label='revenue_synergy_margin (decimal)', min=0, max=1)
    one_time = opt_num(p, 'integration_cost', 0.0, min=0)
    t = projection_inputs(p)
    return project_synergies(rev, margin, cost, one_time, **t)

def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json({"value": json_value})


if __name__ == '__main__':
    main()
