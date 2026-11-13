"""M&A Process Quality Assessment"""
import sys
from pathlib import Path
from typing import Dict, Any, List, Optional
from enum import Enum
from dataclasses import dataclass

_ANALYTICS = str(Path(__file__).resolve().parent.parent.parent)
if _ANALYTICS not in sys.path:
    sys.path.insert(0, _ANALYTICS)

class ProcessType(Enum):
    BROAD_AUCTION = "broad_auction"
    TARGETED_AUCTION = "targeted_auction"
    NEGOTIATED_SALE = "negotiated_sale"
    GO_SHOP = "go_shop"
    UNSOLICITED = "unsolicited"

class BidderType(Enum):
    STRATEGIC = "strategic"
    FINANCIAL_SPONSOR = "financial_sponsor"
    MANAGEMENT = "management"

@dataclass
class Bidder:
    name: str
    bidder_type: BidderType
    initial_indication: Optional[float]
    final_bid: Optional[float]
    selected: bool

class ProcessQualityAssessment:
    """Assess quality and fairness of M&A sale process"""

    def __init__(self, process_type: ProcessType):
        self.process_type = process_type

    def evaluate_market_check(self, parties_contacted: int,
                              ndas_executed: int,
                              management_presentations: int,
                              bids_received: int,
                              go_shop_period_days: Optional[int] = None) -> Dict[str, Any]:
        """Evaluate thoroughness of market check"""

        if self.process_type == ProcessType.BROAD_AUCTION:
            min_expected_contacts = 30
            min_expected_bids = 5
        elif self.process_type == ProcessType.TARGETED_AUCTION:
            min_expected_contacts = 15
            min_expected_bids = 3
        elif self.process_type == ProcessType.GO_SHOP:
            min_expected_contacts = 20
            min_expected_bids = 2
        else:
            min_expected_contacts = 5
            min_expected_bids = 1

        contact_rate = (ndas_executed / parties_contacted * 100) if parties_contacted > 0 else 0
        presentation_rate = (management_presentations / ndas_executed * 100) if ndas_executed > 0 else 0
        bid_rate = (bids_received / management_presentations * 100) if management_presentations > 0 else 0

        process_adequate = (
            parties_contacted >= min_expected_contacts and
            bids_received >= min_expected_bids
        )

        quality_score = 0
        if parties_contacted >= min_expected_contacts:
            quality_score += 25
        if ndas_executed >= min_expected_contacts * 0.5:
            quality_score += 25
        if management_presentations >= min_expected_contacts * 0.3:
            quality_score += 25
        if bids_received >= min_expected_bids:
            quality_score += 25

        return {
            'process_type': self.process_type.value,
            'parties_contacted': parties_contacted,
            'ndas_executed': ndas_executed,
            'management_presentations': management_presentations,
            'bids_received': bids_received,
            'conversion_rates': {
                'contact_to_nda': contact_rate,
                'nda_to_presentation': presentation_rate,
                'presentation_to_bid': bid_rate
            },
            'process_adequate': process_adequate,
            'quality_score': quality_score,
            'go_shop_period_days': go_shop_period_days,
            'go_shop_adequate': go_shop_period_days >= 30 if go_shop_period_days else None
        }

    def analyze_competitive_tension(self, bidders: List[Bidder]) -> Dict[str, Any]:
        """Analyze competitive dynamics among bidders"""

        if not bidders:
            return {'error': 'No bidders provided'}

        strategic_bidders = [b for b in bidders if b.bidder_type == BidderType.STRATEGIC]
        financial_bidders = [b for b in bidders if b.bidder_type == BidderType.FINANCIAL_SPONSOR]

        bids_with_finals = [b for b in bidders if b.final_bid is not None]

        if len(bids_with_finals) >= 2:
            final_bids = sorted([b.final_bid for b in bids_with_finals], reverse=True)
            winning_bid = final_bids[0]
            second_bid = final_bids[1]

            bid_increment = winning_bid - second_bid
            bid_increment_pct = (bid_increment / second_bid * 100) if second_bid > 0 else 0

            competitive_tension = "high" if bid_increment_pct < 5 else "moderate" if bid_increment_pct < 10 else "low"
        else:
            bid_increment = None
            bid_increment_pct = None
            competitive_tension = "none"

        bidders_improved = sum(1 for b in bidders if b.initial_indication and b.final_bid and b.final_bid > b.initial_indication)
        improvement_rate = (bidders_improved / len(bids_with_finals) * 100) if bids_with_finals else 0

        return {
            'total_bidders': len(bidders),
            'strategic_bidders': len(strategic_bidders),
            'financial_bidders': len(financial_bidders),
            'bidders_with_final_bids': len(bids_with_finals),
            'competitive_tension': competitive_tension,
            'bid_increment_pct': bid_increment_pct,
            'bidders_improved_offers': bidders_improved,
            'improvement_rate': improvement_rate,
            'multiple_rounds': improvement_rate > 0
        }

    def evaluate_board_process(self, board_meetings: int,
                               special_committee: bool,
                               independent_financial_advisor: bool,
                               independent_legal_counsel: bool,
                               management_conflicts: bool,
                               fairness_opinion_obtained: bool) -> Dict[str, Any]:
        """Evaluate board's discharge of fiduciary duties"""

        governance_score = 0
        max_score = 100

        if board_meetings >= 3:
            governance_score += 15
        elif board_meetings >= 1:
            governance_score += 10

        if special_committee:
            governance_score += 25

        if independent_financial_advisor:
            governance_score += 20

        if independent_legal_counsel:
            governance_score += 15

        if fairness_opinion_obtained:
            governance_score += 25

        if management_conflicts and not special_committee:
            governance_score -= 30

        governance_rating = "excellent" if governance_score >= 85 else \
                           "good" if governance_score >= 70 else \
                           "adequate" if governance_score >= 50 else "deficient"

        best_practices_met = special_committee and independent_financial_advisor and fairness_opinion_obtained

        return {
            'board_meetings_held': board_meetings,
            'special_committee_formed': special_committee,
            'independent_financial_advisor': independent_financial_advisor,
            'independent_legal_counsel': independent_legal_counsel,
            'management_conflicts_present': management_conflicts,
            'fairness_opinion_obtained': fairness_opinion_obtained,
            'governance_score': governance_score,
            'governance_rating': governance_rating,
            'best_practices_met': best_practices_met,
            'process_defensible': governance_score >= 70
        }

    def timing_pressure_analysis(self, process_duration_days: int,
                                 termination_fee_pct: float,
                                 go_shop_allowed: bool,
                                 no_shop_period: bool,
                                 financing_contingency: bool) -> Dict[str, Any]:
        """Analyze timing pressures and deal protections"""

        adequate_duration = process_duration_days >= 60

        termination_fee_reasonable = 2.0 <= termination_fee_pct <= 4.0

        timing_pressure_score = 100

        if process_duration_days < 30:
            timing_pressure_score -= 40
        elif process_duration_days < 60:
            timing_pressure_score -= 20

        if termination_fee_pct > 4.0:
            timing_pressure_score -= 15

        if no_shop_period and not go_shop_allowed:
            timing_pressure_score -= 25

        pressure_level = "low" if timing_pressure_score >= 75 else \
                        "moderate" if timing_pressure_score >= 50 else "high"

        return {
            'process_duration_days': process_duration_days,
            'adequate_duration': adequate_duration,
            'termination_fee_pct': termination_fee_pct,
            'termination_fee_reasonable': termination_fee_reasonable,
            'go_shop_allowed': go_shop_allowed,
            'no_shop_period': no_shop_period,
            'financing_contingency': financing_contingency,
            'timing_pressure_score': timing_pressure_score,
            'pressure_level': pressure_level,
            'deal_protections_reasonable': termination_fee_reasonable and (go_shop_allowed or not no_shop_period)
        }

    def alternative_transactions_considered(self, alternatives: List[Dict[str, Any]]) -> Dict[str, Any]:
        """Evaluate alternatives to sale transaction"""

        alternative_types = {}

        for alt in alternatives:
            alt_type = alt['type']
            if alt_type not in alternative_types:
                alternative_types[alt_type] = []
            alternative_types[alt_type].append(alt)

        strategic_alternatives = [
            'remain_independent',
            'strategic_acquisition',
            'asset_sale',
            'spin_off',
            'recapitalization',
            'liquidation'
        ]

        alternatives_considered = set(alt['type'] for alt in alternatives)
        comprehensive = len(alternatives_considered) >= 3

        return {
            'alternatives_evaluated': len(alternatives),
            'alternative_types': list(alternatives_considered),
            'alternatives_by_type': alternative_types,
            'comprehensive_analysis': comprehensive,
            'remain_independent_considered': 'remain_independent' in alternatives_considered,
            'strategic_alternatives_considered': len(alternatives_considered.intersection(strategic_alternatives))
        }

    def shareholder_approval_analysis(self, approval_required: bool,
                                     majority_threshold: float,
                                     unaffiliated_shareholder_vote: bool,
                                     anticipated_approval_pct: Optional[float] = None) -> Dict[str, Any]:
        """Analyze shareholder approval requirements"""

        if not approval_required:
            return {
                'approval_required': False,
                'reason': 'Statutory exemption or other exception'
            }

        vote_protection = "strong" if unaffiliated_shareholder_vote else "standard"

        expected_approval = None
        if anticipated_approval_pct:
            expected_approval = anticipated_approval_pct >= majority_threshold

        return {
            'approval_required': approval_required,
            'majority_threshold_pct': majority_threshold,
            'unaffiliated_shareholder_vote': unaffiliated_shareholder_vote,
            'anticipated_approval_pct': anticipated_approval_pct,
            'expected_to_pass': expected_approval,
            'vote_protection_level': vote_protection
        }

    def comprehensive_process_evaluation(self, market_check: Dict[str, Any],
                                        competitive_tension: Dict[str, Any],
                                        board_process: Dict[str, Any],
                                        timing_analysis: Dict[str, Any]) -> Dict[str, Any]:
        """Comprehensive evaluation of entire M&A process"""

        components = {
            'market_check': market_check.get('quality_score', 0) * 0.30,
            'competitive_tension': (100 if competitive_tension.get('competitive_tension') == 'high' else
                                   70 if competitive_tension.get('competitive_tension') == 'moderate' else
                                   40 if competitive_tension.get('competitive_tension') == 'low' else 0) * 0.25,
            'board_governance': board_process.get('governance_score', 0) * 0.30,
            'timing_process': timing_analysis.get('timing_pressure_score', 0) * 0.15
        }

        overall_score = sum(components.values())

        process_quality = "excellent" if overall_score >= 85 else \
                         "good" if overall_score >= 70 else \
                         "adequate" if overall_score >= 50 else "deficient"

        supports_fairness = overall_score >= 60

        return {
            'overall_process_score': overall_score,
            'process_quality_rating': process_quality,
            'component_scores': components,
            'supports_fairness_conclusion': supports_fairness,
            'key_strengths': self._identify_strengths(market_check, competitive_tension, board_process, timing_analysis),
            'key_weaknesses': self._identify_weaknesses(market_check, competitive_tension, board_process, timing_analysis)
        }

    def _identify_strengths(self, market_check, competitive_tension, board_process, timing_analysis) -> List[str]:
        strengths = []

        if market_check.get('quality_score', 0) >= 75:
            strengths.append("Thorough market check with broad outreach")

        if competitive_tension.get('competitive_tension') in ['high', 'moderate']:
            strengths.append("Competitive bidding process")

        if board_process.get('special_committee_formed'):
            strengths.append("Independent special committee formed")

        if board_process.get('governance_score', 0) >= 80:
            strengths.append("Strong governance and board process")

        if timing_analysis.get('go_shop_allowed'):
            strengths.append("Go-shop period allowed")

        return strengths

    def _identify_weaknesses(self, market_check, competitive_tension, board_process, timing_analysis) -> List[str]:
        weaknesses = []

        if market_check.get('quality_score', 0) < 50:
            weaknesses.append("Limited market check")

        if competitive_tension.get('competitive_tension') == 'none':
            weaknesses.append("No competitive tension")

        if not board_process.get('special_committee_formed') and board_process.get('management_conflicts_present'):
            weaknesses.append("Management conflicts without special committee")

        if timing_analysis.get('timing_pressure_score', 0) < 50:
            weaknesses.append("Significant timing pressure")

        if timing_analysis.get('termination_fee_pct', 0) > 4.0:
            weaknesses.append("High termination fee")

        return weaknesses

