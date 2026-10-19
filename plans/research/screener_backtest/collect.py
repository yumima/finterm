"""Collect daily OHLCV for every ticker that was in the S&P 500 at any point
since 2008, plus SPY. Point-in-time membership comes from fja05680/sp500
(`sp500_hist.csv`, one row per membership change date).

Yahoo has no delisted tickers, so names that left the index by bankruptcy or
acquisition are partly missing: collect reports the coverage per year so the
survivorship bias is measured, not assumed.
"""
import sys, pickle, os
import pandas as pd
import yfinance as yf

HERE = os.path.dirname(os.path.abspath(__file__))
MEMBERS = sys.argv[1]           # path to sp500_hist.csv
OUT = os.path.join(HERE, "data")
os.makedirs(OUT, exist_ok=True)

m = pd.read_csv(MEMBERS, parse_dates=["date"])
m = m[m["date"] >= "2008-01-01"]
m["tickers"] = m["tickers"].str.split(",")
universe = sorted({t for ts in m["tickers"] for t in ts})
yahoo = {t: t.replace(".", "-") for t in universe}
print(len(universe), "tickers ever in the index since 2008")

frames = {}
syms = sorted(set(yahoo.values())) + ["SPY"]
for i in range(0, len(syms), 80):
    chunk = syms[i:i + 80]
    d = yf.download(chunk, start="2007-01-01", auto_adjust=True, actions=False,
                    group_by="ticker", threads=True, progress=False)
    for s in chunk:
        try:
            f = d[s].dropna(how="all")
        except KeyError:
            continue
        if len(f) > 50:
            frames[s] = f.copy()
    print(i + len(chunk), "/", len(syms), "have", len(frames), flush=True)

with open(os.path.join(OUT, "prices.pkl"), "wb") as fh:
    pickle.dump(frames, fh)
m.to_pickle(os.path.join(OUT, "members.pkl"))
print("saved", len(frames))
