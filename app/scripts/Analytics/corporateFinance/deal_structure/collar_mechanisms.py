"""Collar Mechanisms for Stock Deal Protection"""
from typing import Dict, Any, List, Optional
import numpy as np

class CollarType:
    FIXED = "fixed"
    FLOATING = "floating"
    SYMMETRIC = "symmetric"
    ASYMMETRIC = "asymmetric"

class CollarMechanism:
    """Analyze collar structures in stock-for-stock M&A transactions"""

    def __init__(self, announcement_price: float,
                 target_shares: float,
                 base_exchange_ratio: float):
        self.announcement_price = announcement_price
        self.target_shares = target_shares
        self.base_exchange_ratio = base_exchange_ratio

    def fixed_collar(self, floor_price: float,
                    cap_price: float,
                    price_scenarios: Optional[List[float]] = None) -> Dict[str, Any]:
        """
        Fixed collar: Exchange ratio adjusts to maintain value within price band

        If acquirer price falls below floor: ratio increases (more shares)
        If acquirer price rises above cap: ratio decreases (fewer shares)
        """

        if price_scenarios is None:
            price_scenarios = np.linspace(
                self.announcement_price * 0.7,
                self.announcement_price * 1.3,
                20
            ).tolist()

        scenarios = []
        base_value = self.base_exchange_ratio * self.announcement_price

        for price in price_scenarios:
            # Fixed value inside the band (ratio floats); ratio locks at the
            # floor / cap outside it. See collar_terms().
            t = collar_terms('fixed_value', self.base_exchange_ratio, self.announcement_price,
                             floor_price, cap_price, price)
            effective_ratio = t['effective_exchange_ratio']
            protection_type = {'below_floor': 'floor_active', 'above_cap': 'cap_active'}.get(t['zone'], 'within_collar')

            actual_value = effective_ratio * price

            total_deal_value = actual_value * self.target_shares
            total_shares_issued = effective_ratio * self.target_shares

            scenarios.append({
                'acquirer_price': price,
                'effective_exchange_ratio': effective_ratio,
                'value_per_target_share': actual_value,
                'total_deal_value': total_deal_value,
                'shares_issued': total_shares_issued,
                'protection_active': protection_type,
                'price_change_pct': ((price - self.announcement_price) / self.announcement_price) * 100
            })

        return {
            'collar_type': 'fixed',
            'announcement_price': self.announcement_price,
            'base_exchange_ratio': self.base_exchange_ratio,
            'floor_price': floor_price,
            'cap_price': cap_price,
            'floor_pct_below': ((self.announcement_price - floor_price) / self.announcement_price) * 100,
            'cap_pct_above': ((cap_price - self.announcement_price) / self.announcement_price) * 100,
            'scenarios': scenarios,
            'value_range': {
                'min': min(s['value_per_target_share'] for s in scenarios),
                'max': max(s['value_per_target_share'] for s in scenarios),
                'base': self.base_exchange_ratio * self.announcement_price
            }
        }

    def floating_collar(self, min_exchange_ratio: float,
                       max_exchange_ratio: float,
                       price_scenarios: Optional[List[float]] = None) -> Dict[str, Any]:
        """
        Floating collar: Exchange ratio bounded, value floats with stock price

        Min ratio: Floor on shares received (downside protection for target)
        Max ratio: Cap on shares issued (dilution protection for acquirer)
        """

        if price_scenarios is None:
            price_scenarios = np.linspace(
                self.announcement_price * 0.7,
                self.announcement_price * 1.3,
                20
            ).tolist()

        scenarios = []

        base_value = self.base_exchange_ratio * self.announcement_price
        for price in price_scenarios:
            # Ratio floats to deliver the announced value, bounded by the
            # min / max ratio; at a bound the value moves with the price.
            floating_ratio = base_value / price
            if floating_ratio > max_exchange_ratio:
                effective_ratio = max_exchange_ratio
                protection_type = "max_ratio_active"
            elif floating_ratio < min_exchange_ratio:
                effective_ratio = min_exchange_ratio
                protection_type = "min_ratio_active"
            else:
                effective_ratio = floating_ratio
                protection_type = "within_collar"

            value_per_share = effective_ratio * price
            total_deal_value = value_per_share * self.target_shares
            total_shares_issued = effective_ratio * self.target_shares

            scenarios.append({
                'acquirer_price': price,
                'effective_exchange_ratio': effective_ratio,
                'value_per_target_share': value_per_share,
                'total_deal_value': total_deal_value,
                'shares_issued': total_shares_issued,
                'protection_active': protection_type
            })

        return {
            'collar_type': 'floating',
            'base_exchange_ratio': self.base_exchange_ratio,
            'min_exchange_ratio': min_exchange_ratio,
            'max_exchange_ratio': max_exchange_ratio,
            'scenarios': scenarios,
            'ratio_range': {
                'min': min_exchange_ratio,
                'max': max_exchange_ratio,
                'base': self.base_exchange_ratio
            }
        }

    def asymmetric_collar(self, floor_price: Optional[float] = None,
                         cap_price: Optional[float] = None,
                         price_scenarios: Optional[List[float]] = None) -> Dict[str, Any]:
        """
        Asymmetric collar: Only floor or only cap protection

        One-sided protection for target (floor only) or acquirer (cap only)
        """

        if floor_price is None and cap_price is None:
            raise ValueError("Must specify either floor_price or cap_price")

        if price_scenarios is None:
            price_scenarios = np.linspace(
                self.announcement_price * 0.7,
                self.announcement_price * 1.3,
                20
            ).tolist()

        scenarios = []
        base_value = self.base_exchange_ratio * self.announcement_price

        for price in price_scenarios:
            protection_type = "no_protection"

            # Fixed ratio with one-sided protection: below the floor the ratio
            # rises so the target still receives base_ratio x floor; above the
            # cap it falls so the target receives base_ratio x cap.
            if floor_price and price < floor_price:
                effective_ratio = self.base_exchange_ratio * floor_price / price
                protection_type = "floor_active"
            elif cap_price and price > cap_price:
                effective_ratio = self.base_exchange_ratio * cap_price / price
                protection_type = "cap_active"
            else:
                effective_ratio = self.base_exchange_ratio

            value_per_share = effective_ratio * price
            total_deal_value = value_per_share * self.target_shares

            scenarios.append({
                'acquirer_price': price,
                'effective_exchange_ratio': effective_ratio,
                'value_per_target_share': value_per_share,
                'total_deal_value': total_deal_value,
                'protection_active': protection_type
            })

        collar_structure = "floor_only" if floor_price and not cap_price else "cap_only"

        return {
            'collar_type': 'asymmetric',
            'collar_structure': collar_structure,
            'floor_price': floor_price,
            'cap_price': cap_price,
            'base_exchange_ratio': self.base_exchange_ratio,
            'scenarios': scenarios
        }

    def walk_away_collar(self, floor_price: float,
                        cap_price: float,
                        termination_fee: float,
                        price_scenarios: Optional[List[float]] = None) -> Dict[str, Any]:
        """
        Collar with walk-away rights if price exceeds bounds

        Either party can terminate if price outside collar bounds
        """

        if price_scenarios is None:
            price_scenarios = np.linspace(
                self.announcement_price * 0.6,
                self.announcement_price * 1.4,
                20
            ).tolist()

        scenarios = []

        for price in price_scenarios:
            if price < floor_price or price > cap_price:
                deal_proceeds = False
                value_per_share = 0
                termination_cost = termination_fee
                outcome = "walk_away_triggered"
            else:
                deal_proceeds = True
                value_per_share = self.base_exchange_ratio * price
                termination_cost = 0
                outcome = "deal_proceeds"

            scenarios.append({
                'acquirer_price': price,
                'deal_proceeds': deal_proceeds,
                'value_per_target_share': value_per_share,
                'termination_fee': termination_cost,
                'outcome': outcome
            })

        return {
            'collar_type': 'walk_away',
            'floor_price': floor_price,
            'cap_price': cap_price,
            'termination_fee': termination_fee,
            'base_exchange_ratio': self.base_exchange_ratio,
            'scenarios': scenarios,
            'deal_completion_rate': sum(1 for s in scenarios if s['deal_proceeds']) / len(scenarios) * 100
        }

    def collar_valuation_impact(self, collar_structure: Dict[str, Any],
                                volatility: float,
                                time_to_close: float) -> Dict[str, Any]:
        """Estimate collar impact on deal valuation using option pricing concepts"""

        base_value = self.base_exchange_ratio * self.announcement_price * self.target_shares

        scenarios = collar_structure.get('scenarios', [])
        if not scenarios:
            return {'error': 'No scenarios in collar structure'}

        price_changes = [s.get('price_change_pct', 0) for s in scenarios if 'price_change_pct' in s]
        values = [s['total_deal_value'] for s in scenarios]

        value_std = np.std(values)
        value_mean = np.mean(values)

        protection_scenarios = [s for s in scenarios if s.get('protection_active') not in ['within_collar', 'no_protection', None]]
        protection_value = value_mean - base_value if protection_scenarios else 0

        return {
            'base_deal_value': base_value,
            'expected_value_with_collar': value_mean,
            'value_standard_deviation': value_std,
            'collar_protection_value': protection_value,
            'coefficient_of_variation': (value_std / value_mean * 100) if value_mean > 0 else 0,
            'scenarios_with_protection': len(protection_scenarios),
            'total_scenarios': len(scenarios)
        }

    def compare_collar_structures(self, price_scenarios: List[float]) -> Dict[str, Any]:
        """Compare different collar structures"""

        no_collar = {
            'type': 'no_collar',
            'scenarios': [
                {
                    'acquirer_price': price,
                    'value_per_target_share': self.base_exchange_ratio * price,
                    'total_deal_value': self.base_exchange_ratio * price * self.target_shares
                }
                for price in price_scenarios
            ]
        }

        fixed = self.fixed_collar(
            floor_price=self.announcement_price * 0.9,
            cap_price=self.announcement_price * 1.1,
            price_scenarios=price_scenarios
        )

        floating = self.floating_collar(
            min_exchange_ratio=self.base_exchange_ratio * 0.9,
            max_exchange_ratio=self.base_exchange_ratio * 1.1,
            price_scenarios=price_scenarios
        )

        floor_only = self.asymmetric_collar(
            floor_price=self.announcement_price * 0.9,
            price_scenarios=price_scenarios
        )

        structures = {
            'no_collar': no_collar,
            'fixed_collar': fixed,
            'floating_collar': floating,
            'floor_only': floor_only
        }

        comparison = []
        for name, structure in structures.items():
            scenarios = structure.get('scenarios', [])
            values = [s['total_deal_value'] for s in scenarios]

            comparison.append({
                'structure': name,
                'mean_value': np.mean(values),
                'std_dev': np.std(values),
                'min_value': min(values),
                'max_value': max(values),
                'coefficient_of_variation': (np.std(values) / np.mean(values) * 100) if np.mean(values) > 0 else 0
            })

        return {
            'comparison': comparison,
            'structures': structures
        }

