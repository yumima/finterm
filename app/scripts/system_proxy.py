"""The OS proxy (a VPN in "system proxy" mode) for curl_cffi too.

urllib/requests and websockets ask the Windows registry / macOS settings for
the proxy (and for the hosts that bypass it), but curl_cffi — yfinance's HTTP
client — reads only the *_PROXY environment variables. With a VPN set as the
system proxy, the push stream went through it while yfinance went DIRECT: from
mainland China Yahoo answered 429, so prices loaded once and indices never
moved (customer report 2026-11-26).

install() (run from sitecustomize at start-up, Windows and macOS only) patches
curl_cffi's Session.request when curl_cffi is first imported: a request with
no proxy of its own asks the OS, at that moment, which proxy serves its URL —
urllib.request.getproxies() (the environment first, else the OS settings) and
proxy_bypass() (NO_PROXY, else the OS's own exceptions such as <local>). The
environment is not touched, so every other library keeps its own behaviour,
and a VPN switched on or off is seen by the next request.
"""
import sys

_TARGET = "curl_cffi.requests.session"


def proxy_for(url):
    """The proxy URL the OS would use for `url`, or None (direct). SOCKS
    proxies are left alone: curl_cffi needs them spelt socks5h://, and a
    wrong guess is worse than going direct."""
    import urllib.parse
    import urllib.request
    try:
        parts = urllib.parse.urlsplit(url)
        scheme, host = (parts.scheme or "http").lower(), parts.hostname or ""
        found = urllib.request.getproxies()
        proxy = found.get(scheme) or (found.get("http") if scheme == "https" else None)
        if not proxy or proxy.lower().startswith("socks") or not host:
            return None
        if urllib.request.proxy_bypass(host):
            return None
        return proxy if "://" in proxy else "http://" + proxy
    except Exception:
        return None


def _patch(module):
    Session = getattr(module, "Session", None)
    if Session is None or getattr(Session.request, "_finterm_proxy", False):
        return
    original = Session.request

    def request(self, method, url, *args, **kwargs):
        if not kwargs.get("proxy") and not kwargs.get("proxies") and not getattr(self, "proxies", None) \
                and not getattr(self, "proxy", None):
            p = proxy_for(str(url))
            if p:
                kwargs["proxy"] = p
        return original(self, method, url, *args, **kwargs)

    request._finterm_proxy = True
    Session.request = request


class _Finder:
    """Patches curl_cffi's session module right after it is imported — no
    import of curl_cffi here (it is slow, and most scripts never use it)."""

    def find_spec(self, name, path=None, target=None):
        if name != _TARGET:
            return None
        import importlib.machinery
        spec = importlib.machinery.PathFinder.find_spec(name, path)
        if spec is None or spec.loader is None or not hasattr(spec.loader, "exec_module"):
            return spec
        exec_module = spec.loader.exec_module

        def exec_and_patch(module):
            exec_module(module)
            try:
                _patch(module)
            except Exception:
                pass

        spec.loader.exec_module = exec_and_patch
        return spec


def install():
    """Windows and macOS only: elsewhere the desktop proxy already reaches
    programs through the environment, which curl reads itself."""
    if sys.platform not in ("win32", "darwin"):
        return False
    if _TARGET in sys.modules:
        _patch(sys.modules[_TARGET])
    elif not any(isinstance(f, _Finder) for f in sys.meta_path):
        sys.meta_path.insert(0, _Finder())
    return True