PQ_FACTORS = ['Board Independence', 'Special Committee', 'Independent Advisor', 'Market Check',
              'Negotiation Process', 'Due Diligence', 'Disclosure Quality', 'Timing Adequacy']
_PQ_KEYS = ['board_independence', 'special_committee', 'independent_advisor', 'market_check',
            'negotiation_process', 'due_diligence', 'disclosure_quality', 'timing_adequacy']
# Rating bands on the 1-5 average: a labelled scale convention, not data.
_PQ_BANDS = [(4.5, 'excellent'), (3.5, 'good'), (2.5, 'adequate'), (0.0, 'deficient')]


def _pq_scored(p: Dict[str, Any]) -> Dict[str, Any]:
    from corporateFinance._cli import InputError
    raw = p['factors']
    if isinstance(raw, dict):
        vals = []
        for k, label in zip(_PQ_KEYS, PQ_FACTORS):
            if raw.get(k) is None:
                raise InputError(f"Missing required input: score for {label}")
            vals.append(raw[k])
    elif isinstance(raw, list) and len(raw) == len(PQ_FACTORS):
        vals = raw
    else:
        raise InputError(f"factors must be {len(PQ_FACTORS)} scores (1-5): {', '.join(PQ_FACTORS)}")
    scores = []
    for label, v in zip(PQ_FACTORS, vals):
        f = float(v)
        if not 1 <= f <= 5:
            raise InputError(f"{label} score must be between 1 and 5 (got {v})")
        scores.append(f)
    avg = sum(scores) / len(scores)
    rating = next(name for floor, name in _PQ_BANDS if avg >= floor)
    low = min(scores)
    return {
        'mode': 'scored (user 1-5 ratings)',
        'average_score': avg,
        'rating': rating,
        'rating_scale': 'average >= 4.5 excellent, >= 3.5 good, >= 2.5 adequate, else deficient',
        'weakest_factors': ', '.join(l for l, s_ in zip(PQ_FACTORS, scores) if s_ == low),
        'factors_rated_2_or_below': sum(1 for s_ in scores if s_ <= 2),
        'factors': [{'factor': l, 'score': s_} for l, s_ in zip(PQ_FACTORS, scores)],
    }


