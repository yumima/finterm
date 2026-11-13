"""Fairness Opinion Valuation Framework"""
import math
import sys
from pathlib import Path
from typing import Dict, Any, List, Optional
from dataclasses import dataclass
from enum import Enum
import numpy as np

_ANALYTICS = str(Path(__file__).resolve().parent.parent.parent)
if _ANALYTICS not in sys.path:
    sys.path.insert(0, _ANALYTICS)

class ValuationMethod(Enum):
    DCF = "dcf"
    PRECEDENT_TRANSACTIONS = "precedent_transactions"
    TRADING_COMPS = "trading_comps"
    LBO_ANALYSIS = "lbo_analysis"
    PREMIUMS_PAID = "premiums_paid"
    REPLACEMENT_COST = "replacement_cost"

@dataclass
class ValuationRange:
    method: ValuationMethod
    low: float
    high: float
    midpoint: float
    weight: float

class FairnessOpinionFramework:
    """Framework for investment banking fairness opinions"""

    def __init__(self, company_name: str, offer_price: float):
        self.company_name = company_name
        self.offer_price = offer_price

    def calculate_dcf_valuation(self, free_cash_flows: List[float],
                               terminal_value: float,
                               wacc_low: float,
                               wacc_high: float) -> Dict[str, Any]:
        """Calculate DCF valuation range"""

        def npv(fcf: List[float], tv: float, discount_rate: float) -> float:
            pv_fcf = sum(cf / ((1 + discount_rate) ** (i + 1)) for i, cf in enumerate(fcf))
            pv_tv = tv / ((1 + discount_rate) ** len(fcf))
            return pv_fcf + pv_tv

        low_value = npv(free_cash_flows, terminal_value, wacc_high)
        high_value = npv(free_cash_flows, terminal_value, wacc_low)
        mid_value = npv(free_cash_flows, terminal_value, (wacc_low + wacc_high) / 2)

        return {
            'method': ValuationMethod.DCF.value,
            'valuation_range': {
                'low': low_value,
                'high': high_value,
                'midpoint': mid_value
            },
            'assumptions': {
                'wacc_low': wacc_low * 100,
                'wacc_high': wacc_high * 100,
                'terminal_value': terminal_value,
                'projection_years': len(free_cash_flows)
            },
            'fcf_projections': free_cash_flows
        }

    def calculate_trading_comps_valuation(self, subject_metric: float,
                                         multiples_low: float,
                                         multiples_high: float,
                                         metric_name: str = "EBITDA") -> Dict[str, Any]:
        """Calculate trading comparables valuation range"""

        low_value = subject_metric * multiples_low
        high_value = subject_metric * multiples_high
        mid_value = subject_metric * ((multiples_low + multiples_high) / 2)

        return {
            'method': ValuationMethod.TRADING_COMPS.value,
            'valuation_range': {
                'low': low_value,
                'high': high_value,
                'midpoint': mid_value
            },
            'assumptions': {
                'metric_name': metric_name,
                'subject_metric': subject_metric,
                'multiple_range_low': multiples_low,
                'multiple_range_high': multiples_high
            }
        }

    def calculate_precedent_transactions_valuation(self, subject_metric: float,
                                                   multiples_low: float,
                                                   multiples_high: float,
                                                   metric_name: str = "EBITDA") -> Dict[str, Any]:
        """Calculate precedent transactions valuation range"""

        low_value = subject_metric * multiples_low
        high_value = subject_metric * multiples_high
        mid_value = subject_metric * ((multiples_low + multiples_high) / 2)

        return {
            'method': ValuationMethod.PRECEDENT_TRANSACTIONS.value,
            'valuation_range': {
                'low': low_value,
                'high': high_value,
                'midpoint': mid_value
            },
            'assumptions': {
                'metric_name': metric_name,
                'subject_metric': subject_metric,
                'multiple_range_low': multiples_low,
                'multiple_range_high': multiples_high
            }
        }

    def calculate_lbo_analysis(self, entry_price: float,
                               exit_multiple_low: float,
                               exit_multiple_high: float,
                               ebitda_at_exit: float,
                               years_to_exit: int = 5,
                               target_irr: float = 0.20) -> Dict[str, Any]:
        """Calculate implied valuation from LBO analysis"""

        exit_value_low = ebitda_at_exit * exit_multiple_low
        exit_value_high = ebitda_at_exit * exit_multiple_high

        implied_value_low = exit_value_low / ((1 + target_irr) ** years_to_exit)
        implied_value_high = exit_value_high / ((1 + target_irr) ** years_to_exit)
        implied_value_mid = (implied_value_low + implied_value_high) / 2

        return {
            'method': ValuationMethod.LBO_ANALYSIS.value,
            'valuation_range': {
                'low': implied_value_low,
                'high': implied_value_high,
                'midpoint': implied_value_mid
            },
            'assumptions': {
                'exit_multiple_low': exit_multiple_low,
                'exit_multiple_high': exit_multiple_high,
                'ebitda_at_exit': ebitda_at_exit,
                'years_to_exit': years_to_exit,
                'target_irr': target_irr * 100
            }
        }

    def calculate_premiums_analysis(self, unaffected_price: float,
                                    premium_low: float,
                                    premium_high: float) -> Dict[str, Any]:
        """Calculate valuation based on acquisition premiums"""

        low_value = unaffected_price * (1 + premium_low)
        high_value = unaffected_price * (1 + premium_high)
        mid_value = unaffected_price * (1 + (premium_low + premium_high) / 2)

        return {
            'method': ValuationMethod.PREMIUMS_PAID.value,
            'valuation_range': {
                'low': low_value,
                'high': high_value,
                'midpoint': mid_value
            },
            'assumptions': {
                'unaffected_price': unaffected_price,
                'premium_low': premium_low * 100,
                'premium_high': premium_high * 100
            }
        }

    def weighted_valuation_summary(self, valuation_ranges: List[ValuationRange]) -> Dict[str, Any]:
        """Create weighted valuation summary across all methods"""

        total_weight = sum(v.weight for v in valuation_ranges)

        if abs(total_weight - 1.0) > 0.01:
            raise ValueError(f"Weights must sum to 1.0, got {total_weight}")

        weighted_low = sum(v.low * v.weight for v in valuation_ranges)
        weighted_high = sum(v.high * v.weight for v in valuation_ranges)
        weighted_mid = sum(v.midpoint * v.weight for v in valuation_ranges)

        method_details = []
        for v in valuation_ranges:
            method_details.append({
                'method': v.method.value,
                'low': v.low,
                'high': v.high,
                'midpoint': v.midpoint,
                'weight': v.weight * 100,
                'contribution_to_weighted_mid': v.midpoint * v.weight
            })

        implied_premium_low = ((self.offer_price - weighted_low) / weighted_low * 100) if weighted_low > 0 else 0
        implied_premium_high = ((self.offer_price - weighted_high) / weighted_high * 100) if weighted_high > 0 else 0
        implied_premium_mid = ((self.offer_price - weighted_mid) / weighted_mid * 100) if weighted_mid > 0 else 0

        within_range = weighted_low <= self.offer_price <= weighted_high

        return {
            'company_name': self.company_name,
            'offer_price': self.offer_price,
            'weighted_valuation_range': {
                'low': weighted_low,
                'high': weighted_high,
                'midpoint': weighted_mid
            },
            'implied_premium': {
                'to_low': implied_premium_low,
                'to_high': implied_premium_high,
                'to_midpoint': implied_premium_mid
            },
            'offer_within_range': within_range,
            'methods_used': method_details,
            'position_in_range': ((self.offer_price - weighted_low) / (weighted_high - weighted_low) * 100) if (weighted_high - weighted_low) > 0 else 0
        }

    def football_field_analysis(self, valuation_ranges: List[ValuationRange]) -> Dict[str, Any]:
        """Create football field valuation chart data"""

        sorted_ranges = sorted(valuation_ranges, key=lambda x: x.midpoint)

        overall_low = min(v.low for v in valuation_ranges)
        overall_high = max(v.high for v in valuation_ranges)

        range_data = []
        for v in sorted_ranges:
            range_data.append({
                'method': v.method.value,
                'low': v.low,
                'high': v.high,
                'midpoint': v.midpoint,
                'range_width': v.high - v.low,
                'offer_within': v.low <= self.offer_price <= v.high
            })

        return {
            'valuation_ranges': range_data,
            'overall_range': {
                'low': overall_low,
                'high': overall_high
            },
            'offer_price': self.offer_price,
            'offer_position': {
                'below_all_ranges': self.offer_price < overall_low,
                'above_all_ranges': self.offer_price > overall_high,
                'within_at_least_one': any(r['offer_within'] for r in range_data)
            }
        }

    def sensitivity_analysis(self, base_valuation: float,
                            key_assumptions: List[Dict[str, Any]]) -> Dict[str, Any]:
        """Perform sensitivity analysis on key valuation assumptions"""

        sensitivity_results = []

        for assumption in key_assumptions:
            param_name = assumption['parameter']
            base_value = assumption['base_value']
            scenarios = assumption['scenarios']

            scenario_results = []
            for scenario in scenarios:
                variance_pct = scenario['variance_pct']
                adjusted_value = base_value * (1 + variance_pct / 100)

                valuation_impact_pct = scenario.get('valuation_impact_pct', variance_pct)
                adjusted_valuation = base_valuation * (1 + valuation_impact_pct / 100)

                scenario_results.append({
                    'scenario': scenario['name'],
                    'parameter_value': adjusted_value,
                    'variance_pct': variance_pct,
                    'valuation': adjusted_valuation,
                    'valuation_change_pct': valuation_impact_pct
                })

            sensitivity_results.append({
                'parameter': param_name,
                'base_value': base_value,
                'scenarios': scenario_results
            })

        return {
            'base_valuation': base_valuation,
            'sensitivity_analysis': sensitivity_results
        }

    def fairness_determination(self, weighted_valuation: Dict[str, Any],
                              qualitative_factors: List[str]) -> Dict[str, Any]:
        """Make fairness determination based on quantitative and qualitative analysis"""

        offer_price = weighted_valuation['offer_price']
        low = weighted_valuation['weighted_valuation_range']['low']
        high = weighted_valuation['weighted_valuation_range']['high']
        mid = weighted_valuation['weighted_valuation_range']['midpoint']

        within_range = weighted_valuation['offer_within_range']
        position = weighted_valuation['position_in_range']

        if within_range:
            if position >= 40 and position <= 60:
                quantitative_conclusion = "fair"
            elif position < 40:
                quantitative_conclusion = "fair_low_end"
            else:
                quantitative_conclusion = "fair_high_end"
        elif offer_price < low:
            shortfall_pct = ((low - offer_price) / low * 100)
            if shortfall_pct < 10:
                quantitative_conclusion = "marginally_below_range"
            else:
                quantitative_conclusion = "materially_below_range"
        else:
            premium_pct = ((offer_price - high) / high * 100)
            if premium_pct < 10:
                quantitative_conclusion = "marginally_above_range"
            else:
                quantitative_conclusion = "materially_above_range"

        is_fair = quantitative_conclusion in ["fair", "fair_low_end", "fair_high_end", "marginally_below_range", "marginally_above_range"]

        return {
            'offer_price': offer_price,
            'valuation_range': {
                'low': low,
                'high': high,
                'midpoint': mid
            },
            'quantitative_analysis': {
                'within_range': within_range,
                'position_in_range_pct': position,
                'conclusion': quantitative_conclusion
            },
            'qualitative_factors_considered': qualitative_factors,
            'fairness_conclusion': {
                'is_fair': is_fair,
                'conclusion': "fair from a financial point of view" if is_fair else "not fair from a financial point of view"
            }
        }

