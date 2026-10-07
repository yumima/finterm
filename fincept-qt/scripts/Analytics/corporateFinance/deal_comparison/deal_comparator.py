"""Deal Comparison and Analysis Tool"""
from typing import Dict, Any, List, Optional
import numpy as np
from dataclasses import dataclass

@dataclass
class Deal:
    deal_id: str
    target_name: str
    acquirer_name: str
    deal_value: float
    offer_price_per_share: Optional[float]
    premium_1day: float
    payment_cash_pct: float
    payment_stock_pct: float
    ev_revenue: Optional[float]
    ev_ebitda: Optional[float]
    synergies: Optional[float]
    status: str
    industry: str
    announced_date: str

class DealComparator:
    """Compare multiple M&A deals side-by-side"""

    def compare_deals(self, deals: List[Deal]) -> Dict[str, Any]:
        """Compare multiple deals across key metrics"""

        if len(deals) < 2:
            raise ValueError("Need at least 2 deals to compare")

        comparison_table = []

        for deal in deals:
            comparison_table.append({
                'deal_id': deal.deal_id,
                'target': deal.target_name,
                'acquirer': deal.acquirer_name,
                'deal_value': deal.deal_value,
                'offer_price': deal.offer_price_per_share,
                'premium_1day': deal.premium_1day,
                'payment_cash_pct': deal.payment_cash_pct,
                'payment_stock_pct': deal.payment_stock_pct,
                'ev_revenue': deal.ev_revenue,
                'ev_ebitda': deal.ev_ebitda,
                'synergies': deal.synergies,
                'status': deal.status,
                'industry': deal.industry,
                'announced_date': deal.announced_date
            })

        deal_values = [d.deal_value for d in deals]
        premiums = [d.premium_1day for d in deals]
        ev_revenues = [d.ev_revenue for d in deals if d.ev_revenue is not None]
        ev_ebitdas = [d.ev_ebitda for d in deals if d.ev_ebitda is not None]

        statistics = {
            'deal_value': {
                'min': min(deal_values),
                'max': max(deal_values),
                'mean': np.mean(deal_values),
                'median': np.median(deal_values)
            },
            'premium_1day': {
                'min': min(premiums),
                'max': max(premiums),
                'mean': np.mean(premiums),
                'median': np.median(premiums)
            }
        }

        if ev_revenues:
            statistics['ev_revenue'] = {
                'min': min(ev_revenues),
                'max': max(ev_revenues),
                'mean': np.mean(ev_revenues),
                'median': np.median(ev_revenues)
            }

        if ev_ebitdas:
            statistics['ev_ebitda'] = {
                'min': min(ev_ebitdas),
                'max': max(ev_ebitdas),
                'mean': np.mean(ev_ebitdas),
                'median': np.median(ev_ebitdas)
            }

        return {
            'num_deals': len(deals),
            'comparison_table': comparison_table,
            'statistics': statistics
        }

    def rank_deals(self, deals: List[Deal],
                  criteria: str = 'premium') -> Dict[str, Any]:
        """Rank deals by specific criteria"""

        ranking_map = {
            'premium': lambda d: d.premium_1day,
            'deal_value': lambda d: d.deal_value,
            'ev_revenue': lambda d: d.ev_revenue if d.ev_revenue else 0,
            'ev_ebitda': lambda d: d.ev_ebitda if d.ev_ebitda else 0,
            'synergies': lambda d: d.synergies if d.synergies else 0
        }

        if criteria not in ranking_map:
            raise ValueError(f"Invalid criteria. Choose from: {list(ranking_map.keys())}")

        sorted_deals = sorted(deals, key=ranking_map[criteria], reverse=True)

        rankings = []
        for rank, deal in enumerate(sorted_deals, 1):
            rankings.append({
                'rank': rank,
                'deal_id': deal.deal_id,
                'target': deal.target_name,
                'acquirer': deal.acquirer_name,
                'metric_value': ranking_map[criteria](deal)
            })

        return {
            'ranking_criteria': criteria,
            'rankings': rankings
        }

    def payment_structure_analysis(self, deals: List[Deal]) -> Dict[str, Any]:
        """Analyze payment structures across deals"""

        all_cash_count = sum(1 for d in deals if d.payment_cash_pct == 100)
        all_stock_count = sum(1 for d in deals if d.payment_stock_pct == 100)
        mixed_count = len(deals) - all_cash_count - all_stock_count

        avg_cash_pct = np.mean([d.payment_cash_pct for d in deals])
        avg_stock_pct = np.mean([d.payment_stock_pct for d in deals])

        return {
            'total_deals': len(deals),
            'all_cash_deals': all_cash_count,
            'all_stock_deals': all_stock_count,
            'mixed_deals': mixed_count,
            'average_cash_pct': avg_cash_pct,
            'average_stock_pct': avg_stock_pct,
            'distribution': {
                'all_cash_pct': (all_cash_count / len(deals) * 100) if deals else 0,
                'all_stock_pct': (all_stock_count / len(deals) * 100) if deals else 0,
                'mixed_pct': (mixed_count / len(deals) * 100) if deals else 0
            }
        }

    def industry_analysis(self, deals: List[Deal]) -> Dict[str, Any]:
        """Analyze deals by industry"""

        industry_groups = {}

        for deal in deals:
            if deal.industry not in industry_groups:
                industry_groups[deal.industry] = []
            industry_groups[deal.industry].append(deal)

        industry_stats = {}

        for industry, industry_deals in industry_groups.items():
            deal_values = [d.deal_value for d in industry_deals]
            premiums = [d.premium_1day for d in industry_deals]

            industry_stats[industry] = {
                'count': len(industry_deals),
                'total_value': sum(deal_values),
                'avg_deal_value': np.mean(deal_values),
                'avg_premium': np.mean(premiums),
                'median_premium': np.median(premiums)
            }

        return {
            'industries': list(industry_groups.keys()),
            'industry_statistics': industry_stats
        }

    def premium_benchmarking(self, target_deal: Deal,
                           comparable_deals: List[Deal]) -> Dict[str, Any]:
        """Benchmark target deal premium against comparables"""

        comp_premiums = [d.premium_1day for d in comparable_deals]

        percentile_25 = np.percentile(comp_premiums, 25)
        percentile_50 = np.percentile(comp_premiums, 50)
        percentile_75 = np.percentile(comp_premiums, 75)

        target_premium = target_deal.premium_1day

        if target_premium < percentile_25:
            position = "below 25th percentile"
        elif target_premium < percentile_50:
            position = "between 25th and 50th percentile"
        elif target_premium < percentile_75:
            position = "between 50th and 75th percentile"
        else:
            position = "above 75th percentile"

        return {
            'target_deal': {
                'deal_id': target_deal.deal_id,
                'target': target_deal.target_name,
                'premium': target_premium
            },
            'comparable_premiums': {
                '25th_percentile': percentile_25,
                'median': percentile_50,
                '75th_percentile': percentile_75,
                'mean': np.mean(comp_premiums),
                'min': min(comp_premiums),
                'max': max(comp_premiums)
            },
            'target_position': position,
            'above_median': target_premium > percentile_50,
            'num_comparables': len(comparable_deals)
        }

    def valuation_multiple_comparison(self, deals: List[Deal],
                                     multiple_type: str = 'ev_ebitda') -> Dict[str, Any]:
        """Compare valuation multiples across deals"""

        if multiple_type == 'ev_ebitda':
            multiples = [(d.target_name, d.ev_ebitda) for d in deals if d.ev_ebitda is not None]
        elif multiple_type == 'ev_revenue':
            multiples = [(d.target_name, d.ev_revenue) for d in deals if d.ev_revenue is not None]
        else:
            raise ValueError("Invalid multiple_type. Use 'ev_ebitda' or 'ev_revenue'")

        if not multiples:
            return {'error': f'No {multiple_type} data available for deals'}

        companies, values = zip(*multiples)

        return {
            'multiple_type': multiple_type,
            'deals': [{'company': c, 'multiple': v} for c, v in multiples],
            'statistics': {
                'min': min(values),
                'max': max(values),
                'mean': np.mean(values),
                'median': np.median(values),
                'std': np.std(values)
            },
            'num_deals_with_data': len(multiples)
        }

    def synergy_analysis(self, deals: List[Deal]) -> Dict[str, Any]:
        """Analyze synergies across deals"""

        deals_with_synergies = [d for d in deals if d.synergies is not None]
        with_value = [d for d in deals_with_synergies if d.deal_value]

        if not deals_with_synergies:
            return {'error': 'No synergy data available'}

        synergies = [d.synergies for d in deals_with_synergies]
        synergy_pct = [(d.synergies / d.deal_value * 100) for d in with_value]

        return {
            'deals_with_synergy_data': len(deals_with_synergies),
            'total_synergies': sum(synergies),
            'synergy_statistics': {
                'min': min(synergies),
                'max': max(synergies),
                'mean': np.mean(synergies),
                'median': np.median(synergies)
            },
            'synergy_as_pct_of_deal': {
                'min': min(synergy_pct),
                'max': max(synergy_pct),
                'mean': np.mean(synergy_pct),
                'median': np.median(synergy_pct)
            } if synergy_pct else None
        }


