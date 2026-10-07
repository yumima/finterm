"""Precedent Transaction Analysis"""
import sys
from pathlib import Path
from typing import Dict, Any, List, Optional, Tuple
import numpy as np
from datetime import datetime, timedelta
from dataclasses import dataclass
from statistics import median, mean, stdev

scripts_path = Path(__file__).parent.parent.parent.parent
sys.path.append(str(scripts_path))

# Add Analytics path for absolute imports
analytics_path = Path(__file__).parent.parent.parent
sys.path.insert(0, str(analytics_path))

# Use absolute imports instead of relative imports
from corporateFinance.deal_database.database_schema import MADatabase

@dataclass
class TransactionComp:
    deal_id: str
    announcement_date: str
    acquirer_name: str
    target_name: str
    deal_value: float
    enterprise_value: float
    revenue: float
    ebitda: float
    ev_revenue: float
    ev_ebitda: float
    ev_ebit: float
    price_earnings: float
    premium_1day: float
    premium_4week: float
    payment_method: str
    deal_status: str

class PrecedentTransactionAnalyzer:
    def __init__(self, db: Optional[MADatabase] = None):
        self.db = db or MADatabase()

    def find_comparables(self, target_metrics: Dict[str, Any],
                        screening_criteria: Optional[Dict[str, Any]] = None) -> List[TransactionComp]:
        """Find comparable M&A transactions"""

        industry = target_metrics.get('industry')
        target_value = target_metrics.get('deal_value', target_metrics.get('enterprise_value', 0))

        criteria = screening_criteria or {}
        years_back = criteria.get('years_back', 3)
        min_value_ratio = criteria.get('min_value_ratio', 0.5)
        max_value_ratio = criteria.get('max_value_ratio', 2.0)
        status_filter = criteria.get('status', 'Completed')

        end_date = datetime.now()
        start_date = end_date - timedelta(days=years_back*365)

        min_value = target_value * min_value_ratio if target_value else 0
        max_value = target_value * max_value_ratio if target_value else float('inf')

        deals = self.db.search_deals({
            'industry': industry,
            'start_date': start_date.strftime('%Y-%m-%d'),
            'end_date': end_date.strftime('%Y-%m-%d'),
            'deal_status': status_filter
        })

        if target_value:
            deals = [d for d in deals if min_value <= d.get('deal_value', 0) <= max_value]

        comps = []
        for deal in deals:
            financials = self.db.get_deal_financials(deal['deal_id'])
            multiples = self.db.get_deal_multiples(deal['deal_id'])

            if not financials or not multiples:
                continue

            fin = financials[0] if financials else {}
            mult = multiples[0] if multiples else {}

            comp = TransactionComp(
                deal_id=deal['deal_id'],
                announcement_date=deal['announcement_date'],
                acquirer_name=deal['acquirer_name'],
                target_name=deal['target_name'],
                deal_value=deal.get('deal_value', 0),
                enterprise_value=deal.get('enterprise_value', deal.get('deal_value', 0)),
                revenue=fin.get('revenue', 0),
                ebitda=fin.get('ebitda', 0),
                ev_revenue=mult.get('ev_revenue', 0),
                ev_ebitda=mult.get('ev_ebitda', 0),
                ev_ebit=mult.get('ev_ebit', 0),
                price_earnings=mult.get('price_earnings', 0),
                premium_1day=deal.get('premium_1day', 0),
                premium_4week=deal.get('premium_4week', 0),
                payment_method=deal.get('payment_method', ''),
                deal_status=deal.get('deal_status', '')
            )
            comps.append(comp)

        comps.sort(key=lambda x: x.announcement_date, reverse=True)
        return comps

    def calculate_multiples_statistics(self, comps: List[TransactionComp]) -> Dict[str, Dict[str, float]]:
        """Calculate statistical metrics for transaction multiples"""

        def calc_stats(values: List[float], name: str) -> Dict[str, float]:
            clean_vals = [v for v in values if v > 0 and not np.isnan(v) and not np.isinf(v)]
            if not clean_vals:
                return {f'{name}_mean': None, f'{name}_median': None, f'{name}_min': None,
                       f'{name}_max': None, f'{name}_std': None, f'{name}_count': 0,
                       f'{name}_q1': None, f'{name}_q3': None}

            return {
                f'{name}_mean': mean(clean_vals),
                f'{name}_median': median(clean_vals),
                f'{name}_min': min(clean_vals),
                f'{name}_max': max(clean_vals),
                f'{name}_std': stdev(clean_vals) if len(clean_vals) > 1 else 0,
                f'{name}_count': len(clean_vals),
                f'{name}_q1': np.percentile(clean_vals, 25),
                f'{name}_q3': np.percentile(clean_vals, 75)
            }

        stats = {}

        ev_revenue_vals = [c.ev_revenue for c in comps if c.ev_revenue > 0]
        stats['ev_revenue'] = calc_stats(ev_revenue_vals, 'ev_revenue')

        ev_ebitda_vals = [c.ev_ebitda for c in comps if c.ev_ebitda > 0]
        stats['ev_ebitda'] = calc_stats(ev_ebitda_vals, 'ev_ebitda')

        ev_ebit_vals = [c.ev_ebit for c in comps if c.ev_ebit > 0]
        stats['ev_ebit'] = calc_stats(ev_ebit_vals, 'ev_ebit')

        pe_vals = [c.price_earnings for c in comps if c.price_earnings > 0]
        stats['price_earnings'] = calc_stats(pe_vals, 'pe')

        premium_1d_vals = [c.premium_1day for c in comps if c.premium_1day > 0]
        stats['premium_1day'] = calc_stats(premium_1d_vals, 'premium_1d')

        premium_4w_vals = [c.premium_4week for c in comps if c.premium_4week > 0]
        stats['premium_4week'] = calc_stats(premium_4w_vals, 'premium_4w')

        return stats

    def apply_multiples_valuation(self, target_financials: Dict[str, float],
                                  comps: List[TransactionComp]) -> Dict[str, Any]:
        """Apply transaction multiples to target company"""

        stats = self.calculate_multiples_statistics(comps)

        valuations = {}

        if target_financials.get('revenue'):
            revenue = target_financials['revenue']
            if (stats['ev_revenue']['ev_revenue_median'] or 0) > 0:
                valuations['ev_revenue_median'] = revenue * stats['ev_revenue']['ev_revenue_median']
                valuations['ev_revenue_mean'] = revenue * stats['ev_revenue']['ev_revenue_mean']
                valuations['ev_revenue_q1'] = revenue * stats['ev_revenue']['ev_revenue_q1']
                valuations['ev_revenue_q3'] = revenue * stats['ev_revenue']['ev_revenue_q3']

        if target_financials.get('ebitda'):
            ebitda = target_financials['ebitda']
            if (stats['ev_ebitda']['ev_ebitda_median'] or 0) > 0:
                valuations['ev_ebitda_median'] = ebitda * stats['ev_ebitda']['ev_ebitda_median']
                valuations['ev_ebitda_mean'] = ebitda * stats['ev_ebitda']['ev_ebitda_mean']
                valuations['ev_ebitda_q1'] = ebitda * stats['ev_ebitda']['ev_ebitda_q1']
                valuations['ev_ebitda_q3'] = ebitda * stats['ev_ebitda']['ev_ebitda_q3']

        if target_financials.get('ebit'):
            ebit = target_financials['ebit']
            if (stats['ev_ebit']['ev_ebit_median'] or 0) > 0:
                valuations['ev_ebit_median'] = ebit * stats['ev_ebit']['ev_ebit_median']
                valuations['ev_ebit_mean'] = ebit * stats['ev_ebit']['ev_ebit_mean']

        if target_financials.get('net_income'):
            net_income = target_financials['net_income']
            if (stats['price_earnings']['pe_median'] or 0) > 0:
                valuations['pe_median'] = net_income * stats['price_earnings']['pe_median']
                valuations['pe_mean'] = net_income * stats['price_earnings']['pe_mean']

        valuation_values = [v for v in valuations.values() if v > 0]
        if valuation_values:
            valuations['blended_median'] = median(valuation_values)
            valuations['blended_mean'] = mean(valuation_values)

        return {
            'valuations': valuations,
            'multiples_used': stats,
            'comp_count': len(comps)
        }

    def build_comp_table(self, comps: List[TransactionComp], target_metrics: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """Build formatted transaction comp table"""

        table_data = []
        for comp in comps:
            row = {
                'Date': comp.announcement_date,
                'Acquirer': comp.acquirer_name,
                'Target': comp.target_name,
                'Deal Value ($M)': comp.deal_value / 1_000_000 if comp.deal_value else 0,
                'EV/Revenue': comp.ev_revenue,
                'EV/EBITDA': comp.ev_ebitda,
                'EV/EBIT': comp.ev_ebit,
                'P/E': comp.price_earnings,
                'Premium 1-Day (%)': comp.premium_1day,
                'Premium 4-Week (%)': comp.premium_4week,
                'Payment': comp.payment_method
            }
            table_data.append(row)

        stats = self.calculate_multiples_statistics(comps)

        summary_row = {
            'Date': 'MEDIAN',
            'Acquirer': '',
            'Target': '',
            'Deal Value ($M)': median([c.deal_value / 1_000_000 for c in comps if c.deal_value]) if comps else 0,
            'EV/Revenue': stats['ev_revenue']['ev_revenue_median'],
            'EV/EBITDA': stats['ev_ebitda']['ev_ebitda_median'],
            'EV/EBIT': stats['ev_ebit']['ev_ebit_median'],
            'P/E': stats['price_earnings']['pe_median'],
            'Premium 1-Day (%)': stats['premium_1day']['premium_1d_median'],
            'Premium 4-Week (%)': stats['premium_4week']['premium_4w_median'],
            'Payment': ''
        }

        mean_row = {
            'Date': 'MEAN',
            'Acquirer': '',
            'Target': '',
            'Deal Value ($M)': mean([c.deal_value / 1_000_000 for c in comps if c.deal_value]) if comps else 0,
            'EV/Revenue': stats['ev_revenue']['ev_revenue_mean'],
            'EV/EBITDA': stats['ev_ebitda']['ev_ebitda_mean'],
            'EV/EBIT': stats['ev_ebit']['ev_ebit_mean'],
            'P/E': stats['price_earnings']['pe_mean'],
            'Premium 1-Day (%)': stats['premium_1day']['premium_1d_mean'],
            'Premium 4-Week (%)': stats['premium_4week']['premium_4w_mean'],
            'Payment': ''
        }

        result = {
            'comparables': table_data,
            'summary_statistics': {
                'median': summary_row,
                'mean': mean_row,
                'quartiles': {
                    'ev_revenue_q1': stats['ev_revenue']['ev_revenue_q1'],
                    'ev_revenue_q3': stats['ev_revenue']['ev_revenue_q3'],
                    'ev_ebitda_q1': stats['ev_ebitda']['ev_ebitda_q1'],
                    'ev_ebitda_q3': stats['ev_ebitda']['ev_ebitda_q3']
                }
            },
            'comp_count': len(comps)
        }

        if target_metrics:
            valuation_result = self.apply_multiples_valuation(target_metrics, comps)
            result['target_valuation'] = valuation_result

        return result

    def sensitivity_analysis(self, target_financials: Dict[str, float],
                           comps: List[TransactionComp],
                           metric: str = 'ebitda',
                           multiple_range: Tuple[float, float] = None) -> Dict[str, Any]:
        """Perform sensitivity analysis on multiples"""

        stats = self.calculate_multiples_statistics(comps)

        if metric == 'ebitda':
            base_multiple = stats['ev_ebitda']['ev_ebitda_median']
            metric_value = target_financials.get('ebitda', 0)
            std = stats['ev_ebitda']['ev_ebitda_std']
        elif metric == 'revenue':
            base_multiple = stats['ev_revenue']['ev_revenue_median']
            metric_value = target_financials.get('revenue', 0)
            std = stats['ev_revenue']['ev_revenue_std']
        else:
            return {}

        if not metric_value or not base_multiple:
            return {}

        if multiple_range:
            min_mult, max_mult = multiple_range
        else:
            min_mult = max(0.1, base_multiple - 2*std)
            max_mult = base_multiple + 2*std

        multiples = np.linspace(min_mult, max_mult, 11)
        metric_adjustments = np.linspace(0.8, 1.2, 11)

        sensitivity_grid = []
        for mult in multiples:
            row = []
            for adj in metric_adjustments:
                valuation = (metric_value * adj) * mult
                row.append(valuation)
            sensitivity_grid.append(row)

        return {
            'base_multiple': base_multiple,
            'base_metric_value': metric_value,
            'base_valuation': metric_value * base_multiple,
            'multiples_range': multiples.tolist(),
            'metric_adjustments': metric_adjustments.tolist(),
            'sensitivity_matrix': sensitivity_grid,
            'metric_type': metric
        }

    def calculate_implied_premium(self, current_price: float, shares_outstanding: float,
                                 implied_enterprise_value: float, net_debt: float) -> Dict[str, float]:
        """Calculate implied acquisition premium"""

        current_market_cap = current_price * shares_outstanding
        implied_equity_value = implied_enterprise_value - net_debt
        implied_price_per_share = implied_equity_value / shares_outstanding

        premium = ((implied_price_per_share - current_price) / current_price) * 100

        return {
            'current_price': current_price,
            'current_market_cap': current_market_cap,
            'implied_enterprise_value': implied_enterprise_value,
            'implied_equity_value': implied_equity_value,
            'implied_price_per_share': implied_price_per_share,
            'implied_premium_pct': premium,
            'premium_dollars': implied_price_per_share - current_price
        }

# ── JSON contract (MAAnalyticsService "calculate" / ma_precedent_transactions)

def _json_calculate(p: Dict[str, Any]) -> Dict[str, Any]:
    """Value a target off user-supplied precedent deals (no database access).

    transactions: [{target, acquirer, date, enterprise_value, revenue, ebitda,
                    ebit?, ev_revenue?, ev_ebitda?, ev_ebit?, premium_1day_pct?}]
      Multiples are computed from enterprise_value / metric unless given
      explicitly. A deal's equity purchase price is NOT an EV: supply EV or
      the multiples.
    target: target_revenue / target_ebitda / target_ebit, optional
      target_net_debt, target_shares, target_price.
    Precedent multiples already embed the control premium paid, so no
    separate control premium is added.
    """
    from corporateFinance._cli import obj_list, opt_num, InputError
    from corporateFinance.valuation.trading_comps import implied_valuation
    deals = obj_list(p, 'transactions', label='transactions (list of precedent deals)')

    def f(d, k):
        v = d.get(k)
        return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else None

    rows = []
    mult = {'ev_revenue': [], 'ev_ebitda': [], 'ev_ebit': [], 'pe': []}
    premiums = []
    for d in deals:
        ev = f(d, 'enterprise_value') if f(d, 'enterprise_value') is not None else f(d, 'ev')
        def m(explicit, metric):
            if f(d, explicit) is not None:
                return f(d, explicit)
            return ev / f(d, metric) if ev is not None and f(d, metric) else None
        r = {'target': d.get('target') or d.get('target_name') or '',
             'acquirer': d.get('acquirer') or d.get('acquirer_name') or '',
             'date': d.get('date') or d.get('announcement_date') or '',
             'enterprise_value': ev,
             'ev_revenue_x': m('ev_revenue', 'revenue'),
             'ev_ebitda_x': m('ev_ebitda', 'ebitda'),
             'ev_ebit_x': m('ev_ebit', 'ebit'),
             'premium_1day_pct': f(d, 'premium_1day_pct')}
        rows.append(r)
        mult['ev_revenue'].append(r['ev_revenue_x'])
        mult['ev_ebitda'].append(r['ev_ebitda_x'])
        mult['ev_ebit'].append(r['ev_ebit_x'])
        if r['premium_1day_pct'] is not None:
            premiums.append(r['premium_1day_pct'])
    if all(v is None for k in mult for v in mult[k]):
        raise InputError("No usable multiples: give each transaction an enterprise_value with revenue/EBITDA, or the multiples")
    target = {'revenue': opt_num(p, 'target_revenue'), 'ebitda': opt_num(p, 'target_ebitda'),
              'ebit': opt_num(p, 'target_ebit'), 'net_income': None,
              'net_debt': opt_num(p, 'target_net_debt'), 'shares': opt_num(p, 'target_shares', gt=0),
              'price': opt_num(p, 'target_price', gt=0)}
    if all(target[k] is None for k in ('revenue', 'ebitda', 'ebit')):
        raise InputError("Enter at least one target metric (target_revenue / target_ebitda / target_ebit)")
    imp = implied_valuation(mult, target)
    imp['implied_valuation'] = [r for r in imp['implied_valuation'] if r['multiple'] != 'P/E']
    out = {
        'transaction_count': len(rows),
        'median_ev_ebitda_x': next((r['median_x'] for r in imp['implied_valuation'] if r['multiple'] == 'EV/EBITDA'), None),
        'median_ev_revenue_x': next((r['median_x'] for r in imp['implied_valuation'] if r['multiple'] == 'EV/Revenue'), None),
        'median_premium_1day_pct': median(premiums) if premiums else None,
        'implied_ev_median_of_methods': imp['implied_ev_median_of_methods'],
        'implied_equity_median_of_methods': imp['implied_equity_median_of_methods'],
        'implied_per_share_median_of_methods': imp.get('implied_per_share_median_of_methods'),
        'transactions': rows,
        'implied_valuation': imp['implied_valuation'],
        'method': 'Multiples = deal EV / target LTM metric; non-positive multiples excluded; quartiles across '
                  'deals; control premium is already embedded in precedent multiples.',
    }
    if imp.get('note'):
        out['note'] = imp['note']
    return out


JSON_COMMANDS = {'calculate': _json_calculate}


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
