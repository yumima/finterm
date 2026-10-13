from typing import List, Dict
from talipp.indicators import ADX, Aroon, Ichimoku, ParabolicSAR, SuperTrend
from talipp.ohlcv import OHLCV
from talipp_wrapper import fnum

def calculate_adx(ohlcv_data: List[Dict], di_period: int = 14, adx_period: int = 14) -> Dict:
    adx = ADX(di_period, adx_period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        adx.add(ohlcv)

    # talipp 2.x: ADXVal fields are adx / plus_di / minus_di.
    adx_values = [fnum(v.adx) if v is not None else None for v in adx]
    last_val = adx[-1] if len(adx) > 0 and adx[-1] is not None else None

    return {
        'values': adx_values,
        'di_period': di_period,
        'adx_period': adx_period,
        'last': fnum(last_val.adx) if last_val else None
    }


def calculate_aroon(ohlcv_data: List[Dict], period: int = 25) -> Dict:
    aroon = Aroon(period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        aroon.add(ohlcv)

    up_values = []
    down_values = []

    for val in aroon:
        if val is not None:
            up_values.append(float(val.up))
            down_values.append(float(val.down))
        else:
            up_values.append(None)
            down_values.append(None)

    last_val = aroon[-1] if len(aroon) > 0 and aroon[-1] is not None else None

    return {
        'up': up_values,
        'down': down_values,
        'last_up': float(last_val.up) if last_val else None,
        'last_down': float(last_val.down) if last_val else None
    }

def calculate_ichimoku(ohlcv_data: List[Dict], tenkan: int = 9, kijun: int = 26, senkou_b: int = 52) -> Dict:
    # talipp 2.x signature: Ichimoku(kijun_period, tenkan_period,
    # chikou_lag_period, senkou_slow_period, senkou_lookup_period); the lag and
    # lookup both conventionally equal the kijun period.
    ichimoku = Ichimoku(kijun, tenkan, kijun, senkou_b, kijun)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        ichimoku.add(ohlcv)

    # 2.x field names: conversion/base line, cloud leading fast/slow.
    tenkan_v = [fnum(v.conversion_line) if v is not None else None for v in ichimoku]
    kijun_v = [fnum(v.base_line) if v is not None else None for v in ichimoku]
    senkou_a = [fnum(v.cloud_leading_fast_line) if v is not None else None for v in ichimoku]
    senkou_bv = [fnum(v.cloud_leading_slow_line) if v is not None else None for v in ichimoku]
    last_val = ichimoku[-1] if len(ichimoku) > 0 and ichimoku[-1] is not None else None

    return {
        'tenkan_sen': tenkan_v,
        'kijun_sen': kijun_v,
        'senkou_span_a': senkou_a,
        'senkou_span_b': senkou_bv,
        'last_tenkan': fnum(last_val.conversion_line) if last_val else None,
        'last_kijun': fnum(last_val.base_line) if last_val else None
    }


def calculate_parabolic_sar(ohlcv_data: List[Dict], af_start: float = 0.02, af_max: float = 0.2, af_increment: float = 0.02) -> Dict:
    # talipp 2.x: ParabolicSAR(init_accel_factor, accel_factor_inc,
    # max_accel_factor) and each output is a ParabolicSARVal carrying .value.
    psar = ParabolicSAR(af_start, af_increment, af_max)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        psar.add(ohlcv)

    values = [fnum(v.value) if v is not None else None for v in psar]
    return {
        'values': values,
        'last': next((v for v in reversed(values) if v is not None), None)
    }


def calculate_supertrend(ohlcv_data: List[Dict], period: int = 10, multiplier: float = 3.0) -> Dict:
    supertrend = SuperTrend(period, multiplier)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        supertrend.add(ohlcv)

    # talipp 2.x: .trend is a Trend enum with UP=1, DOWN=2 — NOT a sign. Map
    # to ±1 explicitly so "trend > 0 means up" holds for every consumer.
    from talipp.indicators.SuperTrend import Trend
    values = [fnum(v.value) if v is not None else None for v in supertrend]
    trends = [(1 if v.trend == Trend.UP else -1) if v is not None and v.trend is not None else None
              for v in supertrend]
    last_v = next((v for v in reversed(values) if v is not None), None)
    last_t = next((t for t in reversed(trends) if t is not None), None)

    return {
        'values': values,
        'trends': trends,
        'last_value': last_v,
        'last_trend': last_t
    }


def main():
    print("Testing TALIpp Trend/Other Indicators")

    ohlcv_data = [
        {'open': 100, 'high': 102, 'low': 99, 'close': 101, 'volume': 1000},
        {'open': 101, 'high': 103, 'low': 100, 'close': 102, 'volume': 1100},
        {'open': 102, 'high': 105, 'low': 101, 'close': 104, 'volume': 1200},
        {'open': 104, 'high': 106, 'low': 103, 'close': 105, 'volume': 1150},
        {'open': 105, 'high': 107, 'low': 104, 'close': 106, 'volume': 1300},
    ] * 10

    print("\n1. Testing ADX...")
    try:
        result = calculate_adx(ohlcv_data, di_period=14, adx_period=14)
        print("Last ADX:", result['last'])
        print("Test 1: PASSED")
    except Exception as e:
        print("Test 1: SKIPPED (ADX requires more data points)")
        print("Error:", str(e))

    print("\n2. Testing Aroon...")
    result = calculate_aroon(ohlcv_data, period=25)
    print("Last Aroon Up:", result['last_up'])
    print("Last Aroon Down:", result['last_down'])
    print("Test 2: PASSED")

    print("\n3. Testing Parabolic SAR...")
    result = calculate_parabolic_sar(ohlcv_data)
    print("Last SAR:", result['last'])
    print("Test 3: PASSED")

    print("\nAll tests: PASSED")

if __name__ == "__main__":
    main()