# ── JSON contract (MAAnalyticsService / ma_* MCP tools) ──────────────────────
#
#   deal_comparator.py <command> '<params JSON>'
#
# Deal records (list of objects) -- units, all optional per deal; a field a
# deal lacks is unknown and is skipped by aggregates (never treated as 0):
#   target / target_name, acquirer / acquirer_name, deal_id, industry,
#   announced_date
#   deal_value               amount (any consistent unit across the deals)
#   premium | premium_1day | premium_pct
#                            1-day premium in PERCENT units (45.3 == 45.3 %)
#   ev_revenue, ev_ebitda    multiples (8.7 == 8.7x)
#   synergies                amount (same unit as deal_value)
#   cash_pct | payment_cash_pct, stock_pct | payment_stock_pct
#                            consideration mix in PERCENT units (0-100)
#
# Commands:
#   compare             {deals}
#   rank                {deals, criteria|rank_by, ascending?}
#   benchmark           {target_premium_pct (percent) | target_premium (fraction),
#                        comparables|deals, industry?}
#   payment_structures  {deals}
#   industry            {deals, industry?}

import sys
import sys as _sys
from pathlib import Path as _Path

_sys.path.insert(0, str(_Path(__file__).resolve().parent.parent.parent))
from corporateFinance._cli import (InputError, is_json_call, run_json, has,  # noqa: E402
                                   num, obj_list, text)


