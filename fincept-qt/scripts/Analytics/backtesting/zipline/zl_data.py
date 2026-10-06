"""
Zipline Data Ingestion

Fetches data via yfinance and converts to pandas DataFrames suitable for
Zipline's run_algorithm(). Uses a custom data source approach to avoid
bcolz bundle ingestion.
"""

import sys
import numpy as np
import pandas as pd
from datetime import datetime, timedelta
from typing import Dict, Any, List, Optional, Tuple


def fetch_yfinance_data(
    symbols: List[str],
    start_date: str,
    end_date: str,
    timeframe: str = '1d',
) -> Dict[str, pd.DataFrame]:
    """
    Fetch OHLCV data via yfinance for each symbol.

    Returns:
        Dict mapping symbol -> DataFrame with columns:
        [open, high, low, close, volume] (lowercase, tz-aware UTC index)

    Raises:
        RuntimeError naming the symbol when real data cannot be obtained
        (never substitutes synthetic prices).
    """
    try:
        import yfinance as yf
    except ImportError:
        raise RuntimeError(f'Market data unavailable for {", ".join(symbols)}: '
                           f'yfinance is not installed')

    result = {}
    for sym in symbols:
        try:
            print(f'[ZL-DATA] Fetching {sym} from {start_date} to {end_date}', file=sys.stderr)
            ticker = yf.Ticker(sym)
            df = ticker.history(start=start_date, end=end_date, interval=timeframe)
        except Exception as e:
            raise RuntimeError(f'Market data unavailable for {sym}: {e}') from e

        if df.empty:
            raise RuntimeError(f'Market data unavailable for {sym}: yfinance returned no data '
                               f'for {start_date} to {end_date}')

        # Normalize columns to lowercase
        df.columns = [c.lower().replace(' ', '_') for c in df.columns]

        # Keep only OHLCV
        keep = ['open', 'high', 'low', 'close', 'volume']
        available = [c for c in keep if c in df.columns]
        df = df[available].copy()

        # Ensure timezone-aware UTC index
        if df.index.tz is None:
            df.index = df.index.tz_localize('UTC')
        else:
            df.index = df.index.tz_convert('UTC')

        df.index.name = 'date'
        df = df.dropna()
        if df.empty:
            raise RuntimeError(f'Market data unavailable for {sym}: all bars incomplete (NaN)')
        # Round to 4 decimal places to eliminate float32 rounding noise
        for col in ['open', 'high', 'low', 'close']:
            if col in df.columns:
                df[col] = df[col].round(4)
        result[sym] = df
        print(f'[ZL-DATA] {sym}: {len(df)} bars loaded', file=sys.stderr)

    return result


def prepare_zipline_data(
    data: Dict[str, pd.DataFrame],
) -> Tuple[pd.DataFrame, List[str]]:
    """
    Merge per-symbol DataFrames into a single panel-like DataFrame for Zipline.

    Returns:
        (merged_df, symbols) where merged_df has MultiIndex columns (field, symbol)
    """
    if len(data) == 1:
        sym = list(data.keys())[0]
        df = data[sym].copy()
        # Create MultiIndex columns: (field, symbol)
        df.columns = pd.MultiIndex.from_tuples(
            [(col, sym) for col in df.columns],
            names=['field', 'symbol']
        )
        return df, [sym]

    # Multi-symbol: align on common dates
    frames = {}
    for sym, df in data.items():
        for col in df.columns:
            frames[(col, sym)] = df[col]

    merged = pd.DataFrame(frames)
    merged.columns = pd.MultiIndex.from_tuples(
        merged.columns, names=['field', 'symbol']
    )
    merged = merged.dropna()
    symbols = list(data.keys())
    return merged, symbols


def data_to_records(data: Dict[str, pd.DataFrame]) -> List[Dict[str, Any]]:
    """Convert data to list-of-dicts for JSON output (get_historical_data command)."""
    records = []
    for sym, df in data.items():
        for idx, row in df.iterrows():
            rec = {
                'symbol': sym,
                'date': idx.strftime('%Y-%m-%d'),
                'open': float(row.get('open', 0)),
                'high': float(row.get('high', 0)),
                'low': float(row.get('low', 0)),
                'close': float(row.get('close', 0)),
                'volume': float(row.get('volume', 0)),
            }
            records.append(rec)
    return records
