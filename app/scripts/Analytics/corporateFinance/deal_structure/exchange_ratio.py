"""Exchange Ratio Calculations for Stock Mergers"""
from typing import Dict, Any, Optional, List
import numpy as np

class ExchangeRatioCalculator:
    """Calculate and analyze exchange ratios for stock-based M&A"""

    def __init__(self, acquirer_price: float, target_price: float):
        self.acquirer_price = acquirer_price
        self.target_price = target_price

    def calculate_fixed_exchange_ratio(self, offer_price_per_share: float) -> Dict[str, Any]:
        """Calculate fixed exchange ratio based on offer price"""

        exchange_ratio = offer_price_per_share / self.acquirer_price

        premium = ((offer_price_per_share - self.target_price) / self.target_price) * 100

        return {
            'exchange_ratio_type': 'fixed',
            'exchange_ratio': exchange_ratio,
            'acquirer_share_price': self.acquirer_price,
            'target_share_price': self.target_price,
            'offer_price_per_target_share': offer_price_per_share,
            'premium_to_target': premium,
            'shares_received_per_target_share': exchange_ratio
        }

    def calculate_fixed_value_exchange(self, offer_value_per_share: float,
                                      current_acquirer_price: float) -> Dict[str, Any]:
        """Calculate exchange ratio that maintains fixed value"""

        exchange_ratio = offer_value_per_share / current_acquirer_price

        original_ratio = offer_value_per_share / self.acquirer_price

        return {
            'exchange_ratio_type': 'fixed_value',
            'target_value': offer_value_per_share,
            'original_acquirer_price': self.acquirer_price,
            'current_acquirer_price': current_acquirer_price,
            'original_exchange_ratio': original_ratio,
            'current_exchange_ratio': exchange_ratio,
            'ratio_adjustment': exchange_ratio - original_ratio,
            'shares_received_per_target_share': exchange_ratio
        }

    def analyze_exchange_ratio_movement(self, exchange_ratio: float,
                                       acquirer_price_range: List[float],
                                       target_shares: float) -> Dict[str, Any]:
        """Analyze value movement across acquirer share price range"""

        analysis = []

        for price in acquirer_price_range:
            value_per_target_share = exchange_ratio * price
            total_value = value_per_target_share * target_shares
            premium = ((value_per_target_share - self.target_price) / self.target_price) * 100

            analysis.append({
                'acquirer_price': price,
                'value_per_target_share': value_per_target_share,
                'total_deal_value': total_value,
                'premium': premium,
                'price_change_vs_announcement': ((price - self.acquirer_price) / self.acquirer_price) * 100
            })

        return {
            'fixed_exchange_ratio': exchange_ratio,
            'target_shares_outstanding': target_shares,
            'price_scenarios': analysis,
            'value_range': {
                'min': min(s['total_deal_value'] for s in analysis),
                'max': max(s['total_deal_value'] for s in analysis),
                'at_announcement': exchange_ratio * self.acquirer_price * target_shares
            }
        }

    def relative_valuation_ratio(self, acquirer_eps: float, target_eps: float,
                                 target_shares: float,
                                 acquirer_shares: float) -> Dict[str, Any]:
        """Calculate exchange ratio based on relative valuations"""

        acquirer_pe = self.acquirer_price / acquirer_eps if acquirer_eps > 0 else 0
        target_pe = self.target_price / target_eps if target_eps > 0 else 0

        exchange_ratio_at_parity = (target_eps / acquirer_eps) if acquirer_eps > 0 else 0

        actual_ratio = self.target_price / self.acquirer_price

        new_shares_issued = exchange_ratio_at_parity * target_shares
        post_deal_shares = acquirer_shares + new_shares_issued
        post_deal_ownership_target = (new_shares_issued / post_deal_shares) * 100

        return {
            'acquirer_pe': acquirer_pe,
            'target_pe': target_pe,
            'acquirer_eps': acquirer_eps,
            'target_eps': target_eps,
            'exchange_ratio_at_eps_parity': exchange_ratio_at_parity,
            'current_market_ratio': actual_ratio,
            'ratio_premium_to_parity': ((actual_ratio / exchange_ratio_at_parity - 1) * 100) if exchange_ratio_at_parity > 0 else 0,
            'post_deal_target_ownership': post_deal_ownership_target
        }

    def contribution_based_ratio(self, acquirer_metrics: Dict[str, float],
                                target_metrics: Dict[str, float],
                                acquirer_shares: float,
                                target_shares: float) -> Dict[str, Any]:
        """Calculate exchange ratio based on contribution analysis"""

        total_revenue = acquirer_metrics.get('revenue', 0) + target_metrics.get('revenue', 0)
        total_ebitda = acquirer_metrics.get('ebitda', 0) + target_metrics.get('ebitda', 0)
        total_net_income = acquirer_metrics.get('net_income', 0) + target_metrics.get('net_income', 0)

        contributions = {}

        if total_revenue > 0:
            contributions['revenue'] = {
                'acquirer': (acquirer_metrics.get('revenue', 0) / total_revenue) * 100,
                'target': (target_metrics.get('revenue', 0) / total_revenue) * 100
            }

        if total_ebitda > 0:
            contributions['ebitda'] = {
                'acquirer': (acquirer_metrics.get('ebitda', 0) / total_ebitda) * 100,
                'target': (target_metrics.get('ebitda', 0) / total_ebitda) * 100
            }

        if total_net_income > 0:
            contributions['net_income'] = {
                'acquirer': (acquirer_metrics.get('net_income', 0) / total_net_income) * 100,
                'target': (target_metrics.get('net_income', 0) / total_net_income) * 100
            }

        avg_target_contribution = np.mean([c['target'] for c in contributions.values()])

        implied_exchange_ratio = (avg_target_contribution / 100) * acquirer_shares / target_shares if target_shares > 0 else 0

        market_ratio = self.target_price / self.acquirer_price

        return {
            'contributions': contributions,
            'average_target_contribution': avg_target_contribution,
            'implied_exchange_ratio': implied_exchange_ratio,
            'market_exchange_ratio': market_ratio,
            'ratio_premium_to_contribution': ((market_ratio / implied_exchange_ratio - 1) * 100) if implied_exchange_ratio > 0 else 0
        }

    def breakeven_exchange_ratio(self, acquirer_eps: float,
                                 target_eps: float,
                                 acquirer_shares: float,
                                 target_shares: float,
                                 synergies: float = 0) -> Dict[str, Any]:
        """Calculate exchange ratio that results in EPS breakeven"""

        acquirer_net_income = acquirer_eps * acquirer_shares
        target_net_income = target_eps * target_shares

        combined_ni = acquirer_net_income + target_net_income + synergies

        # EPS-neutral ratio solves combined_ni / (A + r * T) = acquirer_eps:
        #   r* = (combined_ni / acquirer_eps - A) / T
        breakeven_ratio = None
        if acquirer_eps > 0 and target_shares > 0:
            breakeven_ratio = (combined_ni / acquirer_eps - acquirer_shares) / target_shares
            if breakeven_ratio <= 0:
                breakeven_ratio = None

        breakeven_ratio_results = []
        grid = list(np.linspace(0.5, 2.0, 16))
        for ratio in grid:
            new_shares = ratio * target_shares
            post_deal_shares = acquirer_shares + new_shares
            post_deal_eps = combined_ni / post_deal_shares
            accretion = ((post_deal_eps - acquirer_eps) / acquirer_eps) * 100 if acquirer_eps else None
            breakeven_ratio_results.append({
                'exchange_ratio': float(ratio),
                'post_deal_eps': post_deal_eps,
                'accretion_dilution': accretion
            })

        return {
            'breakeven_exchange_ratio': breakeven_ratio,
            'acquirer_standalone_eps': acquirer_eps,
            'sensitivity_analysis': breakeven_ratio_results,
            'synergies_assumed': synergies
        }

    def collar_analysis(self, base_exchange_ratio: float,
                       floor_price: float,
                       cap_price: float,
                       acquirer_price_scenarios: List[float]) -> Dict[str, Any]:
        """Analyze collar structure on exchange ratio"""

        scenarios = []

        for price in acquirer_price_scenarios:
            # Fixed ratio inside the band; outside it the ratio floats so the
            # target holder receives the value at the floor / cap.
            if price < floor_price:
                effective_ratio = base_exchange_ratio * (floor_price / price)
            elif price > cap_price:
                effective_ratio = base_exchange_ratio * (cap_price / price)
            else:
                effective_ratio = base_exchange_ratio

            value_per_share = effective_ratio * price

            scenarios.append({
                'acquirer_price': price,
                'effective_exchange_ratio': effective_ratio,
                'value_per_target_share': value_per_share,
                'collar_active': price < floor_price or price > cap_price
            })

        return {
            'base_exchange_ratio': base_exchange_ratio,
            'collar_floor': floor_price,
            'collar_cap': cap_price,
            'announcement_price': self.acquirer_price,
            'scenarios': scenarios
        }