def _num(v):
    """Missing/blank -> None (unknown), never 0."""
    if v is None or v == '':
        return None
    f = float(v)
    return f if np.isfinite(f) else None


def _first(d, *keys):
    for k in keys:
        if d.get(k) is not None and d.get(k) != '':
            return d.get(k)
    return None


def _parse_deals(deals_data):
    """Parse deal dicts into Deal objects. Fields the deal record lacks
    stay None so aggregates skip them instead of averaging in zeros."""
    deals = []
    for i, d in enumerate(deals_data):
        if not isinstance(d, dict):
            raise InputError(f"deals[{i}] must be an object")
        deals.append(Deal(
            deal_id=str(d.get('deal_id', '') or ''),
            target_name=str(_first(d, 'target_name', 'target', 'name') or ''),
            acquirer_name=str(_first(d, 'acquirer_name', 'acquirer') or ''),
            deal_value=_num(_first(d, 'deal_value', 'ev')),
            offer_price_per_share=_num(_first(d, 'offer_price_per_share', 'offer_price')),
            premium_1day=_num(_first(d, 'premium_1day', 'premium', 'premium_pct')),
            payment_cash_pct=_num(_first(d, 'payment_cash_pct', 'cash_pct')),
            payment_stock_pct=_num(_first(d, 'payment_stock_pct', 'stock_pct')),
            ev_revenue=_num(d.get('ev_revenue')),
            ev_ebitda=_num(d.get('ev_ebitda')),
            synergies=_num(d.get('synergies')),
            status=str(_first(d, 'status', 'deal_status') or ''),
            industry=str(d.get('industry', '') or ''),
            announced_date=str(_first(d, 'announced_date', 'announcement_date') or ''),
        ))
    return deals


def _known(values):
    return [float(v) for v in values if v is not None]


def _stats(values):
    vals = _known(values)
    if not vals:
        return {'n': 0, 'min': None, 'p25': None, 'median': None, 'mean': None, 'p75': None, 'max': None}
    a = np.array(vals, dtype=float)
    return {
        'n': len(vals),
        'min': float(a.min()),
        'p25': float(np.percentile(a, 25)),
        'median': float(np.median(a)),
        'mean': float(a.mean()),
        'p75': float(np.percentile(a, 75)),
        'max': float(a.max()),
    }


