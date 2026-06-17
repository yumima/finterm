"""
AI Quant Lab - REAL Market Data Helper
======================================

Shared, single source of truth for REAL OHLCV + engineered features used by the
AI Quant Lab training/evaluation modules (qlib_rl, qlib_advanced_models,
qlib_meta_learning).

HARD RULE — ZERO fabricated data. Every number that ends up in a model's
training set, an evaluation metric, or a displayed return comes from real
market prices fetched via yfinance. If real data genuinely cannot be obtained
(empty fetch / network failure), the helpers here RAISE RealDataError; callers
translate that into an explicit {"success": False, "error": ...} result. We
never substitute np.random noise.

The yfinance access pattern (group_by='ticker', defensive MultiIndex handling,
auto_adjust, stdout redirection) mirrors the proven daemon in
scripts/yfinance_data.py.
"""

from __future__ import annotations

import io
import contextlib
from datetime import datetime, timedelta
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np
import pandas as pd

try:
    import yfinance as yf
except Exception as _yf_exc:  # pragma: no cover - import guard
    yf = None
    _YF_IMPORT_ERROR = str(_yf_exc)
else:
    _YF_IMPORT_ERROR = None


class RealDataError(RuntimeError):
    """Raised when real market data cannot be obtained.

    Callers MUST catch this and emit {"success": False, "error": str(exc)}
    rather than fall back to synthetic data.
    """


# Feature columns produced by add_features(), in a stable order. Used as the
# default feature set when building supervised (X, y) matrices.
FEATURE_COLUMNS: List[str] = [
    "returns",      # 1-day simple return of the (adjusted) close
    "log_returns",  # log return
    "volatility",   # 20-day rolling std of returns (annualised-agnostic)
    "rsi",          # 14-period Relative Strength Index, scaled to 0..1
    "macd",         # MACD line (12/26 EMA difference)
    "signal",       # MACD signal line (9 EMA of MACD)
    "macd_hist",    # MACD histogram (macd - signal)
    "volume_z",     # 20-day z-score of volume
]


def _normalize_symbol(sym: str) -> str:
    """Mirror yfinance_data class-share normalisation (BRK.B -> BRK-B)."""
    import re
    s = (sym or "").strip().upper()
    return re.sub(r"\.([A-Z])$", r"-\1", s)


def _resolve_dates(start_date: Optional[str], end_date: Optional[str],
                   lookback_days: int = 730) -> Tuple[str, str]:
    """Return (start, end) as YYYY-MM-DD strings, filling sensible defaults.

    Defaults give ~2y of daily bars ending today, which is enough rows for the
    windowed supervised matrices the models expect.
    """
    end = end_date or datetime.now().strftime("%Y-%m-%d")
    if start_date:
        start = start_date
    else:
        try:
            end_dt = datetime.strptime(end, "%Y-%m-%d")
        except ValueError:
            end_dt = datetime.now()
        start = (end_dt - timedelta(days=lookback_days)).strftime("%Y-%m-%d")
    return start, end


def fetch_ohlcv(tickers: Sequence[str],
                start_date: Optional[str] = None,
                end_date: Optional[str] = None,
                interval: str = "1d") -> Dict[str, pd.DataFrame]:
    """Fetch REAL daily OHLCV for each ticker over [start_date, end_date].

    Returns {ticker: DataFrame[open, high, low, close, volume]} indexed by date.
    Tickers/days with no data are simply omitted (never fabricated). Raises
    RealDataError only if NOTHING could be fetched for ANY ticker.
    """
    if yf is None:
        raise RealDataError(f"yfinance unavailable: {_YF_IMPORT_ERROR}")

    if isinstance(tickers, str):
        tickers = [tickers]
    syms = [t for t in (tickers or []) if t]
    if not syms:
        raise RealDataError("No tickers provided")

    start, end = _resolve_dates(start_date, end_date)

    norm_to_orig: Dict[str, str] = {}
    download_syms: List[str] = []
    for s in syms:
        n = _normalize_symbol(s)
        norm_to_orig[n] = s
        download_syms.append(n)

    try:
        _buf = io.StringIO()
        with contextlib.redirect_stdout(_buf):
            data = yf.download(download_syms, start=start, end=end,
                               interval=interval, group_by="ticker",
                               progress=False, threads=True, auto_adjust=True)
    except Exception as e:
        raise RealDataError(
            f"yfinance download failed for {download_syms} "
            f"[{start}..{end}]: {e}")

    if data is None or len(data) == 0:
        raise RealDataError(
            f"yfinance returned no data for {download_syms} [{start}..{end}]")

    single = len(download_syms) == 1
    cols = ["Open", "High", "Low", "Close", "Volume"]

    def _frame_for(norm_sym: str) -> Optional[pd.DataFrame]:
        # Layouts vary (single vs multi-ticker, ticker on column level 0 or 1).
        # Mirror the defensive access used in yfinance_data.py.
        try:
            if not isinstance(data.columns, pd.MultiIndex):
                hist = data
            else:
                level0 = data.columns.get_level_values(0).unique().tolist()
                level1 = data.columns.get_level_values(1).unique().tolist()
                if norm_sym in level0:
                    hist = data[norm_sym]
                elif norm_sym in level1:
                    hist = data.xs(norm_sym, axis=1, level=1)
                elif single:
                    hist = data
                else:
                    return None
            if hist is None or hist.empty:
                return None
            present = [c for c in cols if c in hist.columns]
            if "Close" not in present:
                return None
            hist = hist[present].dropna(how="all")
            hist = hist.dropna(subset=["Close"])
            if hist.empty:
                return None
            out = hist.rename(columns={
                "Open": "open", "High": "high", "Low": "low",
                "Close": "close", "Volume": "volume"})
            return out
        except Exception:
            return None

    out: Dict[str, pd.DataFrame] = {}
    for norm_sym in download_syms:
        orig = norm_to_orig.get(norm_sym, norm_sym)
        frame = _frame_for(norm_sym)
        if frame is not None and not frame.empty:
            out[orig] = frame

    if not out:
        raise RealDataError(
            f"No usable OHLCV rows for {syms} [{start}..{end}] "
            f"(empty or all-NaN response)")
    return out


