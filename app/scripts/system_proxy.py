"""The OS proxy (a VPN client in "system proxy" mode) for every HTTP library.

urllib/requests and websockets read the Windows registry / macOS settings, but
curl_cffi (yfinance's HTTP client) reads only the *_PROXY environment
variables. With a VPN set as the system proxy, yfinance then went DIRECT — from
mainland China Yahoo answers that with 429 "Too Many Requests" — while the
push stream went through the VPN: prices loaded once, indices never moved
(customer report 2026-11-26).

apply() copies the system proxy into the environment, once, at interpreter
start (sitecustomize.py), unless the user's environment already names one.
"""
import os
import sys

_ENV_KEYS = ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy")
_LOCAL = ("localhost", "127.0.0.1", "::1")


def _override_to_no_proxy(override):
    """Windows' ProxyOverride ("localhost;127.*;192.168.*;*.corp;<local>") as
    NO_PROXY entries curl and requests both understand: suffixes as .domain,
    trailing-* IPv4 prefixes as CIDR; anything else is dropped."""
    out = []
    for raw in (override or "").replace(",", ";").split(";"):
        e = raw.strip()
        if not e or e == "<local>":
            continue
        if e.startswith("*."):
            out.append(e[1:])
            continue
        parts = e.split(".")
        if parts[-1] == "*" and 1 <= len(parts) - 1 <= 3 and all(p.isdigit() for p in parts[:-1]):
            octets = [int(p) for p in parts[:-1]]
            if all(0 <= o <= 255 for o in octets):
                bits = 8 * len(octets)
                out.append(".".join(str(o) for o in octets + [0] * (4 - len(octets))) + f"/{bits}")
            continue
        if "*" not in e:
            out.append(e)
    return out


def _windows_override():
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                            r"Software\Microsoft\Windows\CurrentVersion\Internet Settings") as k:
            return str(winreg.QueryValueEx(k, "ProxyOverride")[0])
    except Exception:
        return ""


def system_proxies():
    """{'http': url, 'https': url} from the OS settings (no environment), or {}.
    SOCKS-only setups are left alone: requests without PySocks would fail on a
    socks:// proxy, which is worse than going direct."""
    import urllib.request
    try:
        if sys.platform == "win32":
            found = urllib.request.getproxies_registry()
        elif sys.platform == "darwin":
            found = urllib.request.getproxies_macosx_sysconf()
        else:
            return {}  # Linux: the desktop proxy reaches programs through the environment already
    except Exception:
        return {}
    out = {}
    for scheme in ("http", "https"):
        url = found.get(scheme)
        if url and not url.lower().startswith("socks"):
            out[scheme] = url if "://" in url else "http://" + url
    if "https" not in out and "http" in out:
        out["https"] = out["http"]  # one proxy for both, as the OS means it
    return out


def apply(environ=None):
    """Put the system proxy into `environ` (default os.environ). Returns what
    was set ({} when the environment already has a proxy or the OS has none)."""
    env = os.environ if environ is None else environ
    if any(env.get(k) for k in _ENV_KEYS):
        return {}
    found = system_proxies()
    if not found:
        return {}
    set_ = {}
    for scheme, url in found.items():
        for key in (f"{scheme.upper()}_PROXY", f"{scheme}_proxy"):  # libcurl reads only lowercase http_proxy
            env[key] = url
            set_[key] = url
    no_proxy = [x.strip() for x in (env.get("NO_PROXY") or env.get("no_proxy") or "").split(",") if x.strip()]
    extra = list(_LOCAL) + (_override_to_no_proxy(_windows_override()) if sys.platform == "win32" else [])
    for e in extra:
        if e not in no_proxy:
            no_proxy.append(e)
    env["NO_PROXY"] = env["no_proxy"] = ",".join(no_proxy)
    set_["NO_PROXY"] = env["NO_PROXY"]
    return set_
