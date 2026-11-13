"""Berkus Method - Pre-Revenue Startup Valuation"""
import sys
from pathlib import Path
from typing import Dict, Any, List
from enum import Enum

_ANALYTICS = str(Path(__file__).resolve().parent.parent.parent)
if _ANALYTICS not in sys.path:
    sys.path.insert(0, _ANALYTICS)

class BerkusFactor(Enum):
    """Five key value factors in Berkus Method"""
    SOUND_IDEA = "sound_idea"
    PROTOTYPE = "prototype"
    QUALITY_MANAGEMENT = "quality_management"
    STRATEGIC_RELATIONSHIPS = "strategic_relationships"
    PRODUCT_ROLLOUT = "product_rollout"

class BerkusMethod:
    """Berkus Method for pre-revenue startup valuation"""

    def __init__(self, max_value_per_factor: float = 500_000):
        self.max_value = max_value_per_factor
        self.max_total_valuation = max_value_per_factor * 5

        self.factor_descriptions = {
            BerkusFactor.SOUND_IDEA: "Basic value, viable idea with significant market opportunity",
            BerkusFactor.PROTOTYPE: "Technology risk reduced through working prototype/product",
            BerkusFactor.QUALITY_MANAGEMENT: "Strong management team with relevant experience",
            BerkusFactor.STRATEGIC_RELATIONSHIPS: "Strategic alliances, partnerships, or customer commitments",
            BerkusFactor.PRODUCT_ROLLOUT: "Evidence of market acceptance, initial sales or validation"
        }

    def assess_factor(self, factor: BerkusFactor, score: float) -> float:
        """
        Assess single factor value

        Args:
            factor: BerkusFactor enum
            score: 0-1 score (0 = none, 0.5 = moderate, 1 = excellent)

        Returns:
            Dollar value for this factor
        """
        if not 0 <= score <= 1:
            raise ValueError("Score must be between 0 and 1")

        return self.max_value * score

    def calculate_valuation(self, factor_scores: Dict[BerkusFactor, float]) -> Dict[str, Any]:
        """
        Calculate total startup valuation using Berkus Method

        Args:
            factor_scores: Dict mapping BerkusFactor to score (0-1)

        Returns:
            Valuation breakdown and total
        """

        factor_values = {}
        total_valuation = 0

        for factor in BerkusFactor:
            score = factor_scores.get(factor, 0)
            value = self.assess_factor(factor, score)
            factor_values[factor.value] = {
                'score': score,
                'value': value,
                'max_value': self.max_value,
                'description': self.factor_descriptions[factor]
            }
            total_valuation += value

        return {
            'method': 'Berkus Method',
            'factor_assessments': factor_values,
            'total_valuation': total_valuation,
            'max_possible_valuation': self.max_total_valuation,
            'valuation_percentage': (total_valuation / self.max_total_valuation * 100)
        }

    def quick_assessment(self, idea_score: float, prototype_score: float,
                        team_score: float, relationships_score: float,
                        rollout_score: float) -> Dict[str, Any]:
        """Quick valuation with direct scores"""

        factor_scores = {
            BerkusFactor.SOUND_IDEA: idea_score,
            BerkusFactor.PROTOTYPE: prototype_score,
            BerkusFactor.QUALITY_MANAGEMENT: team_score,
            BerkusFactor.STRATEGIC_RELATIONSHIPS: relationships_score,
            BerkusFactor.PRODUCT_ROLLOUT: rollout_score
        }

        return self.calculate_valuation(factor_scores)

    def guided_assessment(self) -> Dict[str, Any]:
        """Interactive guided assessment with questions"""

        assessment_questions = {
            BerkusFactor.SOUND_IDEA: [
                "Is the market opportunity large (>$1B)?",
                "Is the problem clearly defined and significant?",
                "Is the solution innovative and defensible?"
            ],
            BerkusFactor.PROTOTYPE: [
                "Does a working prototype exist?",
                "Has the technology been validated?",
                "Are key technical risks mitigated?"
            ],
            BerkusFactor.QUALITY_MANAGEMENT: [
                "Does the team have relevant industry experience?",
                "Have founders successfully built companies before?",
                "Is the team complete with necessary skills?"
            ],
            BerkusFactor.STRATEGIC_RELATIONSHIPS: [
                "Are there signed LOIs or partnerships?",
                "Does the startup have strategic investors?",
                "Are there channel partners committed?"
            ],
            BerkusFactor.PRODUCT_ROLLOUT: [
                "Is there evidence of product-market fit?",
                "Are there paying customers or users?",
                "Is there positive market feedback?"
            ]
        }

        return {
            'method': 'Berkus Method - Guided Assessment',
            'assessment_framework': assessment_questions,
            'scoring_guide': {
                '0.0-0.2': 'None/Minimal',
                '0.2-0.4': 'Weak',
                '0.4-0.6': 'Moderate',
                '0.6-0.8': 'Strong',
                '0.8-1.0': 'Excellent'
            }
        }

BERKUS_KEYS = ['sound_idea', 'prototype', 'quality_team', 'strategic_relationships', 'product_rollout']
BERKUS_LABELS = ['Sound Idea', 'Prototype', 'Quality Team', 'Strategic Relationships', 'Product Rollout']
_BERKUS_ALIASES = {
    'sound_idea': ('sound_idea', 'idea_score'),
    'prototype': ('prototype', 'prototype_score'),
    'quality_team': ('quality_team', 'quality_management', 'team_score'),
    'strategic_relationships': ('strategic_relationships', 'relationships_score'),
    'product_rollout': ('product_rollout', 'rollout_score'),
}


def berkus_scores_from_params(p: Dict[str, Any]) -> List[float]:
    """Five factor scores in [0, 1], from `scores` (ordered list) or named keys.
    Every factor is required -- a missing factor is not silently 'average'."""
    from corporateFinance._cli import InputError
    raw = p.get('scores')
    if isinstance(raw, list):
        if len(raw) != 5:
            raise InputError(f"scores must list 5 factor scores ({', '.join(BERKUS_LABELS)})")
        vals = raw
    else:
        src = raw if isinstance(raw, dict) else p
        vals = []
        for key in BERKUS_KEYS:
            v = next((src[a] for a in _BERKUS_ALIASES[key] if src.get(a) is not None), None)
            if v is None:
                raise InputError(f"Missing required input: Berkus score for {key}")
            vals.append(v)
    out = []
    for label, v in zip(BERKUS_LABELS, vals):
        if v is None:
            raise InputError(f"Missing required input: Berkus score for {label}")
        f = float(v)
        if not 0.0 <= f <= 1.0:
            raise InputError(f"{label} score must be between 0 and 1 (got {f})")
        out.append(f)
    return out


def berkus_json(p: Dict[str, Any]) -> Dict[str, Any]:
    """JSON contract: {scores: [5 x 0-1] | {sound_idea,...}, max_value_per_factor}"""
    from corporateFinance._cli import num, pct
    scores = berkus_scores_from_params(p)
    max_value = num(p, 'max_value_per_factor', label='Max value per factor ($)', gt=0)
    factors = [{'factor': label, 'score_pct': pct(s), 'value': s * max_value, 'max_value': max_value}
               for label, s in zip(BERKUS_LABELS, scores)]
    total = sum(f['value'] for f in factors)
    max_total = max_value * 5
    return {
        'method': 'Berkus Method',
        'pre_money_valuation': total,
        'max_possible_valuation': max_total,
        'share_of_max_pct': pct(total / max_total),
        'max_value_per_factor': max_value,
        'factors': factors,
    }


def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json({'calculate': berkus_json})


if __name__ == '__main__':
    main()