_METRICS = [
    # (attribute, display metric name, output key suffix)
    ('deal_value', 'Deal value', ''),
    ('premium_1day', 'Premium (1-day, %)', '_pct'),
    ('ev_revenue', 'EV/Revenue (x)', '_x'),
    ('ev_ebitda', 'EV/EBITDA (x)', '_x'),
    ('synergies', 'Synergies', ''),
]


def _deal_row(d):
    return {
        'target': d.target_name,
        'acquirer': d.acquirer_name,
        'industry': d.industry or None,
        'deal_value': d.deal_value,
        'premium_pct': d.premium_1day,
        'ev_revenue_x': d.ev_revenue,
        'ev_ebitda_x': d.ev_ebitda,
        'synergies': d.synergies,
        'cash_pct': d.payment_cash_pct,
        'stock_pct': d.payment_stock_pct,
    }


def _deals_arg(p, key='deals', min_len=1):
    return _parse_deals(obj_list(p, key, min_len=min_len))


def cmd_compare(p):
    deals = _deals_arg(p, min_len=2)
    out = {'num_deals': len(deals)}
    stats_rows = []
    for attr, name, suffix in _METRICS:
        s = _stats([getattr(d, attr) for d in deals])
        stats_rows.append({'metric': name, **s})
        base = {'deal_value': 'deal_value', 'premium_1day': 'premium', 'ev_revenue': 'ev_revenue',
                'ev_ebitda': 'ev_ebitda', 'synergies': 'synergies'}[attr]
        if attr == 'synergies':
            known = _known([d.synergies for d in deals])
            out['total_synergies'] = float(sum(known)) if known else None
            continue
        out[f'median_{base}{suffix}'] = s['median']
        out[f'mean_{base}{suffix}'] = s['mean']
    out['deals'] = [_deal_row(d) for d in deals]
    out['statistics'] = stats_rows
    return out


_RANK_ATTR = {
    'premium': ('premium_1day', 'premium_pct'),
    'deal_value': ('deal_value', 'deal_value'),
    'ev_revenue': ('ev_revenue', 'ev_revenue_x'),
    'ev_ebitda': ('ev_ebitda', 'ev_ebitda_x'),
    'synergies': ('synergies', 'synergies'),
}


def cmd_rank(p):
    deals = _deals_arg(p)
    key = 'criteria' if has(p, 'criteria') else 'rank_by'
    criteria = text(p, key, label='criteria', choices=list(_RANK_ATTR))
    ascending = bool(p.get('ascending', False))
    attr, out_key = _RANK_ATTR[criteria]
    with_val = [d for d in deals if getattr(d, attr) is not None]
    without = [d for d in deals if getattr(d, attr) is None]
    with_val.sort(key=lambda d: getattr(d, attr), reverse=not ascending)
    rankings = []
    prev, prev_rank = None, 0
    for i, d in enumerate(with_val, 1):
        v = getattr(d, attr)
        rank = prev_rank if v == prev else i   # ties share a rank
        prev, prev_rank = v, rank
        rankings.append({'rank': rank, 'target': d.target_name, 'acquirer': d.acquirer_name, out_key: v})
    return {
        'ranking_criteria': criteria,
        'order': 'ascending' if ascending else 'descending',
        'num_ranked': len(rankings),
        'num_missing_metric': len(without),
        'rankings': rankings,
        'missing_metric': [{'target': d.target_name, 'acquirer': d.acquirer_name} for d in without],
    }


def _percentile_rank(value, sample):
    """Mid-rank percentile: share of the sample below `value`, counting ties
    as half (0 = below every comparable, 100 = above every comparable)."""
    a = np.array(sample, dtype=float)
    return float((np.sum(a < value) + 0.5 * np.sum(a == value)) / len(a) * 100)


