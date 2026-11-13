"""Comprehensive Startup Valuation Summary"""
import sys
from pathlib import Path
from typing import Dict, Any, List, Optional

# Add Analytics path for absolute imports
analytics_path = Path(__file__).parent.parent.parent
sys.path.insert(0, str(analytics_path))

# Use absolute imports instead of relative imports
from corporateFinance.startup_valuation.berkus_method import BerkusMethod, BerkusFactor
from corporateFinance.startup_valuation.scorecard_method import ScorecardMethod
from corporateFinance.startup_valuation.vc_method import VCMethod
from corporateFinance.startup_valuation.first_chicago_method import FirstChicagoMethod, Scenario
from corporateFinance.startup_valuation.risk_factor_summation import RiskFactorSummation, RiskFactor


def _coerce_berkus_scores(raw: dict) -> dict:
    """Convert JSON string-keyed dict to BerkusFactor enum-keyed dict."""
    key_map = {f.value: f for f in BerkusFactor}
    return {key_map[k]: float(v) for k, v in raw.items() if k in key_map}


def _coerce_first_chicago_scenarios(raw: list) -> list:
    """Convert list of dicts to list of Scenario dataclass objects."""
    return [
        Scenario(
            name=s.get('name', 'Scenario'),
            probability=float(s.get('probability', 0.33)),
            exit_year=int(s.get('exit_year', 5)),
            exit_value=float(s.get('exit_value', 0)),
            description=s.get('description', ''),
        )
        for s in raw
    ]


def _coerce_risk_factor_assessments(raw: dict) -> dict:
    """Convert JSON string-keyed dict to RiskFactor enum-keyed dict."""
    key_map = {f.value: f for f in RiskFactor}
    return {key_map[k]: int(v) for k, v in raw.items() if k in key_map}