# ── JSON contract (MAAnalyticsService "analyze") ─────────────────────────────
import sys as _sys
from pathlib import Path as _Path
_sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))
from corporateFinance._cli import (is_json_call, run_json, num, opt_num, num_list, text,  # noqa: E402
                                   has, InputError, pct)


def collar_terms(collar_type: str, base_ratio: float, reference_price: float,
                 floor_price: float, cap_price: float, price: float) -> Dict[str, Any]:
    """Effective exchange ratio and value per target share at one acquirer price.

    fixed_ratio : ratio = base inside [floor, cap]; outside, the ratio floats so
                  the target receives base x floor (below) / base x cap (above).
    fixed_value : value = base x reference_price inside the band (ratio floats);
                  outside, the ratio locks at value/floor (below) or value/cap
                  (above) and value moves with the acquirer price.
    """
    if collar_type == 'fixed_ratio':
        if price < floor_price:
            ratio, zone = base_ratio * floor_price / price, 'below_floor'
        elif price > cap_price:
            ratio, zone = base_ratio * cap_price / price, 'above_cap'
        else:
            ratio, zone = base_ratio, 'within_collar'
    else:
        target_value = base_ratio * reference_price
        if price < floor_price:
            ratio, zone = target_value / floor_price, 'below_floor'
        elif price > cap_price:
            ratio, zone = target_value / cap_price, 'above_cap'
        else:
            ratio, zone = target_value / price, 'within_collar'
    return {'acquirer_price': price, 'effective_exchange_ratio': ratio,
            'value_per_target_share': ratio * price, 'zone': zone}


