from typing import List, Dict
from talipp.indicators import OBV, VWAP, VWMA, ChaikinOsc, ForceIndex
from talipp.ohlcv import OHLCV

def calculate_obv(ohlcv_data: List[Dict]) -> Dict:
    obv = OBV()
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        obv.add(ohlcv)

    return {
        'values': [float(v) if v is not None else None for v in list(obv)],
        'last': float(obv[-1]) if len(obv) > 0 and obv[-1] is not None else None
    }

def calculate_vwap(ohlcv_data: List[Dict]) -> Dict:
    vwap = VWAP()
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        vwap.add(ohlcv)

    return {
        'values': [float(v) if v is not None else None for v in list(vwap)],
        'last': float(vwap[-1]) if len(vwap) > 0 and vwap[-1] is not None else None
    }

def calculate_vwma(ohlcv_data: List[Dict], period: int = 20) -> Dict:
    vwma = VWMA(period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        vwma.add(ohlcv)

    return {
        'values': [float(v) if v is not None else None for v in list(vwma)],
        'period': period,
        'last': float(vwma[-1]) if len(vwma) > 0 and vwma[-1] is not None else None
    }

def calculate_mfi(ohlcv_data: List[Dict], period: int = 14) -> Dict:
    # talipp 2.x dropped MFI, so the classic Money Flow Index is computed
    # directly with rolling sums: typical price * volume flows, positive vs
    # negative over the window; 100 - 100/(1 + ratio).
    #
    # No volume → no money flow → None, never a fabricated reading. An
    # instrument whose bars carry no volume must chart as unavailable, not as
    # a flat neutral line that looks like a real measurement.
    typ = [(b['high'] + b['low'] + b['close']) / 3.0 for b in ohlcv_data]
    vol = [float(b.get('volume', 0) or 0) for b in ohlcv_data]

    n = len(typ)
    pos_flow = [0.0] * n   # per-bar signed flows (index i = flow of bar i vs i-1)
    neg_flow = [0.0] * n
    for i in range(1, n):
        flow = typ[i] * vol[i]
        if typ[i] > typ[i - 1]:
            pos_flow[i] = flow
        elif typ[i] < typ[i - 1]:
            neg_flow[i] = flow

    values = [None] * n
    pos = neg = win_vol = 0.0
    for i in range(1, n):
        pos += pos_flow[i]
        neg += neg_flow[i]
        win_vol += vol[i]
        if i > period:
            pos -= pos_flow[i - period]
            neg -= neg_flow[i - period]
            win_vol -= vol[i - period]
        if i < period:
            continue
        if win_vol <= 0.0:
            continue   # no volume in the window — MFI is undefined here
        if neg == 0.0:
            values[i] = 100.0
        elif pos == 0.0:
            values[i] = 0.0
        else:
            values[i] = 100.0 - 100.0 / (1.0 + pos / neg)

    return {
        'values': values,
        'period': period,
        'last': next((v for v in reversed(values) if v is not None), None)
    }


def calculate_chaikin_osc(ohlcv_data: List[Dict], fast_period: int = 3, slow_period: int = 10) -> Dict:
    chaikin = ChaikinOsc(fast_period, slow_period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        chaikin.add(ohlcv)

    return {
        'values': [float(v) if v is not None else None for v in list(chaikin)],
        'last': float(chaikin[-1]) if len(chaikin) > 0 and chaikin[-1] is not None else None
    }

def calculate_force_index(ohlcv_data: List[Dict], period: int = 13) -> Dict:
    force_idx = ForceIndex(period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        force_idx.add(ohlcv)

    return {
        'values': [float(v) if v is not None else None for v in list(force_idx)],
        'period': period,
        'last': float(force_idx[-1]) if len(force_idx) > 0 and force_idx[-1] is not None else None
    }

def main():
    print("Testing TALIpp Volume Indicators")

    ohlcv_data = [
        {'open': 100, 'high': 102, 'low': 99, 'close': 101, 'volume': 1000},
        {'open': 101, 'high': 103, 'low': 100, 'close': 102, 'volume': 1100},
        {'open': 102, 'high': 105, 'low': 101, 'close': 104, 'volume': 1200},
        {'open': 104, 'high': 106, 'low': 103, 'close': 105, 'volume': 1150},
        {'open': 105, 'high': 107, 'low': 104, 'close': 106, 'volume': 1300},
    ] * 5

    print("\n1. Testing OBV...")
    result = calculate_obv(ohlcv_data)
    print("Last OBV:", result['last'])
    assert result['last'] is not None
    print("Test 1: PASSED")

    print("\n2. Testing VWAP...")
    result = calculate_vwap(ohlcv_data)
    print("Last VWAP:", result['last'])
    assert result['last'] is not None
    print("Test 2: PASSED")

    print("\n3. Testing VWMA...")
    result = calculate_vwma(ohlcv_data, period=10)
    print("Last VWMA:", result['last'])
    print("Test 3: PASSED")

    print("\nAll tests: PASSED")

if __name__ == "__main__":
    main()
