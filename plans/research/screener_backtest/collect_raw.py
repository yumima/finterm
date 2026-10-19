"""Split-adjusted, NOT dividend-adjusted closes for the names in prices.pkl.

fund_backtest rolls a market cap forward from the public-float date by the
stock's price change. The auto-adjusted closes in prices.pkl fold dividends in,
which turns that ratio into a total return and overstates a payer's market cap
by the dividends since the float date. auto_adjust=False still applies splits
(yfinance applies those unconditionally), which is the basis wanted here.
"""
import os, pickle
import yfinance as yf

HERE = os.path.dirname(os.path.abspath(__file__))
D = os.path.join(HERE, "data")
syms = sorted(pickle.load(open(os.path.join(D, "prices.pkl"), "rb")))
raw = {}
for i in range(0, len(syms), 80):
    chunk = syms[i:i + 80]
    d = yf.download(chunk, start="2007-01-01", auto_adjust=False, actions=False,
                    group_by="ticker", threads=True, progress=False)
    for s in chunk:
        try:
            c = d[s]["Close"].dropna()
        except KeyError:
            continue
        if len(c) > 50:
            raw[s] = c.copy()
    print(i + len(chunk), "/", len(syms), "have", len(raw), flush=True)
pickle.dump(raw, open(os.path.join(D, "closes_raw.pkl"), "wb"))
