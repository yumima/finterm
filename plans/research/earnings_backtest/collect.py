"""Collect a pooled per-print dataset for the earnings backtest.

Reuses the daemon's own measurement code so every number matches what the
ER Earnings tab computes. Adds: market-adjusted (SPY) reaction, longer history.
"""
import sys, json, time, os, concurrent.futures as cf
sys.path.insert(0, "/home/yma/fin/finterm/fincept-qt/scripts")
import yfinance_data as yd
import yfinance as yf
import pandas as pd

OUT = os.path.join(os.path.dirname(__file__), "prints.jsonl")

UNIVERSE = """AAPL MSFT NVDA AMZN GOOGL META TSLA AVGO ORCL CRM ADBE AMD INTC QCOM TXN MU AMAT LRCX KLAC ADI
NFLX DIS CMCSA T VZ TMUS CHTR JPM BAC WFC C GS MS SCHW BLK AXP V MA PYPL COF USB PNC TFC
UNH JNJ PFE MRK ABBV LLY BMY AMGN GILD CVS CI HUM ELV TMO DHR ABT MDT ISRG SYK BSX ZTS VRTX REGN BIIB
WMT COST TGT HD LOW NKE SBUX MCD CMG YUM KO PEP PG CL KMB MDLZ GIS HSY KHC PM MO EL
XOM CVX COP EOG SLB OXY PSX MPC VLO HAL DVN
CAT DE BA LMT RTX GE HON MMM UPS FDX UNP CSX NSC EMR ETN ITW GD NOC
LIN APD SHW ECL DD DOW NEM FCX NUE
NEE DUK SO D AEP EXC SRE
AMT PLD CCI EQIX SPG O PSA
IBM CSCO ACN NOW INTU SNPS CDNS ANET PANW FTNT CRWD ZS DDOG SNOW MDB NET OKTA TEAM WDAY ADSK SHOP
UBER LYFT ABNB DASH BKNG EXPE MAR HLT RCL CCL DAL UAL AAL LUV
ROKU SPOT PINS SNAP ETSY EBAY W CHWY ZM DOCU TWLO U PLTR COIN HOOD SOFI AFRM UPST RIVN LCID F GM
LULU ULTA BBY DG DLTR ROST TJX GPS ANF AEO URBN KSS M JWN
MRNA BNTX DXCM ILMN ALGN IDXX EW
ON MCHP NXPI SWKS QRVO WDC STX SMCI DELL HPQ HPE
SQ NKE CLX CHD TSN HRL CAG CPB SJM MKC
""".split()
UNIVERSE = list(dict.fromkeys(UNIVERSE))


def spy_hist():
    h = yf.Ticker("SPY").history(period="12y", interval="1d", auto_adjust=True)
    return h


def collect(sym, spy):
    try:
        t = yf.Ticker(sym)
        hist = t.history(period="12y", interval="1d", auto_adjust=True)
        if hist is None or hist.empty:
            return []
        df = t.get_earnings_dates(limit=60)
        if df is None or df.empty:
            return []
    except Exception as e:
        print("ERR", sym, e, file=sys.stderr)
        return []
    now = pd.Timestamp.now(tz=df.index.tz)
    rows = {}
    for idx, row in df.iterrows():
        ts = pd.Timestamp(idx)
        if ts >= now:
            continue
        def cell(n):
            v = row.get(n) if n in df.columns else None
            try:
                return None if v is None or pd.isna(v) else float(v)
            except Exception:
                return None
        est, act, sur = cell("EPS Estimate"), cell("Reported EPS"), cell("Surprise(%)")
        if act is None:
            continue
        r, ru5, ru20, pb, pa, live = yd._earnings_price_reaction(hist, ts)
        if r is None:
            continue
        sr, *_ = yd._earnings_price_reaction(spy, ts)
        # 60-session run-up and 90-calendar into the print, from the same before-close
        day = yd._announce_et(int(ts.timestamp())).date()
        if day in rows and rows[day].get("eps_actual") is not None:
            continue
        rows[day] = {
            "symbol": sym,
            "ts": int(ts.timestamp()),
            "hour": int(yd._announce_et(int(ts.timestamp())).hour),
            "eps_estimate": est, "eps_actual": act, "surprise_pct": sur,
            "suspect": bool(yd._surprise_basis_suspect(est, act, sur)),
            "reaction": r, "spy_reaction": sr,
            "runup5": ru5, "runup20": ru20,
            "pre_vol": yd._pre_event_vol(hist, ts),
            "price_before": pb,
        }
    out = sorted(rows.values(), key=lambda d: d["ts"])
    return out


def main():
    spy = spy_hist()
    done = set()
    if os.path.exists(OUT):
        for line in open(OUT):
            done.add(json.loads(line)["symbol"])
    todo = [s for s in UNIVERSE if s not in done]
    print(f"{len(todo)} symbols to fetch", file=sys.stderr)
    with open(OUT, "a") as f, cf.ThreadPoolExecutor(max_workers=4) as ex:
        futs = {ex.submit(collect, s, spy): s for s in todo}
        for fut in cf.as_completed(futs):
            s = futs[fut]
            rows = fut.result()
            for r in rows:
                f.write(json.dumps(r) + "\n")
            f.flush()
            print(s, len(rows), file=sys.stderr)


if __name__ == "__main__":
    main()
