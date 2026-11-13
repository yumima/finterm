"""
Backtesting.py Data Module

Data helpers: yfinance loading, resample_apply, OHLCV_AGG, TRADES_AGG,
FractionalBacktest, MultiBacktest wrappers.
"""

import pandas as pd
import numpy as np
from typing import Dict, Any, List, Optional


# ============================================================================
# Data Loading
# ============================================================================

def load_yfinance_data(symbol: str, start_date: str = None,
                       end_date: str = None) -> Optional[pd.DataFrame]:
    """Load data via yfinance"""
    try:
        import yfinance as yf
        ticker = yf.Ticker(symbol)
        data = ticker.history(
            start=start_date or '2020-01-01',
            end=end_date or None,
            auto_adjust=True
        )
        if data is not None and len(data) > 0:
            ohlcv = ['Open', 'High', 'Low', 'Close', 'Volume']
            data.columns = [c.capitalize() for c in data.columns]
            available = [c for c in ohlcv if c in data.columns]
            data = data[available]
            # Round to 4 decimal places to eliminate float32 rounding noise
            for col in ['Open', 'High', 'Low', 'Close']:
                if col in data.columns:
                    data[col] = data[col].round(4)
            return data
    except Exception:
        pass
    return None


def load_data(symbol: str, start_date: str = None,
              end_date: str = None) -> pd.DataFrame:
    """
    Load real market data via yfinance.

    Raises RuntimeError naming the symbol when no real data can be obtained;
    never substitutes bundled sample or synthetic data.
    """
    data = load_yfinance_data(symbol, start_date, end_date)
    if data is not None and len(data) > 0:
        return data
    raise RuntimeError(f'Market data unavailable for {symbol}: yfinance returned no data '
                       f'(or yfinance is not installed)')


# ============================================================================
# Resampling
# ============================================================================

def resample_ohlcv(data: pd.DataFrame, rule: str = 'W') -> pd.DataFrame:
    """
    Resample OHLCV data to a different timeframe.
    Wraps backtesting.lib.resample_apply if available.

    Args:
        data: OHLCV DataFrame
        rule: Pandas resample rule ('W', 'M', '4H', etc.)

    Returns:
        Resampled DataFrame
    """
    try:
        from backtesting.lib import resample_apply, OHLCV_AGG
        return resample_apply(rule, data, OHLCV_AGG)
    except (ImportError, TypeError):
        pass

    agg = {
        'Open': 'first',
        'High': 'max',
        'Low': 'min',
        'Close': 'last',
    }
    if 'Volume' in data.columns:
        agg['Volume'] = 'sum'

    return data.resample(rule).agg(agg).dropna()


def get_ohlcv_agg() -> Dict[str, str]:
    """Get OHLCV aggregation rules"""
    try:
        from backtesting.lib import OHLCV_AGG
        return dict(OHLCV_AGG)
    except ImportError:
        return {'Open': 'first', 'High': 'max', 'Low': 'min', 'Close': 'last', 'Volume': 'sum'}


def get_trades_agg() -> Dict[str, str]:
    """Get trades aggregation rules"""
    try:
        from backtesting.lib import TRADES_AGG
        return dict(TRADES_AGG)
    except ImportError:
        return {}


# ============================================================================
# Backtest Variants
# ============================================================================

def run_fractional_backtest(data: pd.DataFrame, strategy_class,
                            cash: float = 10000,
                            commission: float = 0.0,
                            **kwargs) -> Dict[str, Any]:
    """
    Run backtest with fractional share support.
    Uses FractionalBacktest from backtesting.lib if available.
    """
    try:
        from backtesting.lib import FractionalBacktest
        bt = FractionalBacktest(data, strategy_class, cash=cash, commission=commission, **kwargs)
        stats = bt.run()
        return {
            'success': True,
            'message': 'Fractional backtest completed',
            'stats': _stats_summary(stats),
        }
    except ImportError:
        from backtesting import Backtest
        bt = Backtest(data, strategy_class, cash=cash, commission=commission, **kwargs)
        stats = bt.run()
        return {
            'success': True,
            'message': 'Standard backtest (FractionalBacktest not available)',
            'stats': _stats_summary(stats),
        }
    except Exception as e:
        return {'success': False, 'error': str(e)}


def run_multi_backtest(data_dict: Dict[str, pd.DataFrame],
                       strategy_class,
                       cash: float = 10000,
                       commission: float = 0.0) -> Dict[str, Any]:
    """
    Run backtest across multiple symbols/datasets.
    Uses MultiBacktest from backtesting.lib if available.
    """
    try:
        from backtesting.lib import MultiBacktest
        bt = MultiBacktest(list(data_dict.values()), strategy_class,
                           cash=cash, commission=commission)
        stats = bt.run()
        return {
            'success': True,
            'message': 'Multi-backtest completed',
            'stats': _stats_summary(stats),
        }
    except ImportError:
        # Fallback: run individually
        from backtesting import Backtest
        results = {}
        for name, df in data_dict.items():
            try:
                bt = Backtest(df, strategy_class, cash=cash, commission=commission)
                s = bt.run()
                results[name] = _stats_summary(s)
            except Exception as e:
                results[name] = {'error': str(e)}
        return {
            'success': True,
            'message': 'Individual backtests completed (MultiBacktest not available)',
            'results': results,
        }
    except Exception as e:
        return {'success': False, 'error': str(e)}


def _stats_summary(stats) -> Dict[str, Any]:
    """Extract key metrics from stats object"""
    def _sf(key, default=0.0):
        try:
            v = float(stats.get(key, default))
            if np.isnan(v) or np.isinf(v):
                return float(default)
            return v
        except (TypeError, ValueError):
            return float(default)

    return {
        'return_pct': _sf('Return [%]'),
        'sharpe': _sf('Sharpe Ratio'),
        'sortino': _sf('Sortino Ratio'),
        'max_drawdown_pct': _sf('Max. Drawdown [%]'),
        'win_rate_pct': _sf('Win Rate [%]'),
        'profit_factor': _sf('Profit Factor'),
        'num_trades': int(stats.get('# Trades', 0)),
        'equity_final': _sf('Equity Final [$]'),
        'sqn': _sf('SQN'),
    }
