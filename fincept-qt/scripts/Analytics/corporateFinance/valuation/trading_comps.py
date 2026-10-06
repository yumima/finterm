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
            print(f"Error fetching {ticker}: {e}")
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

        def calc_stats(values: List[float], name: str) -> Dict[str, float]:
            clean_vals = [v for v in values
                          if v is not None and v > 0 and not np.isnan(v) and not np.isinf(v)]
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
        stats['revenue_growth'] = calc_stats([c.revenue_growth for c in comps], 'rev_growth')
        stats['ebitda_margin'] = calc_stats([c.ebitda_margin for c in comps], 'ebitda_margin')
        stats['net_margin'] = calc_stats([c.net_margin for c in comps], 'net_margin')
        stats['roe'] = calc_stats([c.roe for c in comps], 'roe')

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

        valuation_values = [v for v in valuations.values() if v is not None and v > 0]
        if valuation_values:
            valuations['blended_median'] = median(valuation_values)
            valuations['blended_mean'] = mean(valuation_values)

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

def main():
    """CLI entry point - outputs JSON for C++ integration"""
    import json

    if len(sys.argv) < 2:
        result = {
            "success": False,
            "error": "No command specified. Usage: trading_comps.py <command> [args...]"
        }
        print(json.dumps(result))
        sys.exit(1)

    command = sys.argv[1]
    analyzer = TradingCompsAnalyzer()

    try:
        if command == "trading_comps":
            # Host sends: "trading_comps" target_ticker comp_tickers_json
            if len(sys.argv) < 4:
                raise ValueError("Target ticker and comp tickers required")

            target_ticker = sys.argv[2]
            comp_tickers = json.loads(sys.argv[3])

            # Fetch target data
            target_comp = analyzer.fetch_comp_data(target_ticker)
            target_financials = {}
            if target_comp:
                target_financials = {
                    'revenue': target_comp.revenue_ltm,
                    'ebitda': target_comp.ebitda_ltm,
                    'ebit': target_comp.ebit_ltm,
                    'net_income': target_comp.net_income_ltm
                }

            # Fetch comps
            comps = []
            for ticker in comp_tickers:
                comp = analyzer.fetch_comp_data(ticker)
                if comp and comp.market_cap and comp.market_cap > 0:
                    comps.append(comp)

            if not comps:
                result = {"success": True, "data": {"comparables": [], "comp_count": 0, "target": target_ticker}}
                print(json.dumps(result))
            else:
                comp_table = analyzer.build_comp_table(comps, target_financials if target_financials else None)
                comp_table['target_ticker'] = target_ticker
                result = {"success": True, "data": comp_table}
                print(json.dumps(result, default=str))

        elif command == "find_comps":
            # find_comparables(industry)
            if len(sys.argv) < 3:
                raise ValueError("Industry required")

            industry = sys.argv[2]
            comps = analyzer.find_comparables(industry)

            result = {
                "success": True,
                "data": comps,
                "count": len(comps)
            }
            print(json.dumps(result))

        elif command == "build_table":
            # build_comp_table(industry, target_financials)
            if len(sys.argv) < 4:
                raise ValueError("Industry and target financials required")

            industry = sys.argv[2]
            target_financials = json.loads(sys.argv[3])

            comps = analyzer.find_comparables(industry)
            comp_table = analyzer.build_comp_table(comps, target_financials)

            result = {
                "success": True,
                "data": comp_table
            }
            print(json.dumps(result))

        else:
            result = {
                "success": False,
                "error": f"Unknown command: {command}. Available: trading_comps, find_comps, build_table"
            }
            print(json.dumps(result))
            sys.exit(1)

    except Exception as e:
        result = {
            "success": False,
            "error": str(e),
            "command": command
        }
        print(json.dumps(result))
        sys.exit(1)

if __name__ == '__main__':
    main()
