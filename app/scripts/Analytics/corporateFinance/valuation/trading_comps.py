"""Trading Comparables Analysis - Public Company Comps"""
import sys
from pathlib import Path
from typing import Dict, Any, List, Optional
import numpy as np
from dataclasses import dataclass
from statistics import median, mean, stdev
from datetime import datetime

scripts_path = Path(__file__).parent.parent.parent.parent
sys.path.append(str(scripts_path))

try:
    import yfinance as yf
except ImportError:
    yf = None

@dataclass
class TradingComp:
    ticker: str
    company_name: str
    market_cap: float
    enterprise_value: float
    revenue_ltm: float
    ebitda_ltm: float
    ebit_ltm: float
    net_income_ltm: float
    total_debt: float
    cash: float
    shares_outstanding: float
    stock_price: float
    ev_revenue: float
    ev_ebitda: float
    ev_ebit: float
    price_earnings: float
    price_book: float
    price_sales: float
    revenue_growth: float
    ebitda_margin: float
    net_margin: float
    roe: float
    roa: float

class TradingCompsAnalyzer:
    def __init__(self):
        if yf is None:
            raise ImportError("yfinance required: pip install yfinance")

        self.industry_tickers = {
            'Technology': ['AAPL', 'MSFT', 'GOOGL', 'AMZN', 'META', 'NVDA', 'ADBE', 'CRM', 'ORCL', 'IBM'],
            'Healthcare': ['UNH', 'JNJ', 'PFE', 'ABBV', 'TMO', 'ABT', 'DHR', 'BMY', 'LLY', 'AMGN'],
            'Financial Services': ['JPM', 'BAC', 'WFC', 'C', 'GS', 'MS', 'BLK', 'SCHW', 'USB', 'PNC'],
            'Industrials': ['BA', 'HON', 'UNP', 'CAT', 'GE', 'MMM', 'LMT', 'RTX', 'DE', 'EMR'],
            'Consumer Discretionary': ['AMZN', 'TSLA', 'HD', 'NKE', 'MCD', 'LOW', 'SBUX', 'TGT', 'TJX', 'F'],
            'Consumer Staples': ['WMT', 'PG', 'KO', 'PEP', 'COST', 'PM', 'MO', 'CL', 'MDLZ', 'KMB'],
            'Energy': ['XOM', 'CVX', 'COP', 'SLB', 'EOG', 'PXD', 'MPC', 'PSX', 'VLO', 'OXY'],
            'Materials': ['LIN', 'APD', 'SHW', 'ECL', 'DD', 'NEM', 'FCX', 'NUE', 'VMC', 'MLM']
        }

    def fetch_comp_data(self, ticker: str) -> Optional[TradingComp]:
        """Fetch comprehensive financial data for single company"""
        try:
            stock = yf.Ticker(ticker)
            info = stock.info
            financials = stock.financials
            balance_sheet = stock.balance_sheet

            # Missing fields stay None (rendered "—"); ratios are only formed
            # from real inputs. No 0 defaults, and no EBIT := EBITDA swap.
            def num(key):
                v = info.get(key)
                return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else None

            def div(a, b):
                return a / b if a is not None and b else None

            market_cap = num('marketCap')
            shares_outstanding = num('sharesOutstanding')
            stock_price = num('currentPrice')
            if stock_price is None:
                stock_price = num('regularMarketPrice')
            total_debt = num('totalDebt')
            cash = num('totalCash')
            enterprise_value = (market_cap + total_debt - cash
                                if None not in (market_cap, total_debt, cash) else num('enterpriseValue'))

            revenue_ltm = num('totalRevenue')
            ebitda_ltm = num('ebitda')
            # yfinance: 'ebit' is often None; operating income (revenue x
            # operating margin) is the standard EBIT proxy from real data.
            ebit_ltm = num('ebit')
            if ebit_ltm is None and revenue_ltm and num('operatingMargins') is not None:
                ebit_ltm = revenue_ltm * num('operatingMargins')
            # yfinance: 'netIncome' is often None, use 'netIncomeToCommon' or profitMargins
            net_income_ltm = num('netIncome')
            if net_income_ltm is None:
                net_income_ltm = num('netIncomeToCommon')
            if net_income_ltm is None and revenue_ltm and num('profitMargins') is not None:
                net_income_ltm = revenue_ltm * num('profitMargins')

            def pct(key):
                v = num(key)
                return v * 100 if v is not None else None

            def ratio_pct(a, b):
                r = div(a, b)
                return r * 100 if r is not None else None

            revenue_growth = pct('revenueGrowth')
            ebitda_margin = ratio_pct(ebitda_ltm, revenue_ltm)
            net_margin = ratio_pct(net_income_ltm, revenue_ltm)
            roe = pct('returnOnEquity')
            roa = pct('returnOnAssets')

            ev_revenue = div(enterprise_value, revenue_ltm)
            ev_ebitda = div(enterprise_value, ebitda_ltm)
            ev_ebit = div(enterprise_value, ebit_ltm)
            price_earnings = num('trailingPE')
            price_book = num('priceToBook')
            price_sales = num('priceToSalesTrailing12Months')

            return TradingComp(
                ticker=ticker,
                company_name=info.get('longName', ticker),
                market_cap=market_cap,
                enterprise_value=enterprise_value,
                revenue_ltm=revenue_ltm,
                ebitda_ltm=ebitda_ltm,
                ebit_ltm=ebit_ltm,
                net_income_ltm=net_income_ltm,
                total_debt=total_debt,
                cash=cash,
                shares_outstanding=shares_outstanding,
                stock_price=stock_price,
                ev_revenue=ev_revenue,
                ev_ebitda=ev_ebitda,
                ev_ebit=ev_ebit,
                price_earnings=price_earnings,
                price_book=price_book,
                price_sales=price_sales,
                revenue_growth=revenue_growth,
                ebitda_margin=ebitda_margin,
                net_margin=net_margin,
                roe=roe,
                roa=roa
            )

        except Exception as e:
            # stderr: stdout carries the JSON result
            print(f"Error fetching {ticker}: {e}", file=sys.stderr)
            return None

    def find_comparables(self, industry: str, custom_tickers: Optional[List[str]] = None) -> List[TradingComp]:
        """Find public company comparables"""

        tickers = custom_tickers or self.industry_tickers.get(industry, [])

        comps = []
        for ticker in tickers:
            comp = self.fetch_comp_data(ticker)
            if comp and comp.market_cap and comp.market_cap > 0:
                comps.append(comp)

        return comps

    def calculate_statistics(self, comps: List[TradingComp]) -> Dict[str, Dict[str, float]]:
        """Calculate trading multiples statistics"""

        def calc_stats(values: List[float], name: str, positive_only: bool = True) -> Dict[str, float]:
            # Valuation multiples on negative earnings are not meaningful (NM)
            # and are excluded; growth / margin / ROE keep negative values.
            clean_vals = [v for v in values
                          if v is not None and not np.isnan(v) and not np.isinf(v)
                          and (v > 0 or not positive_only)]
            if not clean_vals:
                # No usable comps for this multiple: unavailable, not 0x.
                return {f'{name}_mean': None, f'{name}_median': None, f'{name}_min': None,
                       f'{name}_max': None, f'{name}_std': None, f'{name}_count': 0,
                       f'{name}_q1': None, f'{name}_q3': None}

            return {
                f'{name}_mean': mean(clean_vals),
                f'{name}_median': median(clean_vals),
                f'{name}_min': min(clean_vals),
                f'{name}_max': max(clean_vals),
                f'{name}_std': stdev(clean_vals) if len(clean_vals) > 1 else None,
                f'{name}_count': len(clean_vals),
                f'{name}_q1': np.percentile(clean_vals, 25),
                f'{name}_q3': np.percentile(clean_vals, 75)
            }

        stats = {}

        stats['ev_revenue'] = calc_stats([c.ev_revenue for c in comps], 'ev_revenue')
        stats['ev_ebitda'] = calc_stats([c.ev_ebitda for c in comps], 'ev_ebitda')
        stats['ev_ebit'] = calc_stats([c.ev_ebit for c in comps], 'ev_ebit')
        stats['price_earnings'] = calc_stats([c.price_earnings for c in comps], 'pe')
        stats['price_book'] = calc_stats([c.price_book for c in comps], 'pb')
        stats['price_sales'] = calc_stats([c.price_sales for c in comps], 'ps')
        stats['revenue_growth'] = calc_stats([c.revenue_growth for c in comps], 'rev_growth', False)
        stats['ebitda_margin'] = calc_stats([c.ebitda_margin for c in comps], 'ebitda_margin', False)
        stats['net_margin'] = calc_stats([c.net_margin for c in comps], 'net_margin', False)
        stats['roe'] = calc_stats([c.roe for c in comps], 'roe', False)

        return stats

    def apply_trading_multiples(self, target_financials: Dict[str, float],
                                comps: List[TradingComp]) -> Dict[str, Any]:
        """Apply trading multiples to target"""

        stats = self.calculate_statistics(comps)
        valuations = {}

        def apply(metric, multiple):
            return metric * multiple if multiple is not None else None

        if target_financials.get('revenue'):
            revenue = target_financials['revenue']
            valuations['ev_revenue_median'] = apply(revenue, stats['ev_revenue']['ev_revenue_median'])
            valuations['ev_revenue_mean'] = apply(revenue, stats['ev_revenue']['ev_revenue_mean'])
            valuations['ev_revenue_q1'] = apply(revenue, stats['ev_revenue']['ev_revenue_q1'])
            valuations['ev_revenue_q3'] = apply(revenue, stats['ev_revenue']['ev_revenue_q3'])

        if target_financials.get('ebitda'):
            ebitda = target_financials['ebitda']
            valuations['ev_ebitda_median'] = apply(ebitda, stats['ev_ebitda']['ev_ebitda_median'])
            valuations['ev_ebitda_mean'] = apply(ebitda, stats['ev_ebitda']['ev_ebitda_mean'])
            valuations['ev_ebitda_q1'] = apply(ebitda, stats['ev_ebitda']['ev_ebitda_q1'])
            valuations['ev_ebitda_q3'] = apply(ebitda, stats['ev_ebitda']['ev_ebitda_q3'])

        if target_financials.get('ebit'):
            ebit = target_financials['ebit']
            valuations['ev_ebit_median'] = apply(ebit, stats['ev_ebit']['ev_ebit_median'])
            valuations['ev_ebit_mean'] = apply(ebit, stats['ev_ebit']['ev_ebit_mean'])

        if target_financials.get('net_income'):
            net_income = target_financials['net_income']
            valuations['pe_median'] = apply(net_income, stats['price_earnings']['pe_median'])
            valuations['pe_mean'] = apply(net_income, stats['price_earnings']['pe_mean'])

        # EV multiples imply ENTERPRISE value; P/E implies EQUITY value. Only
        # the EV-based medians are blended (mixing the two was a unit error).
        ev_values = [valuations.get(k) for k in ('ev_revenue_median', 'ev_ebitda_median', 'ev_ebit_median')]
        ev_values = [v for v in ev_values if v is not None and v > 0]
        if ev_values:
            valuations['blended_ev_median'] = median(ev_values)
            valuations['blended_ev_mean'] = mean(ev_values)

        return {
            'valuations': valuations,
            'multiples_used': stats,
            'comp_count': len(comps)
        }

    def build_comp_table(self, comps: List[TradingComp], target_metrics: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """Build formatted trading comp table"""

        def median_or_none(values, scale):
            vals = [v / scale for v in values if v is not None]
            return median(vals) if vals else None

        table_data = []
        for comp in comps:
            row = {
                'Ticker': comp.ticker,
                'Company': comp.company_name,
                'Market Cap ($M)': comp.market_cap / 1_000_000 if comp.market_cap is not None else None,
                'EV ($M)': comp.enterprise_value / 1_000_000 if comp.enterprise_value is not None else None,
                'EV/Revenue': comp.ev_revenue,
                'EV/EBITDA': comp.ev_ebitda,
                'EV/EBIT': comp.ev_ebit,
                'P/E': comp.price_earnings,
                'P/B': comp.price_book,
                'P/S': comp.price_sales,
                'Rev Growth (%)': comp.revenue_growth,
                'EBITDA Margin (%)': comp.ebitda_margin,
                'Net Margin (%)': comp.net_margin,
                'ROE (%)': comp.roe
            }
            table_data.append(row)

        stats = self.calculate_statistics(comps)

        summary_row = {
            'Ticker': 'MEDIAN',
            'Company': '',
            'Market Cap ($M)': median_or_none([c.market_cap for c in comps], 1_000_000),
            'EV ($M)': median_or_none([c.enterprise_value for c in comps], 1_000_000),
            'EV/Revenue': stats['ev_revenue']['ev_revenue_median'],
            'EV/EBITDA': stats['ev_ebitda']['ev_ebitda_median'],
            'EV/EBIT': stats['ev_ebit']['ev_ebit_median'],
            'P/E': stats['price_earnings']['pe_median'],
            'P/B': stats['price_book']['pb_median'],
            'P/S': stats['price_sales']['ps_median'],
            'Rev Growth (%)': stats['revenue_growth']['rev_growth_median'],
            'EBITDA Margin (%)': stats['ebitda_margin']['ebitda_margin_median'],
            'Net Margin (%)': stats['net_margin']['net_margin_median'],
            'ROE (%)': stats['roe']['roe_median']
        }

        result = {
            'comparables': table_data,
            'summary_statistics': {
                'median': summary_row,
                'detailed_stats': stats
            },
            'comp_count': len(comps),
            'as_of_date': datetime.now().strftime('%Y-%m-%d')
        }

        if target_metrics:
            valuation_result = self.apply_trading_multiples(target_metrics, comps)
            result['target_valuation'] = valuation_result

        return result

    def regression_analysis(self, comps: List[TradingComp], x_metric: str = 'ebitda_margin',
                           y_metric: str = 'ev_ebitda') -> Dict[str, Any]:
        """Perform regression analysis on comp set"""

        x_vals = []
        y_vals = []

        for comp in comps:
            x = getattr(comp, x_metric, None)
            y = getattr(comp, y_metric, None)

            if x and y and x > 0 and y > 0 and not np.isnan(x) and not np.isnan(y):
                x_vals.append(x)
                y_vals.append(y)

        if len(x_vals) < 3:
            return {'error': 'Insufficient data for regression'}

        coefficients = np.polyfit(x_vals, y_vals, 1)
        slope, intercept = coefficients

        y_pred = [slope * x + intercept for x in x_vals]
        residuals = [y - yp for y, yp in zip(y_vals, y_pred)]
        ss_res = sum(r**2 for r in residuals)
        ss_tot = sum((y - mean(y_vals))**2 for y in y_vals)
        r_squared = 1 - (ss_res / ss_tot) if ss_tot > 0 else 0

        return {
            'slope': slope,
            'intercept': intercept,
            'r_squared': r_squared,
            'x_metric': x_metric,
            'y_metric': y_metric,
            'data_points': len(x_vals),
            'x_values': x_vals,
            'y_values': y_vals
        }

# ── Implied valuation shared by trading comps and precedent transactions ─────

_MULTIPLE_METRIC = [
    # (label, multiples key, target metric key, kind)
    ('EV/Revenue', 'ev_revenue', 'revenue', 'ev'),
    ('EV/EBITDA', 'ev_ebitda', 'ebitda', 'ev'),
    ('EV/EBIT', 'ev_ebit', 'ebit', 'ev'),
    ('P/E', 'pe', 'net_income', 'equity'),
]


def implied_valuation(multiples: Dict[str, List[Optional[float]]], target: Dict[str, Any]) -> Dict[str, Any]:
    """Apply peer multiples (quartiles) to the target.

    multiples: {'ev_revenue': [...], 'ev_ebitda': [...], 'ev_ebit': [...], 'pe': [...]};
    non-positive / missing peer multiples are NM and excluded.
    target: revenue / ebitda / ebit / net_income, optional net_debt, shares, price.
    EV multiples imply enterprise value -> equity = EV - net debt; P/E implies
    equity value directly -> EV = equity + net debt.
    """
    net_debt = target.get('net_debt')
    shares = target.get('shares')
    price = target.get('price')
    rows = []
    equity_medians = []
    ev_medians = []
    for label, key, metric_key, kind in _MULTIPLE_METRIC:
        vals = [float(v) for v in (multiples.get(key) or []) if v is not None and np.isfinite(v) and v > 0]
        metric = target.get(metric_key)
        row: Dict[str, Any] = {'multiple': label, 'peers_n': len(vals)}
        if not vals:
            row.update({'q1_x': None, 'median_x': None, 'q3_x': None, 'mean_x': None})
        else:
            row.update({'q1_x': float(np.percentile(vals, 25)), 'median_x': float(median(vals)),
                        'q3_x': float(np.percentile(vals, 75)), 'mean_x': float(mean(vals))})
        row['target_metric'] = metric
        if vals and metric is not None and metric > 0:
            lo, mid, hi = metric * row['q1_x'], metric * row['median_x'], metric * row['q3_x']
            if kind == 'ev':
                ev = (lo, mid, hi)
                eq = tuple(x - net_debt for x in ev) if net_debt is not None else (None, None, None)
            else:
                eq = (lo, mid, hi)
                ev = tuple(x + net_debt for x in eq) if net_debt is not None else (None, None, None)
            row.update({'implied_ev_low': ev[0], 'implied_ev_median': ev[1], 'implied_ev_high': ev[2],
                        'implied_equity_low': eq[0], 'implied_equity_median': eq[1], 'implied_equity_high': eq[2]})
            if eq[1] is not None and shares:
                row['implied_per_share_low'] = eq[0] / shares
                row['implied_per_share_median'] = eq[1] / shares
                row['implied_per_share_high'] = eq[2] / shares
                if price:
                    row['vs_current_price_pct'] = (eq[1] / shares / price - 1) * 100
            if ev[1] is not None:
                ev_medians.append(ev[1])
            if eq[1] is not None:
                equity_medians.append(eq[1])
        else:
            row['note'] = 'NM (no positive peer multiples or target metric <= 0)' if vals else 'no peer data'
        rows.append(row)
    out: Dict[str, Any] = {'implied_valuation': rows}
    out['implied_ev_median_of_methods'] = median(ev_medians) if ev_medians else None
    out['implied_equity_median_of_methods'] = median(equity_medians) if equity_medians else None
    if equity_medians and shares:
        out['implied_per_share_median_of_methods'] = median(equity_medians) / shares
    if net_debt is None:
        out['note'] = 'Target net debt unknown: EV-to-equity bridge not applied.'
    return out


def peer_from_inputs(d: Dict[str, Any]) -> Dict[str, Any]:
    """Peer multiples from user-supplied figures: EV = market cap + total debt - cash
    (plus preferred / minority interest when given), unless explicit
    multiples are supplied."""
    def f(k):
        v = d.get(k)
        return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else None
    ev = f('enterprise_value')
    if ev is None and None not in (f('market_cap'), f('total_debt'), f('cash')):
        ev = f('market_cap') + f('total_debt') - f('cash') + (f('preferred_stock') or 0) + (f('minority_interest') or 0)
    def ratio(a, b):
        return a / b if a is not None and b else None
    return {
        'ticker': d.get('ticker') or d.get('name') or '',
        'market_cap': f('market_cap'),
        'enterprise_value': ev,
        'ev_revenue': f('ev_revenue') if f('ev_revenue') is not None else ratio(ev, f('revenue')),
        'ev_ebitda': f('ev_ebitda') if f('ev_ebitda') is not None else ratio(ev, f('ebitda')),
        'ev_ebit': f('ev_ebit') if f('ev_ebit') is not None else ratio(ev, f('ebit')),
        'pe': f('pe') if f('pe') is not None else ratio(f('market_cap'), f('net_income')),
    }


def _json_calculate(p: Dict[str, Any]) -> Dict[str, Any]:
    """Two input modes (can't mix):
      live:   target_ticker + comp_tickers (comma list) -> figures from yfinance
      manual: peers [{ticker, market_cap, total_debt, cash, revenue, ebitda,
              ebit, net_income} or explicit ev_revenue/ev_ebitda/ev_ebit/pe]
              + target_revenue / target_ebitda / target_ebit / target_net_income,
              optional target_net_debt, target_shares, target_price.
    """
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
    from corporateFinance._cli import has, InputError, num, opt_num
    if isinstance(p.get('peers'), list) and p['peers']:
        peers = [peer_from_inputs(d) for d in p['peers'] if isinstance(d, dict)]
        target = {
            'revenue': opt_num(p, 'target_revenue'), 'ebitda': opt_num(p, 'target_ebitda'),
            'ebit': opt_num(p, 'target_ebit'), 'net_income': opt_num(p, 'target_net_income'),
            'net_debt': opt_num(p, 'target_net_debt'), 'shares': opt_num(p, 'target_shares', gt=0),
            'price': opt_num(p, 'target_price', gt=0),
        }
        if all(target[k] is None for k in ('revenue', 'ebitda', 'ebit', 'net_income')):
            raise InputError("Enter at least one target metric (target_revenue / target_ebitda / target_ebit / target_net_income)")
        source = 'user-supplied figures'
        target_label = str(p.get('target_name') or 'Target')
        table = [{'ticker': x['ticker'], 'market_cap': x['market_cap'], 'enterprise_value': x['enterprise_value'],
                  'ev_revenue_x': x['ev_revenue'], 'ev_ebitda_x': x['ev_ebitda'], 'ev_ebit_x': x['ev_ebit'],
                  'pe_x': x['pe']} for x in peers]
    else:
        tt = str(p.get('target_ticker') or '').strip().upper()
        raw = p.get('comp_tickers')
        tickers = raw if isinstance(raw, list) else str(raw or '').replace(';', ',').split(',')
        tickers = [str(t).strip().upper() for t in tickers if str(t).strip()]
        tickers = [t for t in dict.fromkeys(tickers) if t != tt]  # target never in its own peer set
        if not tt:
            raise InputError("Missing required input: target_ticker")
        if not tickers:
            raise InputError("Missing required input: comp_tickers (comma-separated peers)")
        analyzer = TradingCompsAnalyzer()
        tc = analyzer.fetch_comp_data(tt)
        if tc is None:
            raise InputError(f"No market data for target {tt}")
        comps = [c for c in (analyzer.fetch_comp_data(t) for t in tickers) if c and c.market_cap]
        missing = [t for t in tickers if t not in {c.ticker for c in comps}]
        if not comps:
            raise InputError("No market data for any comparable: " + ', '.join(tickers))
        peers = [{'ticker': c.ticker, 'ev_revenue': c.ev_revenue, 'ev_ebitda': c.ev_ebitda,
                  'ev_ebit': c.ev_ebit, 'pe': c.price_earnings} for c in comps]
        net_debt = (tc.total_debt - tc.cash) if None not in (tc.total_debt, tc.cash) else None
        target = {'revenue': tc.revenue_ltm, 'ebitda': tc.ebitda_ltm, 'ebit': tc.ebit_ltm,
                  'net_income': tc.net_income_ltm, 'net_debt': net_debt,
                  'shares': tc.shares_outstanding, 'price': tc.stock_price}
        source = 'yfinance (LTM)'
        target_label = tt
        table = [{'ticker': c.ticker, 'company': c.company_name, 'market_cap': c.market_cap,
                  'total_debt': c.total_debt, 'cash': c.cash, 'enterprise_value': c.enterprise_value,
                  'ev_revenue_x': c.ev_revenue, 'ev_ebitda_x': c.ev_ebitda, 'ev_ebit_x': c.ev_ebit,
                  'pe_x': c.price_earnings, 'revenue_growth_pct': c.revenue_growth,
                  'ebitda_margin_pct': c.ebitda_margin} for c in comps]
    if not peers:
        raise InputError("No comparable companies")
    mult = {k: [x[k] for x in peers] for k in ('ev_revenue', 'ev_ebitda', 'ev_ebit', 'pe')}
    imp = implied_valuation(mult, target)
    med = {'ticker': 'MEDIAN'}
    for k in ('ev_revenue', 'ev_ebitda', 'ev_ebit', 'pe'):
        vals = [v for v in mult[k] if v is not None and v > 0]
        med[k + '_x'] = median(vals) if vals else None
    out = {
        'target': target_label,
        'comp_count': len(peers),
        'source': source,
        'median_ev_ebitda_x': med['ev_ebitda_x'],
        'median_ev_revenue_x': med['ev_revenue_x'],
        'median_pe_x': med['pe_x'],
        'implied_ev_median_of_methods': imp['implied_ev_median_of_methods'],
        'implied_equity_median_of_methods': imp['implied_equity_median_of_methods'],
        'implied_per_share_median_of_methods': imp.get('implied_per_share_median_of_methods'),
        'target_net_debt': target.get('net_debt'),
        'target_price': target.get('price'),
        'comparables': table + [med],
        'implied_valuation': imp['implied_valuation'],
        'method': 'EV = market cap + total debt - cash; non-positive multiples are NM and excluded; '
                  'EV multiples -> EV -> equity via target net debt; P/E -> equity directly.',
    }
    if source.startswith('yfinance'):
        out['as_of_date'] = datetime.now().strftime('%Y-%m-%d')
        if missing:
            out['comps_without_data'] = ', '.join(missing)
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
