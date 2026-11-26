"""Run by Python at start-up for every finterm Python process (the scripts
folder is on PYTHONPATH). Two things, neither of which may stop the
interpreter:

1. system_proxy.install(): curl_cffi (yfinance) follows the OS proxy, as the
   other HTTP libraries do (Windows/macOS; nothing is loaded elsewhere).
2. The interpreter's own sitecustomize, if it has one further down sys.path
   (a distro or corporate hook): this file shadows it, so it is run here.
"""
import os
import sys

try:
    import system_proxy as _system_proxy

    _system_proxy.install()
except Exception:
    pass

try:
    import importlib.machinery
    import importlib.util

    _here = os.path.dirname(os.path.abspath(__file__))
    _rest = [p for p in sys.path if os.path.abspath(p or os.curdir) != _here]
    _spec = importlib.machinery.PathFinder.find_spec("sitecustomize", _rest)
    if _spec and _spec.origin and os.path.abspath(_spec.origin) != os.path.abspath(__file__):
        _mod = importlib.util.module_from_spec(_spec)
        _spec.loader.exec_module(_mod)
except Exception:
    pass
