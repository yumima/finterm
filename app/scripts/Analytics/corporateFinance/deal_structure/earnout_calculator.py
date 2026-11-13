"""Earnout Calculator for Contingent Payments"""
from typing import Dict, Any, List, Optional
from dataclasses import dataclass
from enum import Enum
import numpy as np

class EarnoutMetric(Enum):
    REVENUE = "revenue"
    EBITDA = "ebitda"
    NET_INCOME = "net_income"
    USERS = "users"
    CUSTOM = "custom"

@dataclass
class EarnoutTranche:
    metric: EarnoutMetric
    threshold: float
    payment: float
    measurement_period_years: int
    description: str = ""

class EarnoutCalculator:
    """Calculate and value earnout provisions in M&A deals"""

    def __init__(self, discount_rate: float = 0.10):
        self.discount_rate = discount_rate

    def calculate_simple_earnout(self, base_price: float,
                                 earnout_tranches: List[EarnoutTranche],
                                 probability_weights: List[float]) -> Dict[str, Any]:
        """Calculate earnout value with probability weighting"""

        if len(earnout_tranches) != len(probability_weights):
            raise ValueError("Must provide probability for each tranche")

        # Each tranche has its own achievement probability; tranches are not
        # mutually exclusive outcomes, so the probabilities need not sum to 1.
        if any(not 0 <= p <= 1 for p in probability_weights):
            raise ValueError("Each tranche probability must be in [0, 1]")

        tranche_details = []
        total_expected_earnout = 0

        for tranche, probability in zip(earnout_tranches, probability_weights):
            pv = tranche.payment / ((1 + self.discount_rate) ** tranche.measurement_period_years)
            expected_value = pv * probability

            total_expected_earnout += expected_value

            tranche_details.append({
                'metric': tranche.metric.value,
                'threshold': tranche.threshold,
                'payment_if_achieved': tranche.payment,
                'measurement_period': tranche.measurement_period_years,
                'probability': probability * 100,
                'present_value': pv,
                'expected_value': expected_value,
                'description': tranche.description
            })

        total_consideration = base_price + total_expected_earnout
        earnout_pct = (total_expected_earnout / total_consideration * 100) if total_consideration > 0 else 0

        return {
            'base_price': base_price,
            'earnout_tranches': tranche_details,
            'total_earnout_face_value': sum(t.payment for t in earnout_tranches),
            'total_earnout_pv': sum(t['present_value'] for t in tranche_details),
            'total_earnout_expected_value': total_expected_earnout,
            'total_consideration': total_consideration,
            'earnout_as_pct_of_deal': earnout_pct,
            'discount_rate': self.discount_rate * 100
        }

    def tiered_earnout(self, base_price: float,
                      metric_name: str,
                      tiers: List[Dict[str, float]],
                      probability_distribution: List[float]) -> Dict[str, Any]:
        """
        Tiered earnout based on performance levels

        tiers: [{'threshold': value, 'payment': amount}, ...]
        probability_distribution: probability for each tier
        """

        if len(tiers) != len(probability_distribution):
            raise ValueError("Must provide probability for each tier")

        tier_analysis = []
        total_expected = 0

        for tier, prob in zip(tiers, probability_distribution):
            expected = tier['payment'] * prob

            tier_analysis.append({
                'threshold': tier['threshold'],
                'payment': tier['payment'],
                'probability': prob * 100,
                'expected_value': expected
            })

            total_expected += expected

        return {
            'base_price': base_price,
            'metric': metric_name,
            'tiers': tier_analysis,
            'max_earnout': max(t['payment'] for t in tiers),
            'expected_earnout': total_expected,
            'total_expected_consideration': base_price + total_expected
        }

    def continuous_earnout(self, base_price: float,
                          metric_baseline: float,
                          earnout_rate: float,
                          cap: Optional[float] = None,
                          measurement_years: int = 3,
                          expected_metric_path: List[float] = None) -> Dict[str, Any]:
        """
        Continuous earnout (e.g., $X per revenue dollar above threshold)

        Args:
            earnout_rate: Payment per unit of metric above baseline
            expected_metric_path: Projected metric values for each year
        """

        if expected_metric_path is None or len(expected_metric_path) != measurement_years:
            raise ValueError(f"Must provide {measurement_years} years of metric projections")

        yearly_earnouts = []
        total_earnout_pv = 0

        for year, metric_value in enumerate(expected_metric_path, 1):
            excess = max(0, metric_value - metric_baseline)
            earnout_payment = excess * earnout_rate

            if cap:
                earnout_payment = min(earnout_payment, cap)

            pv = earnout_payment / ((1 + self.discount_rate) ** year)
            total_earnout_pv += pv

            yearly_earnouts.append({
                'year': year,
                'metric_value': metric_value,
                'baseline': metric_baseline,
                'excess': excess,
                'earnout_payment': earnout_payment,
                'present_value': pv
            })

        return {
            'base_price': base_price,
            'earnout_structure': 'continuous',
            'baseline_metric': metric_baseline,
            'earnout_rate': earnout_rate,
            'cap': cap,
            'measurement_years': measurement_years,
            'yearly_details': yearly_earnouts,
            'total_earnout_pv': total_earnout_pv,
            'total_consideration': base_price + total_earnout_pv,
            'discount_rate': self.discount_rate * 100
        }

    def revenue_milestone_earnout(self, base_price: float,
                                  milestones: List[Dict[str, float]],
                                  probabilities: List[float],
                                  timing: List[int]) -> Dict[str, Any]:
        """
        Revenue milestone-based earnout

        Args:
            milestones: [{'revenue_target': X, 'payment': Y}, ...]
            probabilities: Achievement probability for each
            timing: Years until each milestone
        """

        if not (len(milestones) == len(probabilities) == len(timing)):
            raise ValueError("Milestones, probabilities, and timing must have same length")

        milestone_analysis = []
        total_expected_pv = 0

        for milestone, prob, years in zip(milestones, probabilities, timing):
            pv = milestone['payment'] / ((1 + self.discount_rate) ** years)
            expected_pv = pv * prob

            total_expected_pv += expected_pv

            milestone_analysis.append({
                'revenue_target': milestone['revenue_target'],
                'payment_if_achieved': milestone['payment'],
                'years_to_milestone': years,
                'probability': prob * 100,
                'present_value': pv,
                'expected_value': expected_pv
            })

        return {
            'base_price': base_price,
            'milestones': milestone_analysis,
            'total_milestone_payments': sum(m['payment'] for m in milestones),
            'total_expected_earnout_pv': total_expected_pv,
            'total_expected_consideration': base_price + total_expected_pv,
            'discount_rate': self.discount_rate * 100
        }

    def earnout_sensitivity(self, base_earnout: Dict[str, Any],
                           probability_scenarios: List[List[float]],
                           scenario_names: List[str]) -> Dict[str, Any]:
        """Sensitivity analysis for earnout value under different probability assumptions"""

        if 'earnout_tranches' not in base_earnout:
            raise ValueError("Base earnout must have tranche structure")

        results = []

        for probs, name in zip(probability_scenarios, scenario_names):
            total_expected = 0
            for tranche, prob in zip(base_earnout['earnout_tranches'], probs):
                expected = tranche['present_value'] * prob
                total_expected += expected

            total_consideration = base_earnout['base_price'] + total_expected

            results.append({
                'scenario': name,
                'probabilities': [p * 100 for p in probs],
                'expected_earnout': total_expected,
                'total_consideration': total_consideration
            })

        return {
            'base_case': base_earnout,
            'sensitivity_scenarios': results,
            'valuation_range': {
                'min': min(r['total_consideration'] for r in results),
                'max': max(r['total_consideration'] for r in results),
                'base': base_earnout['total_consideration']
            }
        }

    def earnout_risk_adjustment(self, earnout_value: float,
                                measurement_period: int,
                                execution_risk: str = 'medium') -> Dict[str, Any]:
        """Apply risk adjustment to earnout valuation"""

        risk_discounts = {
            'low': 0.10,
            'medium': 0.25,
            'high': 0.40,
            'very_high': 0.60
        }

        discount = risk_discounts.get(execution_risk, 0.25)

        risk_adjusted_value = earnout_value * (1 - discount)

        return {
            'unadjusted_earnout_value': earnout_value,
            'execution_risk_level': execution_risk,
            'risk_discount': discount * 100,
            'risk_adjusted_value': risk_adjusted_value,
            'value_haircut': earnout_value - risk_adjusted_value,
            'measurement_period_years': measurement_period
        }

