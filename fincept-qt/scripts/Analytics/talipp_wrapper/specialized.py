from typing import List, Dict
from talipp.indicators import (
    AO, AccuDist, BOP, CHOP, CoppockCurve, DPO, EMV, FibonacciRetracement,
    IBS, KST, KVO, MassIndex, McGinleyDynamic, MeanDev, PivotsHL,
    RogersSatchell, SFX, SMMA, SOBV, STC, StdDev, TRIX, TTM, UO, VTX, ZigZag
)
from talipp.ohlcv import OHLCV
from talipp_wrapper import fnum

def calculate_ao(ohlcv_data: List[Dict], fast_period: int = 5, slow_period: int = 34) -> Dict:
    ao = AO(fast_period, slow_period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        ao.add(ohlcv)
    return {
        'values': [float(v) if v is not None else None for v in list(ao)],
        'last': float(ao[-1]) if len(ao) > 0 and ao[-1] is not None else None
    }

def calculate_accu_dist(ohlcv_data: List[Dict]) -> Dict:
    ad = AccuDist()
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        ad.add(ohlcv)
    return {
        'values': [float(v) if v is not None else None for v in list(ad)],
        'last': float(ad[-1]) if len(ad) > 0 and ad[-1] is not None else None
    }

def calculate_bop(ohlcv_data: List[Dict]) -> Dict:
    bop = BOP()
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        bop.add(ohlcv)
    return {
        'values': [float(v) if v is not None else None for v in list(bop)],
        'last': float(bop[-1]) if len(bop) > 0 and bop[-1] is not None else None
    }

def calculate_chop(ohlcv_data: List[Dict], period: int = 14) -> Dict:
    chop = CHOP(period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        chop.add(ohlcv)
    return {
        'values': [float(v) if v is not None else None for v in list(chop)],
        'period': period,
        'last': float(chop[-1]) if len(chop) > 0 and chop[-1] is not None else None
    }

def calculate_coppock_curve(prices: List[float], roc1_period: int = 14, roc2_period: int = 11, wma_period: int = 10) -> Dict:
    cc = CoppockCurve(roc1_period, roc2_period, wma_period)
    for price in prices:
        cc.add(price)
    return {
        'values': [float(v) if v is not None else None for v in list(cc)],
        'last': float(cc[-1]) if len(cc) > 0 and cc[-1] is not None else None
    }

def calculate_dpo(prices: List[float], period: int = 20) -> Dict:
    dpo = DPO(period)
    for price in prices:
        dpo.add(price)
    return {
        'values': [float(v) if v is not None else None for v in list(dpo)],
        'period': period,
        'last': float(dpo[-1]) if len(dpo) > 0 and dpo[-1] is not None else None
    }

def calculate_emv(ohlcv_data: List[Dict], period: int = 14) -> Dict:
    # talipp 2.x: EMV(period, volume_div) — 10000 is the classic divisor.
    emv = EMV(period, 10000)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        emv.add(ohlcv)

    values = [float(v) if v is not None else None for v in emv]
    return {
        'values': values,
        'period': period,
        'last': next((v for v in reversed(values) if v is not None), None)
    }


def calculate_fibonacci_retracement(ohlcv_data: List[Dict]) -> Dict:
    fib = FibonacciRetracement()
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        fib.add(ohlcv)

    values = []
    for val in fib:
        if val is not None:
            values.append({
                'level_0': float(val.level_0),
                'level_236': float(val.level_236),
                'level_382': float(val.level_382),
                'level_500': float(val.level_500),
                'level_618': float(val.level_618),
                'level_1000': float(val.level_1000)
            })
        else:
            values.append(None)

    last_val = fib[-1] if len(fib) > 0 and fib[-1] is not None else None
    return {
        'values': values,
        'last': {
            'level_0': float(last_val.level_0),
            'level_236': float(last_val.level_236),
            'level_382': float(last_val.level_382),
            'level_500': float(last_val.level_500),
            'level_618': float(last_val.level_618),
            'level_1000': float(last_val.level_1000)
        } if last_val else None
    }

def calculate_ibs(ohlcv_data: List[Dict]) -> Dict:
    ibs = IBS()
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        ibs.add(ohlcv)
    return {
        'values': [float(v) if v is not None else None for v in list(ibs)],
        'last': float(ibs[-1]) if len(ibs) > 0 and ibs[-1] is not None else None
    }

def calculate_kst(prices: List[float]) -> Dict:
    # talipp 2.x: KST takes the full classic parameter set — ROC periods
    # (10, 15, 20, 30), their SMA periods (10, 10, 10, 15), signal 9.
    kst = KST(10, 10, 15, 10, 20, 10, 30, 15, 9)
    for price in prices:
        kst.add(price)

    kst_values = [fnum(v.kst) if v is not None else None for v in kst]
    signal_values = [fnum(v.signal) if v is not None else None for v in kst]

    return {
        'kst': kst_values,
        'signal': signal_values,
        'last_kst': next((v for v in reversed(kst_values) if v is not None), None),
        'last_signal': next((v for v in reversed(signal_values) if v is not None), None)
    }


def calculate_kvo(ohlcv_data: List[Dict], fast_period: int = 34, slow_period: int = 55, signal_period: int = 13) -> Dict:
    # talipp 2.x: KVO(fast, slow) emits plain floats and no signal line — the
    # classic signal (an EMA of KVO) is computed here.
    from talipp.indicators import EMA
    kvo = KVO(fast_period, slow_period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        kvo.add(ohlcv)

    kvo_values = [float(v) if v is not None else None for v in kvo]
    sig = EMA(signal_period)
    signal_values = []
    for v in kvo_values:
        if v is None:
            signal_values.append(None)
            continue
        sig.add(v)
        s = sig[-1] if len(sig) > 0 else None
        signal_values.append(float(s) if s is not None else None)

    return {
        'kvo': kvo_values,
        'signal': signal_values,
        'last_kvo': next((v for v in reversed(kvo_values) if v is not None), None),
        'last_signal': next((v for v in reversed(signal_values) if v is not None), None)
    }


def calculate_mass_index(ohlcv_data: List[Dict], period: int = 25, ema_period: int = 9) -> Dict:
    # talipp 2.x: MassIndex(ma_period, ma_ma_period, ma_ratio_period) — the
    # classic form smooths twice with the same EMA period.
    mi = MassIndex(ema_period, ema_period, period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        mi.add(ohlcv)

    values = [float(v) if v is not None else None for v in mi]
    return {
        'values': values,
        'period': period,
        'last': next((v for v in reversed(values) if v is not None), None)
    }


def calculate_mcginley_dynamic(prices: List[float], period: int = 14) -> Dict:
    # talipp 2.x: McGinleyDynamic(period) — the k factor is internal now.
    md = McGinleyDynamic(period)
    for price in prices:
        md.add(price)

    values = [float(v) if v is not None else None for v in md]
    return {
        'values': values,
        'period': period,
        'last': next((v for v in reversed(values) if v is not None), None)
    }


def calculate_mean_dev(prices: List[float], period: int = 20) -> Dict:
    md = MeanDev(period)
    for price in prices:
        md.add(price)
    return {
        'values': [float(v) if v is not None else None for v in list(md)],
        'period': period,
        'last': float(md[-1]) if len(md) > 0 and md[-1] is not None else None
    }

def calculate_pivots_hl(ohlcv_data: List[Dict], left_bars: int = 5, right_bars: int = 5) -> Dict:
    pivots = PivotsHL(left_bars, right_bars)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        pivots.add(ohlcv)

    values = []
    for val in pivots:
        if val is not None:
            values.append({
                'high': float(val.high) if val.high is not None else None,
                'low': float(val.low) if val.low is not None else None
            })
        else:
            values.append(None)

    last_val = pivots[-1] if len(pivots) > 0 and pivots[-1] is not None else None
    return {
        'values': values,
        'last': {
            'high': float(last_val.high) if last_val and last_val.high is not None else None,
            'low': float(last_val.low) if last_val and last_val.low is not None else None
        } if last_val else None
    }

def calculate_rogers_satchell(ohlcv_data: List[Dict], period: int = 14) -> Dict:
    rs = RogersSatchell(period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        rs.add(ohlcv)
    return {
        'values': [float(v) if v is not None else None for v in list(rs)],
        'period': period,
        'last': float(rs[-1]) if len(rs) > 0 and rs[-1] is not None else None
    }

def calculate_sfx(ohlcv_data: List[Dict], period: int = 5, atr_period: int = 10, atr_mult: float = 4.0) -> Dict:
    sfx = SFX(period, atr_period, atr_mult)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        sfx.add(ohlcv)

    values = []
    for val in sfx:
        if val is not None:
            values.append({'value': float(val.value), 'trend': int(val.trend)})
        else:
            values.append(None)

    last_val = sfx[-1] if len(sfx) > 0 and sfx[-1] is not None else None
    return {
        'values': values,
        'last': {'value': float(last_val.value), 'trend': int(last_val.trend)} if last_val else None
    }

def calculate_smma(prices: List[float], period: int = 20) -> Dict:
    smma = SMMA(period)
    for price in prices:
        smma.add(price)
    return {
        'values': [float(v) if v is not None else None for v in list(smma)],
        'period': period,
        'last': float(smma[-1]) if len(smma) > 0 and smma[-1] is not None else None
    }

def calculate_sobv(ohlcv_data: List[Dict], period: int = 10) -> Dict:
    sobv = SOBV(period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        sobv.add(ohlcv)
    return {
        'values': [float(v) if v is not None else None for v in list(sobv)],
        'period': period,
        'last': float(sobv[-1]) if len(sobv) > 0 and sobv[-1] is not None else None
    }

def calculate_stc(prices: List[float], fast_period: int = 23, slow_period: int = 50, cycle: int = 10) -> Dict:
    # talipp 2.x: STC(fast_macd_period, slow_macd_period, stoch_period,
    # stoch_smoothing_period); 3 is the conventional smoothing.
    stc = STC(fast_period, slow_period, cycle, 3)
    for price in prices:
        stc.add(price)

    values = [float(v) if v is not None else None for v in stc]
    return {
        'values': values,
        'last': next((v for v in reversed(values) if v is not None), None)
    }


def calculate_std_dev(prices: List[float], period: int = 20) -> Dict:
    std = StdDev(period)
    for price in prices:
        std.add(price)
    return {
        'values': [float(v) if v is not None else None for v in list(std)],
        'period': period,
        'last': float(std[-1]) if len(std) > 0 and std[-1] is not None else None
    }

def calculate_trix(prices: List[float], period: int = 15, signal_period: int = 9) -> Dict:
    # talipp 2.x: TRIX(period) emits plain floats and no signal line — the
    # classic signal (an EMA of TRIX) is computed here.
    from talipp.indicators import EMA
    trix = TRIX(period)
    for price in prices:
        trix.add(price)

    trix_values = [float(v) if v is not None else None for v in trix]
    sig = EMA(signal_period)
    signal_values = []
    for v in trix_values:
        if v is None:
            signal_values.append(None)
            continue
        sig.add(v)
        s = sig[-1] if len(sig) > 0 else None
        signal_values.append(float(s) if s is not None else None)

    return {
        'trix': trix_values,
        'signal': signal_values,
        'last_trix': next((v for v in reversed(trix_values) if v is not None), None),
        'last_signal': next((v for v in reversed(signal_values) if v is not None), None)
    }


def calculate_ttm(ohlcv_data: List[Dict], period: int = 20) -> Dict:
    # talipp 2.x: TTM(period, bb_std_dev_mult, kc_atr_mult) — ONE period drives
    # both bands, so there is no separate kc_period knob to offer. The
    # chartable line is the squeeze histogram; the boolean flags ride along.
    ttm = TTM(period, 2.0, 1.5)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        ttm.add(ohlcv)

    values = [fnum(v.histogram) if v is not None else None for v in ttm]
    squeeze = [bool(v.squeeze) if v is not None and v.squeeze is not None else None for v in ttm]

    return {
        'values': values,
        'squeeze': squeeze,
        'last': next((v for v in reversed(values) if v is not None), None)
    }


def calculate_uo(ohlcv_data: List[Dict], short_period: int = 7, medium_period: int = 14, long_period: int = 28) -> Dict:
    uo = UO(short_period, medium_period, long_period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        uo.add(ohlcv)
    return {
        'values': [float(v) if v is not None else None for v in list(uo)],
        'last': float(uo[-1]) if len(uo) > 0 and uo[-1] is not None else None
    }

def calculate_vtx(ohlcv_data: List[Dict], period: int = 14) -> Dict:
    # talipp 2.x: VTXVal fields are plus_vtx / minus_vtx.
    vtx = VTX(period)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'], bar.get('volume', 0))
        vtx.add(ohlcv)

    plus = [fnum(v.plus_vtx) if v is not None else None for v in vtx]
    minus = [fnum(v.minus_vtx) if v is not None else None for v in vtx]

    return {
        'plus': plus,
        'minus': minus,
        'last_plus': next((v for v in reversed(plus) if v is not None), None),
        'last_minus': next((v for v in reversed(minus) if v is not None), None)
    }


def calculate_zigzag(ohlcv_data: List[Dict], change: float = 5.0) -> Dict:
    # talipp 2.x: ZigZag(sensitivity_fraction, min_trend_length) emits one
    # value PER PIVOT, not per bar — so this returns its OWN timestamps and
    # the caller must pair against them, not the input bars.
    zz = ZigZag(change / 100.0, 3)
    for bar in ohlcv_data:
        ohlcv = OHLCV(bar['open'], bar['high'], bar['low'], bar['close'],
                      bar.get('volume', 0), bar.get('timestamp'))
        zz.add(ohlcv)

    from talipp.indicators.ZigZag import PivotType
    values = []
    times = []
    for v in zz:
        if v is None or v.ohlcv is None:
            continue
        # A HIGH pivot sits at the bar's high, a LOW pivot at its low.
        px = v.ohlcv.high if v.type == PivotType.HIGH else v.ohlcv.low
        if px is None or v.ohlcv.time is None:
            continue
        values.append(float(px))
        times.append(int(v.ohlcv.time))

    return {
        'values': values,
        'timestamps': times,
        'change': change,
        'last': values[-1] if values else None
    }


def main():
    print("Testing TALIpp Specialized Indicators")

    prices = [100, 102, 101, 105, 107, 106, 108, 110, 109, 111, 113, 112, 115, 114, 116, 118, 117, 120, 119, 121] * 3
    ohlcv_data = [
        {'open': p-1, 'high': p+2, 'low': p-2, 'close': p, 'volume': 1000+i*10}
        for i, p in enumerate(prices)
    ]

    print("\n1. Testing SMMA...")
    result = calculate_smma(prices, period=10)
    print("Last SMMA:", result['last'])
    assert result['last'] is not None
    print("Test 1: PASSED")

    print("\n2. Testing StdDev...")
    result = calculate_std_dev(prices, period=10)
    print("Last StdDev:", result['last'])
    assert result['last'] is not None
    print("Test 2: PASSED")

    print("\n3. Testing BOP...")
    result = calculate_bop(ohlcv_data)
    print("Last BOP:", result['last'])
    assert result['last'] is not None
    print("Test 3: PASSED")

    print("\nAll tests: PASSED")

if __name__ == "__main__":
    main()
