"""Scorecard Valuation Method"""
from pathlib import Path
from typing import Dict, Any, List
import sys

_ANALYTICS = str(Path(__file__).resolve().parent.parent.parent)
if _ANALYTICS not in sys.path:
    sys.path.insert(0, _ANALYTICS)

class ScorecardMethod:
    """Scorecard (or Payne) Method for startup valuation"""

    def __init__(self, region: str = 'US'):
        self.region = region

        self.typical_pre_money_valuations = {
            'US': {'seed': 2_000_000, 'series_a': 5_000_000},
            'Europe': {'seed': 1_500_000, 'series_a': 4_000_000},
            'Asia': {'seed': 1_800_000, 'series_a': 4_500_000}
        }

        self.factor_weights = {
            'management_team': 0.30,
            'size_of_opportunity': 0.25,
            'product_technology': 0.15,
            'competitive_environment': 0.10,
            'marketing_sales_channels': 0.10,
            'need_for_additional_investment': 0.05,
            'other_factors': 0.05
        }

    def get_baseline_valuation(self, stage: str = 'seed') -> float:
        """Get baseline valuation for region and stage"""

        region_valuations = self.typical_pre_money_valuations.get(self.region, self.typical_pre_money_valuations['US'])
        return region_valuations.get(stage, region_valuations['seed'])

    def assess_factor(self, factor_name: str, comparison_score: float) -> float:
        """
        Assess factor relative to average startup

        Args:
            factor_name: Name of factor being assessed
            comparison_score: -0.5 to +0.5 (0 = average, +0.5 = way above, -0.5 = way below)

        Returns:
            Adjustment multiplier (e.g., 1.3 for 30% above average)
        """

        if factor_name not in self.factor_weights:
            raise ValueError(f"Unknown factor: {factor_name}")

        weight = self.factor_weights[factor_name]

        adjustment = 1 + (comparison_score * weight / 0.30)

        return max(0.5, min(1.5, adjustment))

    def calculate_valuation(self, stage: str, factor_assessments: Dict[str, float],
                          custom_baseline: float = None) -> Dict[str, Any]:
        """
        Calculate valuation using scorecard method

        Args:
            stage: Funding stage ('seed', 'series_a')
            factor_assessments: Dict of factor_name -> comparison_score (-0.5 to +0.5)
            custom_baseline: Optional custom baseline instead of regional average

        Returns:
            Detailed valuation breakdown
        """

        baseline = custom_baseline or self.get_baseline_valuation(stage)

        factor_multipliers = {}
        cumulative_multiplier = 1.0

        for factor, comparison_score in factor_assessments.items():
            multiplier = self.assess_factor(factor, comparison_score)
            factor_multipliers[factor] = {
                'comparison_score': comparison_score,
                'weight': self.factor_weights[factor],
                'multiplier': multiplier
            }

            weight = self.factor_weights[factor]
            cumulative_multiplier *= (1 + (multiplier - 1) * weight / sum(self.factor_weights.values()))

        final_valuation = baseline * cumulative_multiplier

        return {
            'method': 'Scorecard Method',
            'baseline_valuation': baseline,
            'region': self.region,
            'stage': stage,
            'factor_assessments': factor_multipliers,
            'cumulative_multiplier': cumulative_multiplier,
            'final_valuation': final_valuation,
            'adjustment_pct': (cumulative_multiplier - 1) * 100
        }

    def comprehensive_assessment(self, stage: str, team_strength: str, market_size: str,
                                product_strength: str, competition: str,
                                sales_traction: str) -> Dict[str, Any]:
        """
        Comprehensive assessment with simplified inputs

        Args:
            All parameters: 'weak', 'average', 'strong', 'excellent'

        Returns:
            Valuation result
        """

        strength_to_score = {
            'weak': -0.4,
            'below_average': -0.2,
            'average': 0.0,
            'above_average': 0.2,
            'strong': 0.35,
            'excellent': 0.5
        }

        assessments = {
            'management_team': strength_to_score.get(team_strength, 0),
            'size_of_opportunity': strength_to_score.get(market_size, 0),
            'product_technology': strength_to_score.get(product_strength, 0),
            'competitive_environment': strength_to_score.get(competition, 0),
            'marketing_sales_channels': strength_to_score.get(sales_traction, 0),
            'need_for_additional_investment': 0.0,
            'other_factors': 0.0
        }

        return self.calculate_valuation(stage, assessments)

    def sensitivity_analysis(self, stage: str, base_assessments: Dict[str, float],
                           variable_factor: str) -> Dict[str, Any]:
        """Run sensitivity analysis on single factor"""

        results = []

        for score in [-0.5, -0.3, -0.1, 0, 0.1, 0.3, 0.5]:
            test_assessments = base_assessments.copy()
            test_assessments[variable_factor] = score

            valuation_result = self.calculate_valuation(stage, test_assessments)

            results.append({
                'factor_score': score,
                'valuation': valuation_result['final_valuation'],
                'multiplier': valuation_result['cumulative_multiplier']
            })

        return {
            'variable_factor': variable_factor,
            'sensitivity_data': results,
            'baseline_valuation': self.get_baseline_valuation(stage)
        }

