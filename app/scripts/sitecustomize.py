"""Run by Python at start-up for every finterm Python process (the scripts
folder is on PYTHONPATH). Kept to the one thing that must happen before any
HTTP library is imported: the OS proxy into the environment (system_proxy.py).
Never raises — a broken hook must not stop the interpreter."""
try:
    import system_proxy as _system_proxy

    _system_proxy.apply()
except Exception:
    pass
