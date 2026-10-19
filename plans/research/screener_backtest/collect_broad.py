"""Five years of daily bars for TODAY's US primary common stocks >= $300M
(TradingView universe) — the small/mid momentum names Luk trades, which the
S&P 500 panel lacks. SURVIVORSHIP-BIASED by construction (only names alive
today): it flatters any long strategy, so it is used as a second read only.
"""
import os, pickle, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "fincept-qt", "scripts"))
import screener_data as sd
import yfinance as yf

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
uni = sorted(sd.fetch_universe(3e8))
print(len(uni), "names")
frames = {}
for i in range(0, len(uni) + 1, 100):
    chunk = uni[i:i + 100] + (["SPY"] if i == 0 else [])
    if not chunk:
        break
    d = yf.download(chunk, period="5y", interval="1d", auto_adjust=True, actions=True,
                    group_by="ticker", threads=True, progress=False)
    for s in chunk:
        f = sd._ticker_frame(d, s)
        if f is not None:
            frames[s] = f[["Open", "High", "Low", "Close", "Volume"]].copy()
    print(i + len(chunk), "have", len(frames), flush=True)
pickle.dump(frames, open(os.path.join(D, "broad_prices.pkl"), "wb"))