# ── JSON contract (MAAnalyticsService "calculate") ───────────────────────────
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
from corporateFinance._cli import is_json_call, run_json, num, opt_num, has, InputError, pct  # noqa: E402


def _key(p: Dict[str, Any], *keys: str) -> Optional[str]:
    for k in keys:
        if has(p, k):
            return k
    return None


def json_calculate(p: Dict[str, Any]) -> Dict[str, Any]:
    """Fixed exchange ratio = offer price per target share / acquirer share price.

    Inputs: acquirer_price (alias acquirer_share_price), required.
    Offer: offer_price (alias offer_price_per_target_share), or target_price
    (unaffected) + premium (decimal). target_price is also used to report the
    premium when the offer is given directly.
    Optional: target_shares_outstanding (shares issued, deal value),
    acquirer_shares_outstanding (pro-forma ownership).
    """
    ak = _key(p, 'acquirer_price', 'acquirer_share_price')
    if not ak:
        raise InputError("Missing required input: acquirer_price (acquirer share price)")
    acq_px = num(p, ak, gt=0)
    tk = _key(p, 'target_price', 'target_share_price')
    tgt_px = num(p, tk, gt=0) if tk else None

    ok = _key(p, 'offer_price', 'offer_price_per_target_share')
    if ok:
        offer = num(p, ok, gt=0)
        if has(p, 'premium') and tgt_px is not None:
            implied = tgt_px * (1 + num(p, 'premium', min=-1))
            if abs(implied - offer) > 1e-6 * max(1.0, offer):
                raise InputError(f"offer_price {offer:g} disagrees with target_price x (1 + premium) = {implied:g}; "
                                 "give one or the other")
    elif has(p, 'premium'):
        if tgt_px is None:
            raise InputError("premium needs target_price (unaffected target share price)")
        offer = tgt_px * (1 + num(p, 'premium', label='premium (decimal)', min=-1))
    else:
        raise InputError("Missing required input: offer_price, or target_price + premium")

    ratio = offer / acq_px
    out: Dict[str, Any] = {
        'exchange_ratio': ratio,
        'offer_price_per_target_share': offer,
        'acquirer_share_price': acq_px,
        'target_share_price': tgt_px,
        'premium_pct': ((offer / tgt_px - 1) * 100) if tgt_px else None,
    }
    sk = _key(p, 'target_shares_outstanding', 'target_shares')
    if sk:
        tgt_sh = num(p, sk, gt=0)
        new_sh = ratio * tgt_sh
        out['new_acquirer_shares_issued'] = new_sh
        out['deal_equity_value'] = offer * tgt_sh
        ak2 = _key(p, 'acquirer_shares_outstanding', 'acquirer_shares')
        if ak2:
            acq_sh = num(p, ak2, gt=0)
            post = acq_sh + new_sh
            out['pro_forma_shares_outstanding'] = post
            out['acquirer_ownership_pct'] = acq_sh / post * 100
            out['target_holders_ownership_pct'] = new_sh / post * 100
    # Value per target share as the acquirer price moves (fixed ratio, no collar).
    out['acquirer_price_sensitivity'] = [
        {'acquirer_price_change_pct': chg * 100,
         'acquirer_price': acq_px * (1 + chg),
         'value_per_target_share': ratio * acq_px * (1 + chg),
         'premium_pct': ((ratio * acq_px * (1 + chg) / tgt_px - 1) * 100) if tgt_px else None}
        for chg in (-0.20, -0.10, 0.0, 0.10, 0.20)
    ]
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