_FLAT_METHODS = [
    ('dcf_value', 'DCF'),
    ('comps_value', 'Trading Comps'),
    ('precedent_value', 'Precedent Transactions'),
]


def fairness_json(p: Dict[str, Any]) -> Dict[str, Any]:
    """JSON contract:
    {offer_price, methods: [{method, low, high} | {method, valuation}, weight?],
     week52_low?, week52_high?}   (flat dcf_value/comps_value/precedent_value accepted)

    Per-share values throughout. A method given as a single `valuation` is a
    point (low = high = valuation) -- no range is invented around it.
    Reference range = [min of method lows, max of method highs].
    Perspective: the target's shareholders. The offer supports a fairness
    conclusion when it is at or above the reference range's low end AND at or
    above the low end of a majority of the individual methods.
    """
    from corporateFinance._cli import InputError, has, num, pct
    offer = num(p, 'offer_price', label='Offer price per share', gt=0)

    methods = p.get('methods')
    if methods is None:
        methods = [{'method': label, 'valuation': p[key]} for key, label in _FLAT_METHODS if has(p, key)]
    if not isinstance(methods, list) or not methods:
        raise InputError("Missing required input: methods (valuation per share by method)")

    rows = []
    for i, m in enumerate(methods):
        if not isinstance(m, dict):
            raise InputError("each method must be an object")
        name = str(m.get('method') or f"Method {i + 1}")
        if has(m, 'low') and has(m, 'high'):
            low = num(m, 'low', label=f"{name} low", gt=0)
            high = num(m, 'high', label=f"{name} high", gt=0)
            if high < low:
                raise InputError(f"{name}: high ({high}) is below low ({low})")
            mid = num(m, 'midpoint', label=f"{name} midpoint", gt=0) if has(m, 'midpoint') else (low + high) / 2
        else:
            v = num(m, 'valuation', label=f"{name} valuation per share", gt=0)
            low = high = mid = v
        w = num(m, 'weight', label=f"{name} weight", min=0) if has(m, 'weight') else None
        rows.append({'method': name, 'low': low, 'high': high, 'midpoint': mid, 'weight': w,
                     'offer_vs_midpoint_pct': pct(offer / mid - 1.0),
                     'offer_at_or_above_low': offer >= low,
                     'offer_within_range': low <= offer <= high})

    weights = [r['weight'] for r in rows]
    if any(w is not None for w in weights):
        if any(w is None for w in weights):
            raise InputError("weight must be given for every method or for none")
        total_w = sum(weights)
        if total_w <= 0:
            raise InputError("method weights must sum to more than 0")
        weighting = 'user weights'
        mid_ref = sum(r['midpoint'] * r['weight'] for r in rows) / total_w
    else:
        weighting = 'equal weights'
        mid_ref = sum(r['midpoint'] for r in rows) / len(rows)
    for r in rows:
        r.pop('weight')

    ref_low = min(r['low'] for r in rows)
    ref_high = max(r['high'] for r in rows)
    supporting = sum(1 for r in rows if r['offer_at_or_above_low'])
    majority = math.floor(len(rows) / 2) + 1

    if offer < ref_low:
        position = 'below reference range'
    elif offer > ref_high:
        position = 'above reference range'
    else:
        position = 'within reference range'
    supports = offer >= ref_low and supporting >= majority

    out = {
        'offer_price': offer,
        'reference_low': ref_low,
        'reference_high': ref_high,
        'reference_midpoint': mid_ref,
        'midpoint_weighting': weighting,
        'offer_vs_midpoint_pct': pct(offer / mid_ref - 1.0),
        'offer_vs_low_pct': pct(offer / ref_low - 1.0),
        # 0% = at the reference low, 100% = at the high; null outside the range
        'position_in_range_pct': (pct((offer - ref_low) / (ref_high - ref_low))
                                  if ref_high > ref_low and ref_low <= offer <= ref_high else None),
        'offer_position': position,
        'methods_supporting': supporting,
        'methods_total': len(rows),
        'supports_fairness': supports,
        'conclusion': ('Offer is at or above the reference range low end and at or above the low end of '
                       f'{supporting} of {len(rows)} methods: supports a fairness conclusion'
                       if supports else
                       f'Offer is {position} and at or above the low end of only {supporting} of {len(rows)} '
                       'methods: does not support a fairness conclusion'),
        'perspective': "target shareholders (offer vs. standalone value per share)",
        'methods': rows,
    }
    if has(p, 'week52_low') or has(p, '52w_low'):
        lo = float(p.get('week52_low', p.get('52w_low')))
        out['offer_vs_52w_low_pct'] = pct(offer / lo - 1.0) if lo > 0 else None
    if has(p, 'week52_high') or has(p, '52w_high'):
        hi = float(p.get('week52_high', p.get('52w_high')))
        out['offer_vs_52w_high_pct'] = pct(offer / hi - 1.0) if hi > 0 else None
    return out


def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json({'generate': fairness_json})


if __name__ == '__main__':
    main()
