"""system_proxy.apply(): the OS proxy reaches curl_cffi (yfinance's client).

A VPN in "system proxy" mode was seen by websockets but not by curl_cffi, so
yfinance went direct and Yahoo answered 429 from mainland China (2026-11-26).
Here a local proxy stands in for the VPN: after apply(), a curl_cffi request
must arrive at it as a CONNECT; localhost stays direct; an explicit *_PROXY in
the environment wins; ProxyOverride entries become NO_PROXY entries.
"""
import os
import socket
import sys
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))

import system_proxy as sp  # noqa: E402

fails = []


def check(name, ok, detail=""):
    print(("PASS : " if ok else "FAIL : ") + name + (f" — {detail}" if detail and not ok else ""))
    if not ok:
        fails.append(name)


# ── the environment apply() writes ──────────────────────────────────────────
sp.system_proxies = lambda: {"http": "http://127.0.0.1:7897", "https": "http://127.0.0.1:7897"}
env = {}
sp.apply(env)
check("HTTPS_PROXY / https_proxy set", env.get("HTTPS_PROXY") == env.get("https_proxy") == "http://127.0.0.1:7897")
check("lowercase http_proxy set (libcurl reads only that one)", env.get("http_proxy") == "http://127.0.0.1:7897")
check("localhost stays direct", all(x in env["NO_PROXY"].split(",") for x in ("localhost", "127.0.0.1", "::1")))

env = {"HTTPS_PROXY": "http://corp:3128"}
check("an explicit proxy in the environment wins", sp.apply(env) == {} and env == {"HTTPS_PROXY": "http://corp:3128"})

env = {"NO_PROXY": "internal.example"}
sp.apply(env)
check("an existing NO_PROXY is kept", env["NO_PROXY"].split(",")[0] == "internal.example")

sp.system_proxies = lambda: {}
env = {}
check("no system proxy: nothing set", sp.apply(env) == {} and env == {})

got = sp._override_to_no_proxy("localhost;127.*;192.168.*;10.*;*.corp.example;intranet;<local>;a*b")
check("ProxyOverride → NO_PROXY", got == ["localhost", "127.0.0.0/8", "192.168.0.0/16", "10.0.0.0/8",
                                          ".corp.example", "intranet"], str(got))

# ── curl_cffi really goes through it ────────────────────────────────────────
try:
    from curl_cffi import requests as creq
except ImportError as e:
    print(f"SKIP : curl_cffi end-to-end ({e})")
    sys.exit(1 if fails else 77)  # 77 = skip: no app venv here

seen = []
srv = socket.socket()
srv.bind(("127.0.0.1", 0))
srv.listen(4)
port = srv.getsockname()[1]


def serve():
    while True:
        try:
            c, _ = srv.accept()
        except OSError:
            return
        data = c.recv(4096).decode("latin-1", "replace")
        seen.append(data.split("\r\n", 1)[0])
        c.sendall(b"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n")  # refuse: no real traffic
        c.close()


threading.Thread(target=serve, daemon=True).start()
saved = {k: os.environ.pop(k) for k in list(os.environ) if k.lower() in
         ("http_proxy", "https_proxy", "all_proxy", "no_proxy")}
try:
    sp.system_proxies = lambda: {"http": f"http://127.0.0.1:{port}", "https": f"http://127.0.0.1:{port}"}
    sp.apply()
    try:
        creq.get("https://query1.finance.yahoo.com/v8/finance/chart/%5EN225", timeout=5)
    except Exception:
        pass  # the stand-in proxy refuses; what matters is that it was asked
    check("curl_cffi sends Yahoo through the system proxy",
          any(line.startswith("CONNECT query1.finance.yahoo.com:443") for line in seen), str(seen))
finally:
    srv.close()
    for k in ("HTTP_PROXY", "HTTPS_PROXY", "http_proxy", "https_proxy", "NO_PROXY", "no_proxy"):
        os.environ.pop(k, None)
    os.environ.update(saved)

sys.exit(1 if fails else 0)
