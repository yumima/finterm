"""Can this machine reach the three Yahoo paths finterm uses?

Run it with finterm's own Python, so it goes through the same libraries and
proxy settings as the app:

  Windows:  %LOCALAPPDATA%\\finterm\\venv-numpy2\\Scripts\\python.exe check_yahoo.py
  Linux:    ~/.local/share/finterm/venv-numpy2/bin/python check_yahoo.py
  macOS:    ~/Library/Application\\ Support/finterm/venv-numpy2/bin/python check_yahoo.py

  1. chart  (v8, no login)  — history, charts, the first prices on screen
  2. quote  (v7, cookie + crumb from fc.yahoo.com) — index prices (^GSPC, ^N225)
             that move on screen, futures/after-hours %
  3. stream (websocket streamer.finance.yahoo.com) — live stock / futures / crypto ticks

Prints one PASS/FAIL line per path plus the detail; send the whole output.
"""
import json
import os
import sys
import time
import urllib.request


def show(name, ok, detail):
    print(f"[{'PASS' if ok else 'FAIL'}] {name}: {detail}", flush=True)


print("python", sys.version.split()[0], "on", sys.platform)
print("system proxies:", urllib.request.getproxies() or "none")
print("proxy env:", {k: v for k, v in os.environ.items() if "proxy" in k.lower()} or "none")
try:
    import yfinance
    print("yfinance", yfinance.__version__)
except Exception as e:
    show("import yfinance", False, e)
    sys.exit(1)

# 1. chart
try:
    t0 = time.time()
    h = yfinance.Ticker("^N225").history(period="5d", interval="1d")
    show("chart ^N225", not h.empty, f"{len(h)} bars, last close {h['Close'].iloc[-1]:.2f} "
                                    f"({time.time() - t0:.1f}s)" if not h.empty else "empty answer")
except Exception as e:
    show("chart ^N225", False, repr(e))

# 2. quote (cookie + crumb)
try:
    from yfinance.data import YfData
    t0 = time.time()
    r = YfData().get_raw_json("https://query1.finance.yahoo.com/v7/finance/quote", timeout=15, params={
        "symbols": "^GSPC,^N225,^HSI", "formatted": "false",
        "fields": "regularMarketPrice,regularMarketTime,marketState"})
    rows = (r.get("quoteResponse") or {}).get("result") or [] if isinstance(r, dict) else []
    show("quote v7", bool(rows), "; ".join(f"{q.get('symbol')} {q.get('regularMarketPrice')} {q.get('marketState')}"
                                           for q in rows) + f" ({time.time() - t0:.1f}s)" if rows
         else f"no quoteResponse: {str(r)[:300]}")
except Exception as e:
    show("quote v7", False, repr(e))

# 3. stream — BTC-USD trades around the clock, so a working feed ticks within seconds
try:
    from websockets.sync.client import connect
    from yfinance.live import BaseWebSocket
    decode = BaseWebSocket(verbose=False)._decode_message
    t0 = time.time()
    with connect("wss://streamer.finance.yahoo.com/?version=2", open_timeout=15, close_timeout=2) as ws:
        ws.send(json.dumps({"subscribe": ["BTC-USD", "ETH-USD"]}))
        got = []
        while time.time() - t0 < 30 and len(got) < 3:
            try:
                raw = ws.recv(timeout=5)
            except TimeoutError:
                continue
            msg = decode(json.loads(raw).get("message", ""))
            if isinstance(msg, dict) and "price" in msg:
                got.append(f"{msg.get('id')} {msg['price']:.2f}")
    show("stream", bool(got), (", ".join(got) if got else "connected but no tick in 30s")
         + f" ({time.time() - t0:.1f}s)")
except Exception as e:
    show("stream", False, repr(e))