class StartupValuationSummary:
    """Aggregate all startup valuation methods into comprehensive summary"""

    def __init__(self, startup_name: str):
        self.startup_name = startup_name

    def comprehensive_valuation(self,
                               berkus_scores: Optional[Dict[BerkusFactor, float]] = None,
                               scorecard_inputs: Optional[Dict[str, Any]] = None,
                               vc_inputs: Optional[Dict[str, Any]] = None,
                               first_chicago_scenarios: Optional[List[Scenario]] = None,
                               risk_factor_assessments: Optional[Dict[RiskFactor, int]] = None) -> Dict[str, Any]:
        """
        Generate comprehensive startup valuation using all applicable methods

        Returns summary with all methods, weighted average, and range
        """

        valuations = {}

        # Berkus Method (pre-revenue)
        if berkus_scores:
            berkus = BerkusMethod()
            # Coerce string keys to BerkusFactor enums if needed
            if berkus_scores and not isinstance(next(iter(berkus_scores), None), BerkusFactor):
                berkus_scores = _coerce_berkus_scores(berkus_scores)
            if berkus_scores:  # Re-check after coercion (may have filtered out invalid keys)
                berkus_result = berkus.calculate_valuation(berkus_scores)
                valuations['berkus'] = {
                    'method': 'Berkus Method',
                    'valuation': berkus_result['total_valuation'],
                    'applicability': 'pre_revenue',
                    'details': berkus_result
                }

        # Scorecard Method
        if scorecard_inputs:
            raw_region = scorecard_inputs.get('region', 'US')
            region = raw_region.capitalize() if raw_region.lower() in ('us', 'europe', 'asia') else 'US'
            scorecard = ScorecardMethod(region=region)
            assessments = scorecard_inputs.get('assessments', {})
            # Full key map: frontend short names → internal factor names
            key_map = {
                'team': 'management_team', 'management_team': 'management_team',
                'quality_team': 'management_team', 'team_strength': 'management_team',
                'market_size': 'size_of_opportunity', 'market_opportunity': 'size_of_opportunity',
                'size_of_opportunity': 'size_of_opportunity',
                'product': 'product_technology', 'product_technology': 'product_technology',
                'competitive': 'competitive_environment', 'competitive_environment': 'competitive_environment',
                'competition': 'competitive_environment',
                'marketing': 'marketing_sales_channels', 'marketing_channels': 'marketing_sales_channels',
                'sales_channels': 'marketing_sales_channels', 'marketing_sales_channels': 'marketing_sales_channels',
                'need_for_funding': 'need_for_additional_investment',
                'need_for_investment': 'need_for_additional_investment',
                'need_for_additional_investment': 'need_for_additional_investment',
                'other': 'other_factors', 'other_factors': 'other_factors',
            }
            mapped_assessments = {}
            for k, v in assessments.items():
                internal_key = key_map.get(k)
                if internal_key:
                    val = float(v)
                    # Frontend sends multipliers (0.5-1.5) → convert to comparison scores (-0.5 to +0.5)
                    if val > 1.1 or val < 0.4:
                        mapped_assessments[internal_key] = max(-0.5, min(0.5, val - 1.0))
                    else:
                        mapped_assessments[internal_key] = val
            # Fill missing factors with 0 (average)
            for factor in scorecard.factor_weights:
                if factor not in mapped_assessments:
                    mapped_assessments[factor] = 0.0
            scorecard_result = scorecard.calculate_valuation(
                stage=scorecard_inputs.get('stage', 'seed'),
                factor_assessments=mapped_assessments
            )
            valuations['scorecard'] = {
                'method': 'Scorecard Method',
                'valuation': scorecard_result['final_valuation'],
                'applicability': 'early_stage',
                'details': scorecard_result
            }

        # VC Method
        if vc_inputs:
            vc = VCMethod()
            vc_result = vc.comprehensive_valuation(**vc_inputs)
            valuations['vc_method'] = {
                'method': 'VC Method',
                'valuation': vc_result['pre_money_valuation'],
                'applicability': 'all_stages',
                'details': vc_result
            }

        # First Chicago Method
        if first_chicago_scenarios:
            fc = FirstChicagoMethod()
            # Coerce dicts to Scenario objects before calling calculate_expected_value
            if first_chicago_scenarios and not isinstance(first_chicago_scenarios[0], Scenario):
                first_chicago_scenarios = _coerce_first_chicago_scenarios(first_chicago_scenarios)
            if first_chicago_scenarios:
                fc_result = fc.calculate_expected_value(first_chicago_scenarios)
                valuations['first_chicago'] = {
                    'method': 'First Chicago Method',
                    'valuation': fc_result['expected_present_value'],
                    'applicability': 'all_stages',
                    'details': fc_result
                }

        # Risk Factor Summation
        if risk_factor_assessments:
            rfs = RiskFactorSummation(base_valuation=2_000_000)
            # Coerce string keys to RiskFactor enums before calling calculate_valuation
            if risk_factor_assessments and not isinstance(next(iter(risk_factor_assessments), None), RiskFactor):
                risk_factor_assessments = _coerce_risk_factor_assessments(risk_factor_assessments)
            if risk_factor_assessments:
                rfs_result = rfs.calculate_valuation(risk_factor_assessments)
                valuations['risk_factor'] = {
                    'method': 'Risk Factor Summation',
                    'valuation': rfs_result['final_valuation'],
                    'applicability': 'early_stage',
                    'details': rfs_result
                }

        if not valuations:
            return {'error': 'No valuation methods provided'}

        # Calculate statistics
        valuation_values = [v['valuation'] for v in valuations.values()]

        min_val = min(valuation_values)
        max_val = max(valuation_values)
        mean_val = sum(valuation_values) / len(valuation_values)
        median_val = sorted(valuation_values)[len(valuation_values) // 2]

        # Weighted average (give more weight to methods with higher applicability)
        weights = {
            'berkus': 0.15,
            'scorecard': 0.20,
            'vc_method': 0.30,
            'first_chicago': 0.25,
            'risk_factor': 0.10
        }

        total_weight = sum(weights.get(k, 0.2) for k in valuations.keys())
        weighted_valuation = sum(
            v['valuation'] * weights.get(k, 0.2)
            for k, v in valuations.items()
        ) / total_weight

        return {
            'startup_name': self.startup_name,
            'valuations_by_method': valuations,
            'valuation_summary': {
                'min': min_val,
                'max': max_val,
                'mean': mean_val,
                'median': median_val,
                'weighted_average': weighted_valuation,
                'range': max_val - min_val,
                'coefficient_of_variation': (
                    (max_val - min_val) / mean_val * 100
                ) if mean_val > 0 else 0
            },
            'methods_used': list(valuations.keys()),
            'num_methods': len(valuations),
            'recommendation': {
                'suggested_valuation': weighted_valuation,
                'valuation_range': f"${min_val:,.0f} - ${max_val:,.0f}",
                'confidence': 'high' if len(valuations) >= 3 else 'moderate'
            }
        }

    def quick_pre_revenue_valuation(self,
                                   idea_quality: int,
                                   team_quality: int,
                                   prototype_status: int,
                                   market_size: int) -> Dict[str, Any]:
        """
        Quick pre-revenue valuation using Berkus and Risk Factor

        All inputs: 0-100 scale
        """

        # Convert to Berkus scores (0-1)
        berkus_scores = {
            BerkusFactor.SOUND_IDEA: idea_quality / 100,
            BerkusFactor.QUALITY_MANAGEMENT: team_quality / 100,
            BerkusFactor.PROTOTYPE: prototype_status / 100,
            BerkusFactor.STRATEGIC_RELATIONSHIPS: 0.3,
            BerkusFactor.PRODUCT_ROLLOUT: 0.2
        }

        # Convert to risk factor scores (-2 to +2)
        def scale_to_risk(score: int) -> int:
            if score >= 80:
                return 2
            elif score >= 60:
                return 1
            elif score >= 40:
                return 0
            elif score >= 20:
                return -1
            else:
                return -2

        risk_assessments = {
            RiskFactor.MANAGEMENT: scale_to_risk(team_quality),
            RiskFactor.STAGE_OF_BUSINESS: -1,
            RiskFactor.TECHNOLOGY: scale_to_risk(prototype_status),
            RiskFactor.COMPETITION: 0,
            RiskFactor.SALES_MARKETING: scale_to_risk(market_size),
            RiskFactor.FUNDING_CAPITAL: -1
        }

        return self.comprehensive_valuation(
            berkus_scores=berkus_scores,
            risk_factor_assessments=risk_assessments
        )

    def series_a_valuation(self,
                          revenue: float,
                          revenue_growth: float,
                          exit_year: int = 5,
                          exit_multiple: float = 8.0,
                          investment_amount: float = 10_000_000) -> Dict[str, Any]:
        """Series A stage valuation using VC and First Chicago methods"""

        # VC Method inputs
        vc_inputs = {
            'exit_year_metric': revenue * (1 + revenue_growth) ** exit_year,
            'exit_multiple': exit_multiple,
            'years_to_exit': exit_year,
            'investment_amount': investment_amount,
            'stage': 'series_a'
        }

        # First Chicago scenarios
        exit_revenue = revenue * (1 + revenue_growth) ** exit_year
        scenarios = [
            Scenario('Bear Case', 0.25, exit_year + 1, exit_revenue * 0.6 * 6.0, 'Conservative exit'),
            Scenario('Base Case', 0.50, exit_year, exit_revenue * exit_multiple, 'Expected outcome'),
            Scenario('Bull Case', 0.25, exit_year - 1, exit_revenue * 1.4 * 10.0, 'Strong exit')
        ]

        return self.comprehensive_valuation(
            vc_inputs=vc_inputs,
            first_chicago_scenarios=scenarios
        )

def comprehensive_json(p: Dict[str, Any]) -> Dict[str, Any]:
    """JSON contract: any subset (>= 1) of
    {berkus: {...}, scorecard: {...}, vc: {...}, first_chicago: {...}, risk_factor: {...}}
    where each object is exactly the payload of that method's own `calculate`.
    Returns each method's value plus the range / mean / median across methods.
    No method weighting is invented: a weighted value is reported only when the
    caller supplies `weights` ({method: weight}).
    """
    from corporateFinance._cli import InputError
    from corporateFinance.startup_valuation.berkus_method import berkus_json
    from corporateFinance.startup_valuation.scorecard_method import scorecard_json
    from corporateFinance.startup_valuation.vc_method import vc_json
    from corporateFinance.startup_valuation.first_chicago_method import first_chicago_json
    from corporateFinance.startup_valuation.risk_factor_summation import risk_factor_json

    runners = [
        ('berkus', 'Berkus', berkus_json, 'pre_money_valuation'),
        ('scorecard', 'Scorecard', scorecard_json, 'pre_money_valuation'),
        ('vc', 'VC Method', vc_json, 'pre_money_valuation'),
        ('first_chicago', 'First Chicago', first_chicago_json, 'valuation'),
        ('risk_factor', 'Risk Factor Summation', risk_factor_json, 'pre_money_valuation'),
    ]
    rows, errors, details = [], [], {}
    for key, label, fn, value_key in runners:
        sub = p.get(key)
        if sub is None:
            continue
        if not isinstance(sub, dict):
            raise InputError(f"{key} must be an object")
        try:
            res = fn(sub)
        except Exception as e:  # one method's bad input should not hide the others
            errors.append({'method': label, 'error': str(e)})
            continue
        details[key] = res
        rows.append({'method': label, 'valuation': res.get(value_key)})
    if not rows and not errors:
        raise InputError("Provide inputs for at least one method: berkus, scorecard, vc, first_chicago, risk_factor")
    vals = sorted(r['valuation'] for r in rows if r['valuation'] is not None)
    if not vals:
        raise InputError("; ".join(f"{e['method']}: {e['error']}" for e in errors) or "No method produced a value")
    n = len(vals)
    median = vals[n // 2] if n % 2 else (vals[n // 2 - 1] + vals[n // 2]) / 2
    weighted = None
    if isinstance(p.get('weights'), dict):
        keyed = {k: details[k] for k in details}
        num_, den = 0.0, 0.0
        for key, label, fn, value_key in runners:
            w = p['weights'].get(key)
            if w is None or key not in keyed or keyed[key].get(value_key) is None:
                continue
            num_ += float(w) * keyed[key][value_key]
            den += float(w)
        weighted = num_ / den if den > 0 else None
    return {
        'methods_used': n,
        'low': vals[0],
        'high': vals[-1],
        'mean': sum(vals) / n,
        'median': median,
        'weighted_valuation': weighted,
        'valuations': rows,
        'errors': errors,
        'detail': details,
    }


def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json({'comprehensive': comprehensive_json})


if __name__ == '__main__':
    main()