def _rsi(close: pd.Series, period: int = 14) -> pd.Series:
    delta = close.diff()
    gain = delta.clip(lower=0.0)
    loss = -delta.clip(upper=0.0)
    avg_gain = gain.ewm(alpha=1.0 / period, min_periods=period, adjust=False).mean()
    avg_loss = loss.ewm(alpha=1.0 / period, min_periods=period, adjust=False).mean()
    rs = avg_gain / avg_loss.replace(0.0, np.nan)
    rsi = 100.0 - (100.0 / (1.0 + rs))
    return rsi.fillna(50.0)


def add_features(df: pd.DataFrame) -> pd.DataFrame:
    """Compute standard technical features from REAL prices.

    Input: DataFrame with at least a 'close' column (open/high/low/volume used
    when present). Output: a copy with FEATURE_COLUMNS added plus the original
    OHLCV columns. All features are derived deterministically from real prices.
    """
    if "close" not in df.columns or df.empty:
        raise RealDataError("Cannot compute features: no close prices")

    out = df.copy()
    close = out["close"].astype(float)

    out["returns"] = close.pct_change()
    out["log_returns"] = np.log(close / close.shift(1))
    out["volatility"] = out["returns"].rolling(window=20, min_periods=5).std()

    out["rsi"] = _rsi(close, 14) / 100.0  # scaled 0..1 like qlib_rl observation

    ema12 = close.ewm(span=12, adjust=False).mean()
    ema26 = close.ewm(span=26, adjust=False).mean()
    out["macd"] = ema12 - ema26
    out["signal"] = out["macd"].ewm(span=9, adjust=False).mean()
    out["macd_hist"] = out["macd"] - out["signal"]

    if "volume" in out.columns:
        vol = out["volume"].astype(float)
        vmean = vol.rolling(window=20, min_periods=5).mean()
        vstd = vol.rolling(window=20, min_periods=5).std().replace(0.0, np.nan)
        out["volume_z"] = ((vol - vmean) / vstd).fillna(0.0)
    else:
        out["volume_z"] = 0.0

    # Drop the warm-up rows that contain NaNs from rolling/diff windows so every
    # remaining row is a fully-real feature vector.
    out = out.dropna(subset=["returns", "volatility", "macd", "signal"])
    return out


def build_market_frame(tickers: Sequence[str],
                       start_date: Optional[str] = None,
                       end_date: Optional[str] = None) -> pd.DataFrame:
    """Build a single OHLCV+features frame for an RL trading environment.

    Uses the FIRST ticker (RL trades one asset at a time). The returned frame
    carries the columns the TradingEnvironment observation reads: close, open,
    high, low, volume, returns, volatility, rsi, macd, signal (plus extras).
    Raises RealDataError if no real data is available.
    """
    if isinstance(tickers, str):
        tickers = [tickers]
    syms = [t for t in (tickers or []) if t]
    if not syms:
        raise RealDataError("No tickers provided for market frame")

    frames = fetch_ohlcv(syms, start_date, end_date)
    # RL env trades a single asset; pick the first ticker that returned data.
    primary = None
    for s in syms:
        if s in frames:
            primary = frames[s]
            break
    if primary is None:
        primary = next(iter(frames.values()))

    feat = add_features(primary)
    if feat.empty:
        raise RealDataError("Real data fetched but feature frame is empty")
    return feat