def _pq_factual(p: Dict[str, Any]) -> Dict[str, Any]:
    from corporateFinance._cli import InputError, has, num
    contacted = num(p, 'num_bidders_contacted', label='Parties contacted', min=0, integer=True)
    if not isinstance(p.get('market_check_conducted'), bool):
        raise InputError("Missing required input: market_check_conducted (true/false)")
    checks = [{'criterion': 'Market check conducted', 'met': p['market_check_conducted'],
               'detail': f"{contacted} parties contacted"}]
    if has(p, 'num_bidders_participated'):
        bids = num(p, 'num_bidders_participated', label='Bids received', min=0, integer=True)
        checks.append({'criterion': 'Competing bids (2 or more)', 'met': bids >= 2,
                       'detail': f"{bids} bids received"})
    if has(p, 'go_shop_period'):
        days = num(p, 'go_shop_period', label='Go-shop period (days)', min=0)
        checks.append({'criterion': 'Post-signing go-shop window', 'met': days > 0,
                       'detail': f"{days:g} days" if days else 'none'})
    for key, label in [('independent_committee', 'Independent special committee'),
                       ('financial_advisor_engaged', 'Independent financial advisor')]:
        if isinstance(p.get(key), bool):
            checks.append({'criterion': label, 'met': p[key], 'detail': None})
    met = sum(1 for c in checks if c['met'])
    return {
        'mode': 'factual checklist',
        'parties_contacted': contacted,
        'criteria_met': met,
        'criteria_assessed': len(checks),
        'gaps': ', '.join(c['criterion'] for c in checks if not c['met']) or None,
        'checklist': checks,
    }


def process_quality_json(p: Dict[str, Any]) -> Dict[str, Any]:
    """JSON contract (either form):
    scored : {factors: [8 x 1-5] | {board_independence, ...}}
    factual: {num_bidders_contacted, market_check_conducted, num_bidders_participated?,
              go_shop_period?, independent_committee?, financial_advisor_engaged?}
    """
    if p.get('factors') is not None:
        return _pq_scored(p)
    return _pq_factual(p)


def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json({'assess': process_quality_json})


if __name__ == '__main__':
    main()