def cmd_benchmark(p):
    if has(p, 'target_premium_pct'):
        target_premium = num(p, 'target_premium_pct', label='target_premium_pct (percent)')
    elif has(p, 'premium_pct'):
        target_premium = num(p, 'premium_pct', label='premium_pct (percent)')
    elif has(p, 'target_premium'):
        # fraction form (0.30 == 30 %)
        target_premium = num(p, 'target_premium', label='target_premium (fraction)') * 100.0
    else:
        raise InputError("Missing required input: target_premium_pct (percent, e.g. 30 for 30%)")
    comps = _deals_arg(p, 'comparables' if p.get('comparables') is not None else 'deals')
    industry = str(p.get('industry') or '').strip()
    if industry:
        comps = [d for d in comps if d.industry.lower() == industry.lower()]
        if not comps:
            raise InputError(f"No comparables in industry '{industry}'")
    prem = _known([d.premium_1day for d in comps])
    if not prem:
        raise InputError("No comparable deal has premium data (premium, percent units)")
    s = _stats(prem)
    if target_premium < s['p25']:
        position = "below 25th percentile"
    elif target_premium < s['median']:
        position = "between 25th and 50th percentile"
    elif target_premium <= s['p75']:
        position = "between 50th and 75th percentile"
    else:
        position = "above 75th percentile"
    return {
        'target_premium_pct': target_premium,
        'median_premium_pct': s['median'],
        'mean_premium_pct': s['mean'],
        'p25_premium_pct': s['p25'],
        'p75_premium_pct': s['p75'],
        'min_premium_pct': s['min'],
        'max_premium_pct': s['max'],
        'percentile_rank_pct': _percentile_rank(target_premium, prem),
        'premium_vs_median_pct': target_premium - s['median'],
        'target_position': position,
        'num_comparables': len(prem),
        'industry_filter': industry or None,
    }


def cmd_payment_structures(p):
    deals = _deals_arg(p)
    known = [d for d in deals if d.payment_cash_pct is not None and d.payment_stock_pct is not None]
    groups = {
        'all_cash': [d for d in known if d.payment_cash_pct >= 100],
        'all_stock': [d for d in known if d.payment_stock_pct >= 100],
        'mixed': [d for d in known if d.payment_cash_pct < 100 and d.payment_stock_pct < 100],
    }
    by_type = []
    for name, g in groups.items():
        by_type.append({
            'type': name,
            'count': len(g),
            'share_of_classified_pct': (len(g) / len(known) * 100) if known else None,
            'avg_premium_pct': _stats([d.premium_1day for d in g])['mean'],
            'avg_deal_value': _stats([d.deal_value for d in g])['mean'],
        })
    return {
        'total_deals': len(deals),
        'all_cash_deals': len(groups['all_cash']),
        'all_stock_deals': len(groups['all_stock']),
        'mixed_deals': len(groups['mixed']),
        'unclassified_deals': len(deals) - len(known),
        'avg_cash_pct': _stats([d.payment_cash_pct for d in deals])['mean'],
        'avg_stock_pct': _stats([d.payment_stock_pct for d in deals])['mean'],
        'by_type': by_type,
    }


def cmd_industry(p):
    deals = _deals_arg(p)
    industry = str(p.get('industry') or '').strip()
    if industry:
        deals = [d for d in deals if d.industry.lower() == industry.lower()]
        if not deals:
            raise InputError(f"No deals in industry '{industry}'")
    groups = {}
    for d in deals:
        groups.setdefault(d.industry or 'Unknown', []).append(d)
    rows = []
    for ind, g in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        vals = _known([d.deal_value for d in g])
        rows.append({
            'industry': ind,
            'count': len(g),
            'total_value': float(sum(vals)) if vals else None,
            'avg_deal_value': _stats(vals)['mean'],
            'median_premium_pct': _stats([d.premium_1day for d in g])['median'],
            'mean_premium_pct': _stats([d.premium_1day for d in g])['mean'],
            'median_ev_revenue_x': _stats([d.ev_revenue for d in g])['median'],
            'median_ev_ebitda_x': _stats([d.ev_ebitda for d in g])['median'],
        })
    return {
        'total_deals': len(deals),
        'num_industries': len(groups),
        'industry_filter': industry or None,
        'by_industry': rows,
    }


JSON_COMMANDS = {
    'compare': cmd_compare,
    'rank': cmd_rank,
    'benchmark': cmd_benchmark,
    'payment_structures': cmd_payment_structures,
    'industry': cmd_industry,
}


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
