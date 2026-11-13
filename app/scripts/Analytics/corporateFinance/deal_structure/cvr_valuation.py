"""Contingent Value Rights (CVR) Valuation"""
from typing import Dict, Any, List, Optional
from dataclasses import dataclass
from enum import Enum
import numpy as np

class CVRType(Enum):
    MILESTONE = "milestone"
    EARNOUT = "earnout"
    REGULATORY = "regulatory"
    COMMODITY_PRICE = "commodity_price"
    LITIGATION = "litigation"

@dataclass
class CVRTrigger:
    trigger_type: CVRType
    description: str
    payment_if_triggered: float
    probability: float
    expected_timing_years: float

class CVRValuation:
    """Valuation of Contingent Value Rights in M&A transactions"""

    def __init__(self, discount_rate: float = 0.12,
                 risk_free_rate: float = 0.04):
        self.discount_rate = discount_rate
        self.risk_free_rate = risk_free_rate

    def value_milestone_cvr(self, triggers: List[CVRTrigger]) -> Dict[str, Any]:
        """Value milestone-based CVR (FDA approval, product launch, etc.)"""

        trigger_analysis = []
        total_expected_pv = 0

        for trigger in triggers:
            pv = trigger.payment_if_triggered / ((1 + self.discount_rate) ** trigger.expected_timing_years)
            expected_value = pv * trigger.probability

            total_expected_pv += expected_value

            trigger_analysis.append({
                'trigger_type': trigger.trigger_type.value,
                'description': trigger.description,
                'payment_if_triggered': trigger.payment_if_triggered,
                'probability': trigger.probability * 100,
                'expected_timing_years': trigger.expected_timing_years,
                'present_value': pv,
                'expected_value': expected_value
            })

        return {
            'cvr_type': 'milestone',
            'triggers': trigger_analysis,
            'total_face_value': sum(t.payment_if_triggered for t in triggers),
            'total_expected_value': total_expected_pv,
            'discount_rate': self.discount_rate * 100,
            'blended_probability': (total_expected_pv / sum(t['present_value'] for t in trigger_analysis) * 100) if trigger_analysis else 0
        }

    def value_regulatory_cvr(self, payment_if_approved: float,
                            approval_probability: float,
                            expected_decision_years: float,
                            appeal_possible: bool = False,
                            appeal_probability: float = 0,
                            appeal_delay_years: float = 0) -> Dict[str, Any]:
        """Value regulatory approval CVR (common in pharma/biotech deals)"""

        base_pv = payment_if_approved / ((1 + self.discount_rate) ** expected_decision_years)
        base_expected = base_pv * approval_probability

        if appeal_possible and approval_probability < 1.0:
            rejection_prob = 1 - approval_probability
            appeal_success_prob = rejection_prob * appeal_probability

            appeal_timing = expected_decision_years + appeal_delay_years
            appeal_pv = payment_if_approved / ((1 + self.discount_rate) ** appeal_timing)
            appeal_expected = appeal_pv * appeal_success_prob

            total_expected = base_expected + appeal_expected
            blended_probability = approval_probability + appeal_success_prob
        else:
            appeal_expected = 0
            total_expected = base_expected
            blended_probability = approval_probability

        return {
            'cvr_type': 'regulatory',
            'payment_if_approved': payment_if_approved,
            'base_approval_probability': approval_probability * 100,
            'expected_decision_years': expected_decision_years,
            'base_expected_value': base_expected,
            'appeal_scenario': {
                'appeal_possible': appeal_possible,
                'appeal_probability': appeal_probability * 100 if appeal_possible else 0,
                'appeal_expected_value': appeal_expected,
                'appeal_delay_years': appeal_delay_years
            },
            'total_expected_value': total_expected,
            'blended_success_probability': blended_probability * 100,
            'discount_rate': self.discount_rate * 100
        }

    def value_commodity_price_cvr(self, payment_schedule: List[Dict[str, float]],
                                  current_price: float,
                                  price_volatility: float,
                                  years_to_maturity: float) -> Dict[str, Any]:
        """
        Value commodity price-linked CVR

        payment_schedule: [{'threshold': price, 'payment': amount}, ...]
        Uses simplified option pricing logic
        """

        expected_payments = []
        total_expected_pv = 0

        from scipy.stats import norm
        sigma_rt = price_volatility * np.sqrt(years_to_maturity)
        for tier in payment_schedule:
            threshold = tier['threshold']
            payment = tier['payment']

            # Cash-or-nothing digital: risk-neutral P(S_T > K) = N(d2) with
            # d2 = (ln(S/K) + (r - sigma^2/2) T) / (sigma sqrt T), and the
            # payment discounted at the risk-free rate (consistent measure).
            d2 = (np.log(current_price / threshold)
                  + (self.risk_free_rate - 0.5 * price_volatility ** 2) * years_to_maturity) / sigma_rt
            probability = float(norm.cdf(d2))

            pv = payment * np.exp(-self.risk_free_rate * years_to_maturity)
            expected_value = pv * probability

            total_expected_pv += expected_value

            expected_payments.append({
                'price_threshold': threshold,
                'payment_if_reached': payment,
                'probability': probability * 100,
                'present_value': pv,
                'expected_value': expected_value
            })

        return {
            'cvr_type': 'commodity_price',
            'current_price': current_price,
            'price_volatility': price_volatility * 100,
            'years_to_maturity': years_to_maturity,
            'payment_tiers': expected_payments,
            'total_expected_value': total_expected_pv,
            'total_max_payment': sum(t['payment'] for t in payment_schedule)
        }

    def value_litigation_cvr(self, case_value: float,
                            win_probability: float,
                            expected_resolution_years: float,
                            legal_costs: float = 0,
                            settlement_probability: float = 0,
                            settlement_amount: float = 0,
                            settlement_timing_years: Optional[float] = None) -> Dict[str, Any]:
        """Value litigation outcome CVR"""

        win_pv = (case_value - legal_costs) / ((1 + self.discount_rate) ** expected_resolution_years)
        win_expected = win_pv * win_probability

        if settlement_probability > 0:
            if settlement_timing_years is None:
                raise ValueError("settlement_timing_years is required when settlement_probability > 0")
            settlement_timing = settlement_timing_years
            settlement_pv = settlement_amount / ((1 + self.discount_rate) ** settlement_timing)
            settlement_expected = settlement_pv * settlement_probability

            no_settlement_prob = 1 - settlement_probability
            adjusted_win_expected = win_expected * no_settlement_prob
            total_expected = adjusted_win_expected + settlement_expected
        else:
            settlement_expected = 0
            total_expected = win_expected

        return {
            'cvr_type': 'litigation',
            'case_value': case_value,
            'win_probability': win_probability * 100,
            'expected_resolution_years': expected_resolution_years,
            'legal_costs': legal_costs,
            'win_scenario_expected_value': win_expected,
            'settlement': {
                'settlement_probability': settlement_probability * 100,
                'settlement_amount': settlement_amount,
                'settlement_expected_value': settlement_expected
            },
            'total_expected_value': total_expected,
            'discount_rate': self.discount_rate * 100
        }

    def value_earnout_cvr(self, base_earnout: float,
                         stretch_earnout: float,
                         base_probability: float,
                         stretch_probability: float,
                         years_to_measurement: int) -> Dict[str, Any]:
        """Value multi-tier earnout CVR"""

        if base_probability < 0 or stretch_probability < 0 or base_probability + stretch_probability > 1:
            raise ValueError("base_probability and stretch_probability are exclusive outcomes and must sum to <= 1")
        base_pv = base_earnout / ((1 + self.discount_rate) ** years_to_measurement)
        base_expected = base_pv * base_probability

        stretch_pv = (base_earnout + stretch_earnout) / ((1 + self.discount_rate) ** years_to_measurement)
        stretch_expected = stretch_pv * stretch_probability

        neither_prob = 1 - base_probability - stretch_probability
        neither_expected = 0

        total_expected = base_expected + stretch_expected

        return {
            'cvr_type': 'earnout',
            'base_earnout': base_earnout,
            'stretch_earnout': stretch_earnout,
            'total_max_payment': base_earnout + stretch_earnout,
            'years_to_measurement': years_to_measurement,
            'scenarios': {
                'base_achieved': {
                    'payment': base_earnout,
                    'probability': base_probability * 100,
                    'expected_value': base_expected
                },
                'stretch_achieved': {
                    'payment': base_earnout + stretch_earnout,
                    'probability': stretch_probability * 100,
                    'expected_value': stretch_expected
                },
                'neither_achieved': {
                    'payment': 0,
                    'probability': neither_prob * 100,
                    'expected_value': 0
                }
            },
            'total_expected_value': total_expected,
            'discount_rate': self.discount_rate * 100
        }

    def cvr_sensitivity_analysis(self, base_valuation: Dict[str, Any],
                                 probability_scenarios: List[float],
                                 timing_scenarios: List[float]) -> Dict[str, Any]:
        """Sensitivity analysis for CVR valuation"""

        base_payment = base_valuation.get('payment_if_approved') or base_valuation.get('case_value', 0)
        base_timing = base_valuation.get('expected_decision_years') or base_valuation.get('expected_resolution_years', 1)

        results = []

        for prob in probability_scenarios:
            for timing in timing_scenarios:
                pv = base_payment / ((1 + self.discount_rate) ** timing)
                expected = pv * prob

                results.append({
                    'probability': prob * 100,
                    'timing_years': timing,
                    'present_value': pv,
                    'expected_value': expected
                })

        return {
            'base_valuation': base_valuation,
            'sensitivity_matrix': results,
            'value_range': {
                'min': min(r['expected_value'] for r in results),
                'max': max(r['expected_value'] for r in results),
                'base': base_valuation.get('total_expected_value', 0)
            }
        }

    def compare_cvr_vs_cash(self, cash_alternative: float,
                           cvr_expected_value: float,
                           cvr_max_value: float,
                           target_shareholder_discount_rate: float = 0.15) -> Dict[str, Any]:
        """Compare CVR to cash alternative from target shareholder perspective"""

        cvr_certainty_equivalent = cvr_expected_value / (1 + target_shareholder_discount_rate)

        cash_superiority = cash_alternative > cvr_certainty_equivalent

        upside_scenario = cvr_max_value - cash_alternative
        downside_risk = cash_alternative - cvr_expected_value

        return {
            'cash_alternative': cash_alternative,
            'cvr_expected_value': cvr_expected_value,
            'cvr_max_value': cvr_max_value,
            'cvr_certainty_equivalent': cvr_certainty_equivalent,
            'cash_preferred': cash_superiority,
            'upside_potential': upside_scenario,
            'downside_risk': downside_risk,
            'risk_reward_ratio': upside_scenario / downside_risk if downside_risk > 0 else float('inf')
        }

