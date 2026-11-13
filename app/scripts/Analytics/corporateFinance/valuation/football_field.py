"""Football Field Valuation Chart - Data Generation for Frontend Rendering"""
import sys
import json
from typing import Dict, Any, List, Optional
from dataclasses import dataclass, asdict

@dataclass
class ValuationRange:
    method_name: str
    low: float
    high: float
    midpoint: float
    weight: float = 1.0

class FootballFieldChart:
    """Generate football field valuation data for frontend charting"""

    def __init__(self):
        self.colors = {
            'DCF': '#2E86AB',
            'Trading Comps': '#A23B72',
            'Transaction Comps': '#F18F01',
            'LBO': '#C73E1D',
            'Premiums Paid': '#6A994E',
            'Sum of Parts': '#BC4B51'
        }

    def add_valuation_range(self, method: str, low: float, high: float,
                           midpoint: Optional[float] = None, weight: float = 1.0) -> ValuationRange:
        """Add valuation range from methodology"""

        if midpoint is None:
            midpoint = (low + high) / 2

        return ValuationRange(
            method_name=method,
            low=low,
            high=high,
            midpoint=midpoint,
            weight=weight
        )

    def format_value(self, value: float, format_type: str = 'auto') -> Dict[str, Any]:
        """Format value with appropriate scale

        Args:
            value: Value to format
            format_type: 'auto', 'billions', 'millions', 'thousands', 'dollars'

        Returns:
            Dict with formatted value and scale
        """
        if format_type == 'auto':
            if abs(value) >= 1e9:
                return {'value': value / 1e9, 'scale': 'B', 'formatted': f'${value/1e9:.2f}B'}
            elif abs(value) >= 1e6:
                return {'value': value / 1e6, 'scale': 'M', 'formatted': f'${value/1e6:.2f}M'}
            elif abs(value) >= 1e3:
                return {'value': value / 1e3, 'scale': 'K', 'formatted': f'${value/1e3:.2f}K'}
            else:
                return {'value': value, 'scale': '', 'formatted': f'${value:.2f}'}
        elif format_type == 'billions':
            return {'value': value / 1e9, 'scale': 'B', 'formatted': f'${value/1e9:.2f}B'}
        elif format_type == 'millions':
            return {'value': value / 1e6, 'scale': 'M', 'formatted': f'${value/1e6:.2f}M'}
        elif format_type == 'thousands':
            return {'value': value / 1e3, 'scale': 'K', 'formatted': f'${value/1e3:.2f}K'}
        else:
            return {'value': value, 'scale': '', 'formatted': f'${value:.2f}'}

    def generate_chart_data(self, valuation_ranges: List[ValuationRange],
                           current_price: Optional[float] = None,
                           offer_price: Optional[float] = None,
                           format_type: str = 'auto') -> Dict[str, Any]:
        """Generate football field chart data for frontend rendering

        Args:
            valuation_ranges: List of valuation ranges
            current_price: Current market price (optional)
            offer_price: Offer/deal price (optional)
            format_type: Value formatting ('auto', 'billions', 'millions', 'thousands', 'dollars')

        Returns:
            Chart data structure for frontend
        """

        if not valuation_ranges:
            return {'error': 'No valuation ranges provided'}

        # Collect all values for scaling
        all_values = []
        for vr in valuation_ranges:
            all_values.extend([vr.low, vr.high])
        if current_price:
            all_values.append(current_price)
        if offer_price:
            all_values.append(offer_price)

        min_val = min(all_values) * 0.9
        max_val = max(all_values) * 1.1

        # Build chart data
        chart_ranges = []
        for vr in valuation_ranges:
            color = self.colors.get(vr.method_name, '#95B8D1')

            chart_ranges.append({
                'method': vr.method_name,
                'low': vr.low,
                'high': vr.high,
                'midpoint': vr.midpoint,
                'weight': vr.weight,
                'color': color,
                'range_pct': ((vr.high - vr.low) / vr.midpoint) * 100 if vr.midpoint > 0 else 0,
                'low_formatted': self.format_value(vr.low, format_type),
                'high_formatted': self.format_value(vr.high, format_type),
                'midpoint_formatted': self.format_value(vr.midpoint, format_type)
            })

        midpoints = [vr.midpoint for vr in valuation_ranges]
        overall_midpoint = sum(midpoints) / len(midpoints) if midpoints else 0

        chart_data = {
            'valuation_ranges': chart_ranges,
            'overall_low': min(vr.low for vr in valuation_ranges),
            'overall_high': max(vr.high for vr in valuation_ranges),
            'overall_midpoint': overall_midpoint,
            'min_val': min_val,
            'max_val': max_val,
            'current_price': current_price,
            'offer_price': offer_price,
            'format_type': format_type
        }

        # Add reference lines
        if current_price:
            chart_data['current_price_formatted'] = self.format_value(current_price, format_type)
            chart_data['current_vs_low_pct'] = ((current_price - chart_data['overall_low']) / chart_data['overall_low']) * 100
            chart_data['current_vs_high_pct'] = ((current_price - chart_data['overall_high']) / chart_data['overall_high']) * 100

        if offer_price:
            chart_data['offer_price_formatted'] = self.format_value(offer_price, format_type)
            chart_data['offer_vs_current_pct'] = ((offer_price - current_price) / current_price) * 100 if current_price else 0
            chart_data['offer_vs_midpoint_pct'] = ((offer_price - overall_midpoint) / overall_midpoint) * 100 if overall_midpoint > 0 else 0

        return chart_data

    def calculate_weighted_valuation(self, valuation_ranges: List[ValuationRange]) -> Dict[str, float]:
        """Calculate weighted average valuation"""

        if not valuation_ranges:
            return {
                'weighted_low': 0,
                'weighted_high': 0,
                'weighted_midpoint': 0,
                'total_weight': 0
            }

        total_weight = sum(vr.weight for vr in valuation_ranges)

        if total_weight == 0:
            return {
                'weighted_low': 0,
                'weighted_high': 0,
                'weighted_midpoint': 0,
                'total_weight': 0
            }

        weighted_low = sum(vr.low * vr.weight for vr in valuation_ranges) / total_weight
        weighted_high = sum(vr.high * vr.weight for vr in valuation_ranges) / total_weight
        weighted_midpoint = sum(vr.midpoint * vr.weight for vr in valuation_ranges) / total_weight

        return {
            'weighted_low': weighted_low,
            'weighted_high': weighted_high,
            'weighted_midpoint': weighted_midpoint,
            'total_weight': total_weight
        }

    def analyze_valuation_dispersion(self, valuation_ranges: List[ValuationRange]) -> Dict[str, float]:
        """Analyze dispersion across valuation methods"""

        if not valuation_ranges:
            return {
                'mean_midpoint': 0,
                'median_midpoint': 0,
                'std_dev': 0,
                'coefficient_of_variation': 0,
                'min_midpoint': 0,
                'max_midpoint': 0,
                'range': 0,
                'range_pct': 0
            }

        midpoints = [vr.midpoint for vr in valuation_ranges]

        mean_val = sum(midpoints) / len(midpoints)
        sorted_midpoints = sorted(midpoints)
        n = len(sorted_midpoints)
        median_val = sorted_midpoints[n // 2] if n % 2 == 1 else (sorted_midpoints[n // 2 - 1] + sorted_midpoints[n // 2]) / 2

        # Calculate standard deviation
        variance = sum((x - mean_val) ** 2 for x in midpoints) / len(midpoints)
        std_dev = variance ** 0.5

        return {
            'mean_midpoint': mean_val,
            'median_midpoint': median_val,
            'std_dev': std_dev,
            'coefficient_of_variation': (std_dev / mean_val) * 100 if mean_val > 0 else 0,
            'min_midpoint': min(midpoints),
            'max_midpoint': max(midpoints),
            'range': max(midpoints) - min(midpoints),
            'range_pct': ((max(midpoints) - min(midpoints)) / mean_val) * 100 if mean_val > 0 else 0
        }

    def generate_summary_table(self, valuation_ranges: List[ValuationRange],
                               current_price: Optional[float] = None,
                               format_type: str = 'auto') -> Dict[str, Any]:
        """Generate valuation summary table

        Args:
            valuation_ranges: List of valuation ranges
            current_price: Current market price (optional)
            format_type: Value formatting ('auto', 'billions', 'millions', 'thousands', 'dollars')

        Returns:
            Summary table data
        """

        table_rows = []
        for vr in valuation_ranges:
            row = {
                'method': vr.method_name,
                'low': vr.low,
                'midpoint': vr.midpoint,
                'high': vr.high,
                'weight': vr.weight,
                'low_formatted': self.format_value(vr.low, format_type),
                'midpoint_formatted': self.format_value(vr.midpoint, format_type),
                'high_formatted': self.format_value(vr.high, format_type)
            }

            if current_price:
                premium = ((vr.midpoint - current_price) / current_price) * 100 if current_price > 0 else 0
                row['implied_premium_to_current_pct'] = premium

            table_rows.append(row)

        weighted = self.calculate_weighted_valuation(valuation_ranges)
        dispersion = self.analyze_valuation_dispersion(valuation_ranges)

        summary_row = {
            'method': 'WEIGHTED AVERAGE',
            'low': weighted['weighted_low'],
            'midpoint': weighted['weighted_midpoint'],
            'high': weighted['weighted_high'],
            'weight': weighted['total_weight'],
            'low_formatted': self.format_value(weighted['weighted_low'], format_type),
            'midpoint_formatted': self.format_value(weighted['weighted_midpoint'], format_type),
            'high_formatted': self.format_value(weighted['weighted_high'], format_type)
        }

        if current_price:
            premium = ((weighted['weighted_midpoint'] - current_price) / current_price) * 100 if current_price > 0 else 0
            summary_row['implied_premium_to_current_pct'] = premium

        return {
            'valuation_methods': table_rows,
            'weighted_average': summary_row,
            'dispersion_analysis': dispersion,
            'format_type': format_type
        }

# ── JSON contract (MAAnalyticsService "generate" / ma_football_field) ────────

def _json_generate(p: Dict[str, Any]) -> Dict[str, Any]:
    """Valuation ranges per method, on one basis (per share, or equity value).

    methods: [{method, low, high, midpoint?}]  -- or the flat keys
    dcf_low/dcf_high, comps_low/comps_high, precedent_low/precedent_high.
    Optional current_price / offer_price on the same basis.
    """
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import opt_num, InputError
    methods = []
    if isinstance(p.get('methods'), list):
        for m in p['methods']:
            if isinstance(m, dict):
                methods.append((str(m.get('method') or 'Method'), m.get('low'), m.get('high'), m.get('midpoint')))
    for key, label in (('dcf', 'DCF'), ('comps', 'Trading Comps'), ('precedent', 'Precedent Transactions'),
                       ('lbo', 'LBO'), ('premiums', 'Premiums Paid')):
        if p.get(f'{key}_low') is not None or p.get(f'{key}_high') is not None:
            methods.append((label, p.get(f'{key}_low'), p.get(f'{key}_high'), None))
    if not methods:
        raise InputError("Missing required input: methods (each with low and high)")
    rows = []
    for name, lo, hi, mid in methods:
        try:
            lo, hi = float(lo), float(hi)
        except (TypeError, ValueError):
            raise InputError(f"{name}: low and high are both required numbers")
        if hi < lo:
            raise InputError(f"{name}: high ({hi}) is below low ({lo})")
        mid = float(mid) if mid is not None else (lo + hi) / 2
        rows.append({'method': name, 'low': lo, 'midpoint': mid, 'high': hi})
    current = opt_num(p, 'current_price', gt=0)
    offer = opt_num(p, 'offer_price', gt=0)
    for r in rows:
        if current:
            r['midpoint_vs_current_pct'] = (r['midpoint'] / current - 1) * 100
        if offer:
            r['offer_position'] = ('below range' if offer < r['low'] else
                                   'above range' if offer > r['high'] else 'within range')
    out = {
        'overall_low': min(r['low'] for r in rows),
        'overall_high': max(r['high'] for r in rows),
        'overlap_low': max(r['low'] for r in rows),
        'overlap_high': min(r['high'] for r in rows),
        'ranges': rows,
    }
    if out['overlap_low'] > out['overlap_high']:
        out['overlap_low'] = out['overlap_high'] = None
        out['note'] = 'Method ranges do not all overlap.'
    if current:
        out['current_price'] = current
    if offer:
        out['offer_price'] = offer
        out['methods_offer_within'] = sum(1 for r in rows if r['low'] <= offer <= r['high'])
    return out


JSON_COMMANDS = {'generate': _json_generate}


def main():
    """CLI entry point: <command> '<params JSON object>' (contract: corporateFinance/_cli.py)."""
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import run_json, fail
    if len(sys.argv) != 3:
        fail("Usage: <script> <command> '<params JSON object>'")
    run_json(JSON_COMMANDS)


if __name__ == '__main__':
    main()
