"""
Volatility Indicators Module
Provides all volatility-based technical indicators from the ta library
"""

import pandas as pd

from ._safe import safe_assign
from ta.volatility import (
    AverageTrueRange,
    BollingerBands,
    KeltnerChannel,
    DonchianChannel,
    UlcerIndex,
)


def calculate_atr(df, window=14, fillna=False):
    """
    Calculate Average True Range (ATR)

    Args:
        df: DataFrame with 'high', 'low', 'close' columns
        window: Period for ATR calculation (default: 14)
        fillna: Fill NaN values (default: False)

    Returns:
        Series with ATR values
    """
    indicator = AverageTrueRange(
        high=df['high'],
        low=df['low'],
        close=df['close'],
        window=window,
        fillna=fillna
    )
    atr = indicator.average_true_range()
    # ta emits 0.0 (not NaN) through the Wilder warm-up, so the C++ side's
    # "NaN = not yet available" contract silently broke: a freshly listed
    # stock displayed ATR 0.0000 as if it were a reading, and the
    # sufficient-history gate that keys off ATR's presence never engaged.
    # Only when the caller did NOT ask for filled values — algo_trading
    # passes fillna=True and expects a dense series.
    if not fillna:
        atr.iloc[:max(0, min(window - 1, len(atr)))] = float('nan')
    return atr


def calculate_bollinger_bands(df, window=20, window_dev=2, fillna=False):
    """
    Calculate Bollinger Bands

    Args:
        df: DataFrame with 'close' column
        window: Period for moving average (default: 20)
        window_dev: Standard deviation multiplier (default: 2)
        fillna: Fill NaN values (default: False)

    Returns:
        Dict with 'bb_mavg', 'bb_hband', 'bb_lband', 'bb_pband', 'bb_wband',
        'bb_hband_indicator', 'bb_lband_indicator' Series
    """
    indicator = BollingerBands(
        close=df['close'],
        window=window,
        window_dev=window_dev,
        fillna=fillna
    )
    return {
        'bb_mavg': indicator.bollinger_mavg(),
        'bb_hband': indicator.bollinger_hband(),
        'bb_lband': indicator.bollinger_lband(),
        'bb_pband': indicator.bollinger_pband(),
        'bb_wband': indicator.bollinger_wband(),
        'bb_hband_indicator': indicator.bollinger_hband_indicator(),
        'bb_lband_indicator': indicator.bollinger_lband_indicator()
    }


def calculate_keltner_channel(df, window=20, window_atr=10, fillna=False, original_version=True):
    """
    Calculate Keltner Channel

    Args:
        df: DataFrame with 'high', 'low', 'close' columns
        window: Period for EMA (default: 20)
        window_atr: Period for ATR (default: 10)
        fillna: Fill NaN values (default: False)
        original_version: Use original version (default: True)

    Returns:
        Dict with 'kc_mavg', 'kc_hband', 'kc_lband', 'kc_pband', 'kc_wband',
        'kc_hband_indicator', 'kc_lband_indicator' Series
    """
    indicator = KeltnerChannel(
        high=df['high'],
        low=df['low'],
        close=df['close'],
        window=window,
        window_atr=window_atr,
        fillna=fillna,
        original_version=original_version
    )
    return {
        'kc_mavg': indicator.keltner_channel_mband(),
        'kc_hband': indicator.keltner_channel_hband(),
        'kc_lband': indicator.keltner_channel_lband(),
        'kc_pband': indicator.keltner_channel_pband(),
        'kc_wband': indicator.keltner_channel_wband(),
        'kc_hband_indicator': indicator.keltner_channel_hband_indicator(),
        'kc_lband_indicator': indicator.keltner_channel_lband_indicator()
    }


def calculate_donchian_channel(df, window=20, offset=0, fillna=False):
    """
    Calculate Donchian Channel

    Args:
        df: DataFrame with 'high', 'low', 'close' columns
        window: Period for channel (default: 20)
        offset: Offset period (default: 0)
        fillna: Fill NaN values (default: False)

    Returns:
        Dict with 'dc_hband', 'dc_lband', 'dc_mband', 'dc_pband', 'dc_wband' Series
    """
    indicator = DonchianChannel(
        high=df['high'],
        low=df['low'],
        close=df['close'],
        window=window,
        offset=offset,
        fillna=fillna
    )
    return {
        'dc_hband': indicator.donchian_channel_hband(),
        'dc_lband': indicator.donchian_channel_lband(),
        'dc_mband': indicator.donchian_channel_mband(),
        'dc_pband': indicator.donchian_channel_pband(),
        'dc_wband': indicator.donchian_channel_wband()
    }


def calculate_ulcer_index(df, window=14, fillna=False):
    """
    Calculate Ulcer Index

    Args:
        df: DataFrame with 'close' column
        window: Period for Ulcer Index calculation (default: 14)
        fillna: Fill NaN values (default: False)

    Returns:
        Series with Ulcer Index values
    """
    indicator = UlcerIndex(
        close=df['close'],
        window=window,
        fillna=fillna
    )
    return indicator.ulcer_index()


def calculate_all_volatility_indicators(df, **kwargs):
    """
    Calculate all volatility indicators at once

    Args:
        df: DataFrame with required columns (high, low, close)
        **kwargs: Optional parameters for individual indicators

    Returns:
        DataFrame with all volatility indicators
    """
    result_df = df.copy()

    # ATR
    safe_assign(result_df, 'atr', lambda: calculate_atr(df, **kwargs.get('atr', {})))

    # Bollinger Bands
    safe_assign(result_df,
                ['bb_mavg', 'bb_hband', 'bb_lband', 'bb_pband', 'bb_wband',
                 'bb_hband_indicator', 'bb_lband_indicator'],
                lambda: calculate_bollinger_bands(df, **kwargs.get('bollinger_bands', {})))

    # Keltner Channel
    safe_assign(result_df,
                ['kc_mavg', 'kc_hband', 'kc_lband', 'kc_pband', 'kc_wband',
                 'kc_hband_indicator', 'kc_lband_indicator'],
                lambda: calculate_keltner_channel(df, **kwargs.get('keltner_channel', {})))

    # Donchian Channel
    safe_assign(result_df,
                ['dc_hband', 'dc_lband', 'dc_mband', 'dc_pband', 'dc_wband'],
                lambda: calculate_donchian_channel(df, **kwargs.get('donchian_channel', {})))

    # Ulcer Index
    safe_assign(result_df, 'ui', lambda: calculate_ulcer_index(df, **kwargs.get('ulcer_index', {})))

    return result_df