# ── JSON contract (MAAnalyticsService "calculate") ───────────────────────────
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
from corporateFinance._cli import is_json_call, run_json, num, opt_num, has, InputError, pct  # noqa: E402


def _first_key(p: Dict[str, Any], *keys: str) -> Optional[str]:
    for k in keys:
        if has(p, k):
            return k
    return None


def _req(p: Dict[str, Any], keys, label: str, **kw) -> float:
    k = _first_key(p, *keys)
    if not k:
        raise InputError(f"Missing required input: {label}")
    return num(p, k, label=label, **kw)


def json_calculate(p: Dict[str, Any]) -> Dict[str, Any]:
    """Probability-weighted PV of an earnout.

    Single tranche: earnout_amount (alias max_earnout), probability (alias
    probability_achieve, decimal), years (alias period / years_to_payment),
    discount_rate (decimal) -- all required; threshold (alias
    revenue_threshold / target_metric) is informational.
    Multiple tranches: tranches = [{payment, probability, years, threshold?}].
    Optional base_price (upfront consideration) for the total.
    Value = sum(probability_i * payment_i / (1 + discount_rate) ** years_i).
    """
    r = num(p, 'discount_rate', label='discount_rate (decimal)', min=0, max=1)
    raw = p.get('tranches')
    if raw is not None:
        if not isinstance(raw, list) or not raw:
            raise InputError("tranches must be a non-empty list of {payment, probability, years}")
        specs = raw
    else:
        specs = [{
            'payment': _req(p, ('earnout_amount', 'max_earnout', 'payment'), 'earnout_amount', min=0),
            'probability': _req(p, ('probability', 'probability_achieve'), 'probability (decimal 0-1)',
                                min=0, max=1),
            'years': _req(p, ('years', 'period', 'years_to_payment'), 'years (to payment)', min=0),
            'threshold': p.get('threshold', p.get('revenue_threshold', p.get('target_metric'))),
        }]

    rows = []
    for i, t in enumerate(specs):
        if not isinstance(t, dict):
            raise InputError(f"tranches[{i}] must be an object")
        pay = num(t, 'payment', label=f'tranches[{i}].payment', min=0)
        prob = num(t, 'probability', label=f'tranches[{i}].probability (decimal)', min=0, max=1)
        yrs = num(t, 'years', label=f'tranches[{i}].years', min=0)
        pv = pay / (1 + r) ** yrs
        rows.append({
            'tranche': i + 1,
            'threshold': float(t['threshold']) if t.get('threshold') not in (None, '') else None,
            'payment_if_achieved': pay,
            'probability_pct': pct(prob),
            'years_to_payment': yrs,
            'pv_if_achieved': pv,
            'expected_value': prob * pv,
        })

    face = sum(x['payment_if_achieved'] for x in rows)
    expected = sum(x['expected_value'] for x in rows)
    out: Dict[str, Any] = {
        'earnout_expected_value': expected,
        'earnout_face_value': face,
        'pv_if_fully_achieved': sum(x['pv_if_achieved'] for x in rows),
        'expected_value_share_of_face_pct': (expected / face * 100) if face else None,
        'discount_rate_pct': pct(r),
    }
    if len(rows) == 1:
        out['probability_pct'] = rows[0]['probability_pct']
        out['years_to_payment'] = rows[0]['years_to_payment']
        if rows[0]['threshold'] is not None:
            out['threshold'] = rows[0]['threshold']
    else:
        out['tranches'] = rows
    if has(p, 'base_price'):
        base = num(p, 'base_price', min=0)
        total = base + expected
        out['base_price'] = base
        out['total_expected_consideration'] = total
        out['earnout_share_of_consideration_pct'] = (expected / total * 100) if total else None
    return out

def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json({"calculate": json_calculate})


if __name__ == '__main__':
    main()
