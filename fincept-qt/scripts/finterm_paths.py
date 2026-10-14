"""Where finterm's Python side keeps data — one root, the app's own.

Everything a script persists goes under the same folder the app uses
(AppPaths in C++), never into ~/.fincept, the home directory, the scripts
tree, or /tmp. PERSONAL data and the app's own data are kept apart:

    user/            the user's — back up or erase this one folder
      agents/        agent task/eval stores and agent memory
      features/      per-feature databases (M&A deals, Alpha Arena, …)
      data/          the user's databases (written by the app)
    market_data/     public-data indexes built locally (SEC Form 4/13F, FINRA)
    cache/           regenerable downloads, safe to delete
    models/          trained/cached ML models (qlib, vision)

PythonRunner sets FINCEPT_DATA_DIR (the root) and FINCEPT_USER_DIR (the
active profile's user/); the fallbacks mirror AppPaths so a script run by
hand writes to the same places.
"""

import os
import sys
from pathlib import Path

_BUNDLE_ID = "com.fincept.terminal"


def data_root() -> Path:
    env = os.environ.get("FINCEPT_DATA_DIR")
    if env:
        return Path(env)
    if sys.platform.startswith("win"):
        base = os.environ.get("LOCALAPPDATA") or str(Path.home() / "AppData" / "Local")
        return Path(base) / _BUNDLE_ID
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / _BUNDLE_ID
    return Path.home() / ".local" / "share" / _BUNDLE_ID


def user_root() -> Path:
    env = os.environ.get("FINCEPT_USER_DIR")
    return Path(env) if env else data_root() / "user"


def user_data_dir() -> Path:
    """The user's database folder, as the app resolves it."""
    env = os.environ.get("FINCEPT_USER_DATA_DIR")
    return Path(env) if env else user_root() / "data"


def _sub(*parts: str) -> Path:
    p = data_root().joinpath(*parts)
    p.mkdir(parents=True, exist_ok=True)
    return p


def _user_sub(*parts: str) -> Path:
    p = user_root().joinpath(*parts)
    p.mkdir(parents=True, exist_ok=True)
    return p


def agents_dir(*parts: str) -> Path:
    return _user_sub("agents", *parts)


def cache_dir(*parts: str) -> Path:
    # The active profile's cache (AppPaths::cache()), so Settings › Storage
    # clears it with the rest; root/cache when run by hand.
    env = os.environ.get("FINCEPT_CACHE_DIR")
    p = (Path(env) if env else data_root() / "cache").joinpath(*parts)
    p.mkdir(parents=True, exist_ok=True)
    return p


def models_dir(*parts: str) -> Path:
    return _sub("models", *parts)


def feature_dir(*parts: str) -> Path:
    return _user_sub("features", *parts)


def market_data_path(filename: str) -> str:
    return str(_sub("market_data") / filename)
