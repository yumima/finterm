"""
BT Data Loading

Fetches data via yfinance and converts to pandas DataFrames suitable for
bt.Backtest(). Raises RuntimeError naming the symbol(s) when real data is
unavailable -- never substitutes synthetic prices.
"""

import sys
import numpy as np
import pandas as pd
from datetime import datetime, timedelta
from typing import Dict, Any, List, Optional


def _import_yfinance(symbols: List[str]):
    try:
        import yfinance as yf
        return yf
    except ImportError:
        raise RuntimeError(f'Market data unavailable for {", ".join(symbols)}: '
                           f'yfinance is not installed')


def fetch_data(
    symbols: List[str],
    start_date: str,
    end_date: str,
    timeframe: str = '1d',
) -> pd.DataFrame:
    """
    Fetch close-price data for bt.

    bt expects a DataFrame with DatetimeIndex and one column per symbol
    (close prices). Returns that format directly.

    Raises RuntimeError if any symbol has no real data.
    """
    yf = _import_yfinance(symbols)

    frames = {}
    for sym in symbols:
        try:
            print(f'[BT-DATA] Fetching {sym} from {start_date} to {end_date}', file=sys.stderr)
            ticker = yf.Ticker(sym)
            df = ticker.history(start=start_date, end=end_date, interval=timeframe)
        except Exception as e:
            raise RuntimeError(f'Market data unavailable for {sym}: {e}') from e

        if df.empty:
            raise RuntimeError(f'Market data unavailable for {sym}: yfinance returned no data '
                               f'for {start_date} to {end_date}')

        # Normalize columns
        df.columns = [c.lower().replace(' ', '_') for c in df.columns]

        if df.index.tz is None:
            df.index = df.index.tz_localize('UTC')
        else:
            df.index = df.index.tz_convert('UTC')

        # Round to 4 decimal places to eliminate float32 rounding noise
        # from Yahoo Finance API (slightly different values across requests)
        frames[sym] = df['close'].round(4)
        print(f'[BT-DATA] {sym}: {len(df)} bars loaded', file=sys.stderr)

    result = pd.DataFrame(frames).dropna()
    if result.empty:
        raise RuntimeError(f'Market data unavailable: no overlapping dates for '
                           f'{", ".join(symbols)} between {start_date} and {end_date}')
    return result


def fetch_ohlcv(
    symbols: List[str],
    start_date: str,
    end_date: str,
    timeframe: str = '1d',
) -> Dict[str, pd.DataFrame]:
    """
    Fetch full OHLCV data per symbol (for indicators that need H/L/V).

    Raises RuntimeError if any symbol has no real data.
    """
    yf = _import_yfinance(symbols)

    result = {}
    for sym in symbols:
        try:
            ticker = yf.Ticker(sym)
            df = ticker.history(start=start_date, end=end_date, interval=timeframe)
        except Exception as e:
            raise RuntimeError(f'Market data unavailable for {sym}: {e}') from e
        if df.empty:
            raise RuntimeError(f'Market data unavailable for {sym}: yfinance returned no data '
                               f'for {start_date} to {end_date}')
        df.columns = [c.lower().replace(' ', '_') for c in df.columns]
        keep = ['open', 'high', 'low', 'close', 'volume']
        available = [c for c in keep if c in df.columns]
        df = df[available].copy()
        if df.index.tz is None:
            df.index = df.index.tz_localize('UTC')
        else:
            df.index = df.index.tz_convert('UTC')
        df.index.name = 'date'
        df = df.dropna()
        # Round to 4 decimal places to eliminate float32 rounding noise
        for col in ['open', 'high', 'low', 'close']:
            if col in df.columns:
                df[col] = df[col].round(4)
        result[sym] = df
    return result


def data_to_records(data: pd.DataFrame) -> List[Dict[str, Any]]:
    """Convert close-price DataFrame to list-of-dicts for JSON output."""
    records = []
    for idx, row in data.iterrows():
        date_str = idx.strftime('%Y-%m-%d') if hasattr(idx, 'strftime') else str(idx)
        for col in data.columns:
            records.append({
                'symbol': col,
                'date': date_str,
                'close': float(row[col]),
            })
    return records