# ── JSON contract (MAAnalyticsService "calculate") ───────────────────────────
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
from corporateFinance._cli import is_json_call, run_json, num, opt_num, text, has, InputError, pct  # noqa: E402


def _key(p: Dict[str, Any], *keys: str) -> Optional[str]:
    for k in keys:
        if has(p, k):
            return k
    return None


def json_calculate(p: Dict[str, Any]) -> Dict[str, Any]:
    """Expected PV of a binary-trigger CVR: probability x payment / (1 + r) ** t.

    Inputs (required): payment (alias max_payout / cvr_payment /
    payment_if_triggered; total $ paid if triggered), probability (decimal),
    years (alias expiry_years / time_to_milestone), discount_rate (decimal).
    Optional: type (milestone | revenue | regulatory; label), shares_outstanding
    (per-share values), appeal_probability + appeal_delay_years (regulatory:
    chance a rejection is overturned on appeal, paid later), cash_alternative
    (per-CVR cash the holder could take instead), triggers = [{payment,
    probability, years, description?}] for several independent milestones.
    """
    def req(keys, label, **kw):
        k = _key(p, *keys)
        if not k:
            raise InputError(f"Missing required input: {label}")
        return num(p, k, label=label, **kw)

    r = num(p, 'discount_rate', label='discount_rate (decimal)', min=0, max=1)
    cvr_type = text(p, 'type', choices=('milestone', 'revenue', 'regulatory')) if has(p, 'type') else 'milestone'

    raw = p.get('triggers')
    if raw is not None:
        if not isinstance(raw, list) or not raw:
            raise InputError("triggers must be a non-empty list of {payment, probability, years}")
        specs = []
        for i, t in enumerate(raw):
            if not isinstance(t, dict):
                raise InputError(f"triggers[{i}] must be an object")
            specs.append({'description': str(t.get('description') or f'trigger_{i + 1}'),
                          'payment': num(t, 'payment', label=f'triggers[{i}].payment', min=0),
                          'probability': num(t, 'probability', label=f'triggers[{i}].probability', min=0, max=1),
                          'years': num(t, 'years', label=f'triggers[{i}].years', min=0)})
    else:
        specs = [{'description': cvr_type,
                  'payment': req(('payment', 'max_payout', 'cvr_payment', 'payment_if_triggered'),
                                 'payment (amount paid if triggered)', min=0),
                  'probability': req(('probability', 'approval_probability'), 'probability (decimal 0-1)',
                                     min=0, max=1),
                  'years': req(('years', 'expiry_years', 'time_to_milestone', 'expected_timing_years'),
                               'years (to the trigger)', min=0)}]

    rows = []
    for s in specs:
        pv = s['payment'] / (1 + r) ** s['years']
        rows.append({'trigger': s['description'], 'payment_if_triggered': s['payment'],
                     'probability_pct': pct(s['probability']), 'years': s['years'],
                     'pv_if_triggered': pv, 'expected_value': s['probability'] * pv})

    appeal_ev = None
    if has(p, 'appeal_probability'):
        if len(specs) != 1:
            raise InputError("appeal_probability applies to a single-trigger CVR")
        ap = num(p, 'appeal_probability', label='appeal_probability (decimal)', min=0, max=1)
        delay = num(p, 'appeal_delay_years', label='appeal_delay_years', min=0)
        s = specs[0]
        appeal_ev = (1 - s['probability']) * ap * s['payment'] / (1 + r) ** (s['years'] + delay)
        rows.append({'trigger': 'appeal_after_rejection', 'payment_if_triggered': s['payment'],
                     'probability_pct': pct((1 - s['probability']) * ap), 'years': s['years'] + delay,
                     'pv_if_triggered': s['payment'] / (1 + r) ** (s['years'] + delay),
                     'expected_value': appeal_ev})

    expected = sum(x['expected_value'] for x in rows)
    face = sum(s['payment'] for s in specs)
    out: Dict[str, Any] = {
        'cvr_type': cvr_type,
        'expected_value': expected,
        'max_payout': face,
        'pv_if_triggered': sum(x['pv_if_triggered'] for x in rows[:len(specs)]),
        'expected_value_share_of_max_pct': (expected / face * 100) if face else None,
        'discount_rate_pct': pct(r),
    }
    if len(specs) == 1:
        out['probability_pct'] = rows[0]['probability_pct']
        out['years'] = specs[0]['years']
    if len(rows) > 1:
        out['triggers'] = rows
    if appeal_ev is not None:
        out['appeal_expected_value'] = appeal_ev
    sk = _key(p, 'shares_outstanding', 'cvrs_outstanding')
    if sk:
        sh = num(p, sk, gt=0)
        out['expected_value_per_share'] = expected / sh
        out['max_payout_per_share'] = face / sh
    if has(p, 'cash_alternative'):
        cash = num(p, 'cash_alternative', min=0)
        per_unit = out.get('expected_value_per_share', expected)
        out['cash_alternative'] = cash
        out['cvr_minus_cash'] = per_unit - cash
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