def build_supervised(tickers: Sequence[str],
                     start_date: Optional[str] = None,
                     end_date: Optional[str] = None,
                     seq_length: int = 20,
                     input_size: Optional[int] = None,
                     flatten: bool = False,
                     task_type: str = "regression",
                     test_fraction: float = 0.2,
                     ) -> Dict[str, np.ndarray]:
    """Build REAL supervised (X, y) train/test matrices from market data.

    X is built from windowed real feature vectors; y is the next-period REAL
    return (regression) or its sign as 0/1 (classification).

    Args:
        seq_length:  window length (timesteps per sample).
        input_size:  number of features per timestep. If given and smaller than
                     the available features, the first `input_size` FEATURE_COLUMNS
                     are used; if larger, columns are zero-padded so the model's
                     declared input_size is honoured (padding is structural, not
                     fabricated signal).
        flatten:     if True, X is reshaped to 2D (n, seq_length*input_size) for
                     tree/sklearn models; otherwise 3D (n, seq_length, input_size)
                     for sequence models.
        task_type:   'regression' (y = next return) or 'classification'
                     (y = 1 if next return > 0 else 0).
        test_fraction: chronological tail fraction reserved for the test split.

    Returns dict with X_train, y_train, X_test, y_test, plus metadata
    (n_features, seq_length, n_samples, tickers). Raises RealDataError on
    insufficient real data.
    """
    if isinstance(tickers, str):
        tickers = [tickers]
    syms = [t for t in (tickers or []) if t]
    if not syms:
        raise RealDataError("No tickers provided for supervised dataset")

    frames = fetch_ohlcv(syms, start_date, end_date)

    feat_cols = list(FEATURE_COLUMNS)
    target_features = input_size if (input_size and input_size > 0) else len(feat_cols)

    all_X: List[np.ndarray] = []
    all_y: List[float] = []

    for sym in syms:
        if sym not in frames:
            continue
        feat = add_features(frames[sym])
        if len(feat) <= seq_length + 1:
            continue

        mat = feat[feat_cols].to_numpy(dtype=np.float32)  # (T, F)

        # Honour the model's declared input_size: trim or zero-pad feature dim.
        F = mat.shape[1]
        if target_features < F:
            mat = mat[:, :target_features]
        elif target_features > F:
            pad = np.zeros((mat.shape[0], target_features - F), dtype=np.float32)
            mat = np.concatenate([mat, pad], axis=1)

        # next-period REAL return as the supervised target
        next_ret = feat["returns"].shift(-1).to_numpy(dtype=np.float32)

        T = mat.shape[0]
        for i in range(seq_length, T):
            y_val = next_ret[i - 1]
            if y_val is None or np.isnan(y_val):
                continue
            window = mat[i - seq_length:i]  # (seq_length, target_features)
            all_X.append(window)
            all_y.append(float(y_val))

    if not all_X:
        raise RealDataError(
            f"Insufficient real data to build {seq_length}-step windows for "
            f"{syms} (need > {seq_length + 1} rows per ticker)")

    X = np.stack(all_X).astype(np.float32)          # (N, seq_length, F)
    y = np.asarray(all_y, dtype=np.float32)

    if task_type == "classification":
        y = (y > 0.0).astype(np.int64)
    else:
        y = y.reshape(-1, 1).astype(np.float32)

    # Chronological split: earliest samples train, latest test (no leakage).
    n = X.shape[0]
    n_test = max(1, int(round(n * test_fraction))) if n > 5 else max(1, n // 5)
    n_train = n - n_test
    if n_train < 1:
        raise RealDataError(
            f"Only {n} real samples available — too few to split train/test")

    X_train, X_test = X[:n_train], X[n_train:]
    y_train, y_test = y[:n_train], y[n_train:]

    if flatten:
        X_train = X_train.reshape(X_train.shape[0], -1)
        X_test = X_test.reshape(X_test.shape[0], -1)

    if task_type != "classification":
        y_train = y_train.reshape(-1, 1)
        y_test = y_test.reshape(-1, 1)

    return {
        "X_train": X_train,
        "y_train": y_train,
        "X_test": X_test,
        "y_test": y_test,
        "n_features": int(target_features),
        "seq_length": int(seq_length),
        "n_samples": int(n),
        "tickers": [s for s in syms if s in frames],
    }
