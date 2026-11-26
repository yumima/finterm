"""Can this machine reach the three Yahoo paths finterm uses?

Run it with finterm's own Python (any Python with yfinance works too):

  Windows:  "%LOCALAPPDATA%\\finterm\\venv-numpy2\\Scripts\\python.exe" check_yahoo.py
  Linux:    ~/.local/share/finterm/venv-numpy2/bin/python check_yahoo.py
  macOS:    ~/Library/Application\\ Support/finterm/venv-numpy2/bin/python check_yahoo.py

  1. chart  (v8, no login)  — history, charts, the first prices on screen
  2. quote  (v7, cookie + crumb from fc.yahoo.com) — index prices (^GSPC, ^N225)
             that move on screen, futures/after-hours %
  3. stream (websocket streamer.finance.yahoo.com) — live stock / futures / crypto ticks

It runs twice when the computer has a system proxy (a VPN in "system proxy"
mode): A as finterm 4.2.0 does (yfinance does not see the system proxy), and
B with the system proxy handed to every library, as the fix does. Send the
whole output.
"""
import json
import os
import subprocess
import sys
import time
import urllib.request

_ENV_KEYS = ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy")


def show(name, ok, detail):
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}: {detail}", flush=True)


def system_proxies():
    """The OS proxy settings (Windows registry / macOS), as the fix reads them."""
    try:
        if sys.platform == "win32":
            found = urllib.request.getproxies_registry()
        elif sys.platform == "darwin":
            found = urllib.request.getproxies_macosx_sysconf()
        else:
            return {}
    except Exception:
        return {}
    out = {}
    for scheme in ("http", "https"):
        url = found.get(scheme)
        if url and not url.lower().startswith("socks"):
            out[scheme] = url if "://" in url else "http://" + url
    if "https" not in out and "http" in out:
        out["https"] = out["http"]
    return out


def checks():
    try:
        import yfinance
        print("  yfinance", yfinance.__version__)
    except Exception as e:
        show("import yfinance", False, e)
        return

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
        rows = ((r.get("quoteResponse") or {}).get("result") or []) if isinstance(r, dict) else []
        if rows:
            show("quote v7", True, "; ".join(f"{q.get('symbol')} {q.get('regularMarketPrice')} "
                                             f"{q.get('marketState')}" for q in rows)
                 + f" ({time.time() - t0:.1f}s)")
        else:
            show("quote v7", False, f"no quoteResponse: {str(r)[:300]}")
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


def main():
    if os.environ.get("CHECK_YAHOO_CHILD"):
        checks()
        return
    print("python", sys.version.split()[0], "on", sys.platform)
    found = system_proxies()
    print("system proxy:", found or "none")
    print("proxy env:", {k: v for k, v in os.environ.items() if k in _ENV_KEYS} or "none")

    runs = [("A — as finterm 4.2.0 does", dict(os.environ))]
    if found and not any(os.environ.get(k) for k in _ENV_KEYS):
        env = dict(os.environ)
        for scheme, url in found.items():
            env[f"{scheme.upper()}_PROXY"] = env[f"{scheme}_proxy"] = url
        env["NO_PROXY"] = env["no_proxy"] = "localhost,127.0.0.1,::1"
        runs.append(("B — with the system proxy handed on (the fix)", env))

    for title, env in runs:
        print(f"\n{title}", flush=True)
        env["CHECK_YAHOO_CHILD"] = "1"
        subprocess.run([sys.executable, os.path.abspath(__file__)], env=env)


if __name__ == "__main__":
    main()