# Payne scorecard: factor order and the method's standard weights. The weights
# are a published method convention (Bill Payne), not company data; a caller
# may override them with `weights`.
SCORECARD_FACTORS = ['management_team', 'size_of_opportunity', 'product_technology',
                     'competitive_environment', 'marketing_sales_channels',
                     'need_for_additional_investment', 'other_factors']
SCORECARD_LABELS = ['Management Team', 'Size of Opportunity', 'Product/Technology',
                    'Competitive Environment', 'Marketing/Sales Channels',
                    'Need for Additional Funding', 'Other']
PAYNE_WEIGHTS = [0.30, 0.25, 0.15, 0.10, 0.10, 0.05, 0.05]
_SCORECARD_ALIASES = {
    'management_team': ('management_team', 'team', 'team_score'),
    'size_of_opportunity': ('size_of_opportunity', 'market_size', 'opportunity_score'),
    'product_technology': ('product_technology', 'product', 'product_score'),
    'competitive_environment': ('competitive_environment', 'competition', 'competition_score'),
    'marketing_sales_channels': ('marketing_sales_channels', 'marketing', 'marketing_score'),
    'need_for_additional_investment': ('need_for_additional_investment', 'need_for_funding', 'funding_score'),
    'other_factors': ('other_factors', 'other', 'other_score'),
}


def scorecard_json(p: Dict[str, Any]) -> Dict[str, Any]:
    """JSON contract:
    {benchmark_pre_money, assessments: [7 ratios] | {factor: ratio}, weights?: [7], stage?}
    Each ratio compares the startup with the average comparable deal
    (1.0 = average, 1.25 = 125%). Valuation = benchmark x sum(w_i x ratio_i).
    """
    from corporateFinance._cli import InputError, num, pct
    bp = dict(p)
    if bp.get('benchmark_pre_money') is None and bp.get('median_pre_money') is not None:
        bp['benchmark_pre_money'] = bp['median_pre_money']
    benchmark = num(bp, 'benchmark_pre_money',
                    label='Benchmark pre-money (average of comparable deals, $)', gt=0)

    raw = p.get('assessments')
    if isinstance(raw, list):
        if len(raw) != 7:
            raise InputError(f"assessments must list 7 ratios ({', '.join(SCORECARD_LABELS)})")
        missing = [lab for lab, x in zip(SCORECARD_LABELS, raw) if x is None]
        if missing:
            raise InputError(f"Missing required input: scorecard ratio for {', '.join(missing)}")
        ratios = [float(x) for x in raw]
    else:
        src = raw if isinstance(raw, dict) else p
        ratios = []
        for key, label in zip(SCORECARD_FACTORS, SCORECARD_LABELS):
            v = next((src[a] for a in _SCORECARD_ALIASES[key] if src.get(a) is not None), None)
            if v is None:
                raise InputError(f"Missing required input: scorecard ratio for {label}")
            ratios.append(float(v))
    for label, r in zip(SCORECARD_LABELS, ratios):
        if not 0.0 <= r <= 3.0:
            raise InputError(f"{label} ratio must be between 0 and 3 (1.0 = average), got {r}")

    if p.get('weights') is not None:
        weights = [float(w) for w in p['weights']]
        if len(weights) != 7 or any(w < 0 for w in weights):
            raise InputError("weights must be 7 non-negative numbers")
        if abs(sum(weights) - 1.0) > 1e-6:
            raise InputError(f"weights must sum to 1 (got {sum(weights):.4f})")
        weights_source = 'user'
    else:
        weights = PAYNE_WEIGHTS
        weights_source = 'Payne scorecard standard weights (30/25/15/10/10/5/5)'

    rows = []
    total_factor = 0.0
    for label, w, r in zip(SCORECARD_LABELS, weights, ratios):
        f = w * r
        total_factor += f
        rows.append({'factor': label, 'weight_pct': pct(w), 'ratio_x': r, 'weighted_factor': f})
    return {
        'method': 'Scorecard Method',
        'stage': p.get('stage'),
        'benchmark_pre_money': benchmark,
        'sum_of_factors_x': total_factor,
        'pre_money_valuation': benchmark * total_factor,
        'adjustment_vs_benchmark_pct': pct(total_factor - 1.0),
        'weights_source': weights_source,
        'factors': rows,
    }


def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json({'calculate': scorecard_json})


if __name__ == '__main__':
    main()
