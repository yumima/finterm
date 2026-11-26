"""system_proxy: curl_cffi (yfinance's client) follows the OS proxy.

A VPN in "system proxy" mode was seen by websockets but not by curl_cffi, so
yfinance went direct and Yahoo answered 429 from mainland China (2026-11-26;
the customer's check: direct 429, through the proxy PASS). Each curl_cffi
request now asks the OS for its URL's proxy, honouring the OS's bypass list,
at the moment it is made; the environment is left alone.
"""
import os
import socket
import subprocess
import sys
import tempfile
import threading
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.abspath(os.path.join(HERE, "..", "..", "scripts"))
sys.path.insert(0, SCRIPTS)

import system_proxy as sp  # noqa: E402

fails = []


def check(name, ok, detail=""):
    print(("PASS : " if ok else "FAIL : ") + name + (f" — {detail}" if detail and not ok else ""))
    if not ok:
        fails.append(name)


real_getproxies, real_bypass = urllib.request.getproxies, urllib.request.proxy_bypass
os_proxies = {}
bypassed = set()
urllib.request.getproxies = lambda: dict(os_proxies)
urllib.request.proxy_bypass = lambda host: host in bypassed

# ── the decision, per URL, at request time ──────────────────────────────────
os_proxies.update({"http": "http://127.0.0.1:7897", "https": "http://127.0.0.1:7897"})
check("https goes through the OS proxy", sp.proxy_for("https://query1.finance.yahoo.com/x") == "http://127.0.0.1:7897")
os_proxies.pop("https")
check("an http-only setting serves https too", sp.proxy_for("https://a.example/") == "http://127.0.0.1:7897")
bypassed.add("ollama-box")
check("a host the OS bypasses goes direct", sp.proxy_for("http://ollama-box:11434/api") is None)
os_proxies.clear()
check("the VPN switched off: direct at the next request", sp.proxy_for("https://query1.finance.yahoo.com/") is None)
os_proxies.update({"https": "socks5://127.0.0.1:7898"})
check("a SOCKS-only setting is left alone", sp.proxy_for("https://query1.finance.yahoo.com/") is None)
os_proxies.clear()
os_proxies.update({"https": "127.0.0.1:7897"})
check("a bare host:port gets its scheme", sp.proxy_for("https://x.example/") == "http://127.0.0.1:7897")
os_proxies.clear()
check("no environment is written", not any(k.lower().endswith("_proxy") for k in os.environ))
check("Linux: nothing installed (curl reads the desktop's env proxy itself)",
      sp.install() is (sys.platform in ("win32", "darwin")))

# ── a hook the interpreter already had still runs ───────────────────────────
with tempfile.TemporaryDirectory() as other:
    with open(os.path.join(other, "sitecustomize.py"), "w") as f:
        f.write("import os\nos.environ['FINTERM_OTHER_HOOK'] = 'ran'\n")
    out = subprocess.run([sys.executable, "-c", "import os; print(os.environ.get('FINTERM_OTHER_HOOK'))"],
                         env={**os.environ, "PYTHONPATH": os.pathsep.join([SCRIPTS, other])},
                         capture_output=True, text=True, timeout=60).stdout.strip()
    check("the interpreter's own sitecustomize still runs", out == "ran", out)

# ── curl_cffi: patched on import, and really goes through the proxy ────────
try:
    import curl_cffi  # noqa: F401
except ImportError as e:
    print(f"SKIP : curl_cffi parts ({e})")
    urllib.request.getproxies, urllib.request.proxy_bypass = real_getproxies, real_bypass
    sys.exit(1 if fails else 77)  # 77 = skip: no app venv here

code = ("import sys; sys.path.insert(0, %r); import system_proxy as sp; sys.meta_path.insert(0, sp._Finder()); "
        "from curl_cffi.requests import Session; print(getattr(Session.request, '_finterm_proxy', False))" % SCRIPTS)
out = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, timeout=60).stdout.strip()
check("curl_cffi's Session is patched when it is first imported", out == "True", out)

import curl_cffi.requests.session as cs  # noqa: E402
from curl_cffi import requests as creq  # noqa: E402

sp._patch(cs)

seen = []
srv = socket.socket()
srv.bind(("127.0.0.1", 0))
srv.listen(8)
port = srv.getsockname()[1]


def serve():
    while True:
        try:
            c, _ = srv.accept()
        except OSError:
            return
        seen.append(c.recv(4096).decode("latin-1", "replace").split("\r\n", 1)[0])
        c.sendall(b"HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n")  # refuse: no real traffic
        c.close()


threading.Thread(target=serve, daemon=True).start()
try:
    os_proxies.update({"http": f"http://127.0.0.1:{port}", "https": f"http://127.0.0.1:{port}"})
    try:
        creq.get("https://query1.finance.yahoo.com/v8/finance/chart/%5EN225", timeout=5)
    except Exception:
        pass  # the stand-in proxy refuses; what matters is that it was asked
    check("a curl_cffi request goes through the OS proxy",
          any(x.startswith("CONNECT query1.finance.yahoo.com:443") for x in seen), str(seen))
    seen.clear()
    bypassed.add("bypassed.invalid")
    try:
        creq.get("https://bypassed.invalid/", timeout=3)
    except Exception:
        pass
    check("...and a bypassed host does not", not seen, str(seen))
finally:
    srv.close()
    urllib.request.getproxies, urllib.request.proxy_bypass = real_getproxies, real_bypass

sys.exit(1 if fails else 0)