def _key(p: Dict[str, Any], *keys: str) -> Optional[str]:
    for k in keys:
        if has(p, k):
            return k
    return None


def json_analyze(p: Dict[str, Any]) -> Dict[str, Any]:
    """Collar on a stock deal.

    Inputs (required): base_ratio (alias base_exchange_ratio / fixed_exchange_ratio),
    floor_price (alias collar_floor), cap_price (alias collar_ceiling),
    acquirer_price (alias acquirer_share_price / announcement_price; the
    reference price the collar was struck at).
    Optional: collar_type 'fixed_ratio' (default) | 'fixed_value',
    target_shares (alias target_shares_outstanding), price_scenarios (list).
    """
    def req(keys, label, **kw):
        k = _key(p, *keys)
        if not k:
            raise InputError(f"Missing required input: {label}")
        return num(p, k, label=label, **kw)

    base = req(('base_ratio', 'base_exchange_ratio', 'fixed_exchange_ratio'), 'base_ratio', gt=0)
    floor_px = req(('floor_price', 'collar_floor'), 'floor_price', gt=0)
    cap_px = req(('cap_price', 'collar_ceiling'), 'cap_price', gt=0)
    ref = req(('acquirer_price', 'acquirer_share_price', 'announcement_price'),
              'acquirer_price (acquirer share price the collar is struck at)', gt=0)
    if floor_px >= cap_px:
        raise InputError("floor_price must be below cap_price")
    ctype = text(p, 'collar_type', choices=('fixed_ratio', 'fixed_value')) if has(p, 'collar_type') else 'fixed_ratio'
    sk = _key(p, 'target_shares', 'target_shares_outstanding')
    tgt_sh = num(p, sk, gt=0) if sk else None

    if has(p, 'price_scenarios'):
        prices = [x for x in num_list(p, 'price_scenarios') if x > 0]
    else:
        lo, hi = min(floor_px, ref) * 0.8, max(cap_px, ref) * 1.2
        prices = [lo + (hi - lo) * i / 10 for i in range(11)]
    prices = sorted(set(prices + [floor_px, cap_px, ref]))

    rows = []
    for px in prices:
        r = collar_terms(ctype, base, ref, floor_px, cap_px, px)
        r['price_change_pct'] = (px / ref - 1) * 100
        if tgt_sh is not None:
            r['shares_issued'] = r['effective_exchange_ratio'] * tgt_sh
            r['total_deal_value'] = r['value_per_target_share'] * tgt_sh
        rows.append(r)

    now = collar_terms(ctype, base, ref, floor_px, cap_px, ref)
    out = {
        'collar_type': ctype,
        'base_exchange_ratio': base,
        'reference_acquirer_price': ref,
        'floor_price': floor_px,
        'cap_price': cap_px,
        'floor_vs_reference_pct': (floor_px / ref - 1) * 100,
        'cap_vs_reference_pct': (cap_px / ref - 1) * 100,
        'current_effective_ratio': now['effective_exchange_ratio'],
        'current_value_per_target_share': now['value_per_target_share'],
        'current_zone': now['zone'],
        'min_ratio_in_band': (base if ctype == 'fixed_ratio' else base * ref / cap_px),
        'max_ratio_in_band': (base if ctype == 'fixed_ratio' else base * ref / floor_px),
        'scenarios': rows,
    }
    if ctype == 'fixed_ratio':
        out['protected_value_at_floor'] = base * floor_px
        out['capped_value_at_cap'] = base * cap_px
    else:
        out['fixed_value_per_target_share'] = base * ref
    if tgt_sh is not None:
        out['current_shares_issued'] = now['effective_exchange_ratio'] * tgt_sh
        out['current_deal_value'] = now['value_per_target_share'] * tgt_sh
    return out

def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json({"analyze": json_analyze})


if __name__ == '__main__':
    main()
