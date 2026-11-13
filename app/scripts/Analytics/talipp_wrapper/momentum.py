from typing import List, Dict
from talipp.indicators import RSI, MACD, Stoch, StochRSI, CCI, ROC, TSI
from talipp.ohlcv import OHLCV
from talipp_wrapper import ema_signal, fnum, last_valid

def calculate_rsi(prices: List[float], period: int = 14) -> Dict:
    rsi = RSI(period)
    for price in prices:
        rsi.add(price)

    return {
        'values': [float(v) if v is not None else None for v in list(rsi)],
        'period': period,
        'last': float(rsi[-1]) if len(rsi) > 0 and rsi[-1] is not None else None
    }

def calculate_macd(prices: List[float], fast_period: int = 12, slow_period: int = 26, signal_period: int = 9) -> Dict:
    # talipp 2.x: MACDVal fields are macd/signal/histogram (not .value), and
    # each field can be None on its own during warm-up.
    macd = MACD(fast_period, slow_period, signal_period)
    for price in prices:
        macd.add(price)

    macd_values = [fnum(v.macd) if v is not None else None for v in macd]
    signal_values = [fnum(v.signal) if v is not None else None for v in macd]
    histogram_values = [fnum(v.histogram) if v is not None else None for v in macd]
    last_val = macd[-1] if len(macd) > 0 and macd[-1] is not None else None

    return {
        'macd': macd_values,
        'signal': signal_values,
        'histogram': histogram_values,
        'last_macd': fnum(last_val.macd) if last_val else None,
        'last_signal': fnum(last_val.signal) if last_val else None,
        'last_histogram': fnum(last_val.histogram) if last_val else None
    }


def calculate_stoch(ohlcv_data: List[Dict], period: int = 14, smoothing_period: int = 3) -> Dict:
    stoch = Stoch(period, smoothing_period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        stoch.add(ohlcv)

    # %D warms up later than %K — the fields go None independently.
    k_values = [fnum(v.k) if v is not None else None for v in stoch]
    d_values = [fnum(v.d) if v is not None else None for v in stoch]
    last_val = stoch[-1] if len(stoch) > 0 and stoch[-1] is not None else None

    return {
        'k': k_values,
        'd': d_values,
        'last_k': fnum(last_val.k) if last_val else None,
        'last_d': fnum(last_val.d) if last_val else None
    }


def calculate_stoch_rsi(prices: List[float], period: int = 14, stoch_period: int = 14, k_smoothing: int = 3, d_smoothing: int = 3) -> Dict:
    stoch_rsi = StochRSI(period, stoch_period, k_smoothing, d_smoothing)
    for price in prices:
        stoch_rsi.add(price)

    k_values = [fnum(v.k) if v is not None else None for v in stoch_rsi]
    d_values = [fnum(v.d) if v is not None else None for v in stoch_rsi]
    last_val = stoch_rsi[-1] if len(stoch_rsi) > 0 and stoch_rsi[-1] is not None else None

    return {
        'k': k_values,
        'd': d_values,
        'last_k': fnum(last_val.k) if last_val else None,
        'last_d': fnum(last_val.d) if last_val else None
    }


def calculate_cci(ohlcv_data: List[Dict], period: int = 20) -> Dict:
    cci = CCI(period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        cci.add(ohlcv)

    return {
        'values': [float(v) if v is not None else None for v in list(cci)],
        'period': period,
        'last': float(cci[-1]) if len(cci) > 0 and cci[-1] is not None else None
    }

def calculate_roc(prices: List[float], period: int = 12) -> Dict:
    roc = ROC(period)
    for price in prices:
        roc.add(price)

    return {
        'values': [float(v) if v is not None else None for v in list(roc)],
        'period': period,
        'last': float(roc[-1]) if len(roc) > 0 and roc[-1] is not None else None
    }

def calculate_tsi(prices: List[float], long_period: int = 25, short_period: int = 13, signal_period: int = 13) -> Dict:
    # talipp 2.x: TSI(fast_period, slow_period) emits plain floats and carries
    # no signal line — the classic signal (an EMA of TSI) is computed here.
    from talipp.indicators import EMA
    tsi = TSI(short_period, long_period)
    for price in prices:
        tsi.add(price)

    tsi_values = [float(v) if v is not None else None for v in tsi]
    signal_values = ema_signal(tsi_values, signal_period)

    last_tsi = last_valid(tsi_values)
    last_signal = last_valid(signal_values)

    return {
        'tsi': tsi_values,
        'signal': signal_values,
        'last_tsi': last_tsi,
        'last_signal': last_signal
    }


def calculate_williams(ohlcv_data: List[Dict], period: int = 14) -> Dict:
    from talipp.indicators import Williams
    williams = Williams(period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        williams.add(ohlcv)

    return {
        'values': [float(v) if v is not None else None for v in list(williams)],
        'period': period,
        'last': float(williams[-1]) if len(williams) > 0 and williams[-1] is not None else None
    }

def main():
    print("Testing TALIpp Momentum Indicators")

    prices = [100, 102, 101, 105, 107, 106, 108, 110, 109, 111, 113, 112, 115, 114, 116, 118, 117, 120, 119, 121]

    print("\n1. Testing RSI...")
    result = calculate_rsi(prices, period=14)
    print("Last RSI:", result['last'])
    assert result['last'] is not None
    print("Test 1: PASSED")

    print("\n2. Testing MACD...")
    result = calculate_macd(prices)
    print("Last MACD:", result['last_macd'])
    print("Test 2: PASSED")

    print("\n3. Testing ROC...")
    result = calculate_roc(prices, period=10)
    print("Last ROC:", result['last'])
    print("Test 3: PASSED")

    print("\nAll tests: PASSED")

if __name__ == "__main__":
    main()
