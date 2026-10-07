"""Shared JSON-in / JSON-out contract for the M&A analytics scripts.

Every script the M&A screen or the ma_* MCP tools invoke accepts one form:

    <script>.py <command> '<params JSON object>'

and prints exactly one JSON object to stdout:

    {"success": true,  "data": {...}}            exit 0
    {"success": false, "error": "<message>"}     exit 1

MAAnalyticsService unwraps "data" before handing the result to the UI / MCP.

Inputs: a value the model needs that is not company data (a discount rate, a
tax rate, a probability...) is a REQUIRED input -- the helpers below raise a
readable error instead of silently substituting a default.

Output naming (the UI formats by key, see MAModulePanel::format_value):
    *_pct   percentage expressed in percent units (12.5 == 12.5 %)
    *_x     multiple (8.5 == 8.5x)
    other   plain number (amounts, counts, per-share values)
NaN / inf are emitted as null (rendered as unavailable), never as 0.
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, List, Optional

_ANALYTICS = str(Path(__file__).resolve().parent.parent)
if _ANALYTICS not in sys.path:
    sys.path.insert(0, _ANALYTICS)


class InputError(ValueError):
    """A required input is missing or out of range."""


def is_json_call(argv: Optional[List[str]] = None) -> bool:
    argv = sys.argv if argv is None else argv
    return len(argv) == 3 and argv[2].lstrip().startswith('{')


def _clean(o: Any) -> Any:
    """JSON-safe copy: NaN/inf -> None, numpy scalars/arrays -> Python."""
    if isinstance(o, dict):
        return {str(k): _clean(v) for k, v in o.items()}
    if isinstance(o, (list, tuple)):
        return [_clean(v) for v in o]
    if isinstance(o, bool) or o is None or isinstance(o, str):
        return o
    if hasattr(o, 'tolist') and not isinstance(o, (int, float)):
        return _clean(o.tolist())
    if hasattr(o, 'item') and not isinstance(o, (int, float)):
        o = o.item()
    if isinstance(o, float):
        return o if math.isfinite(o) else None
    if isinstance(o, int):
        return o
    if hasattr(o, '__dict__'):
        return _clean(vars(o))
    return str(o)


def emit(data: Any) -> None:
    print(json.dumps({"success": True, "data": _clean(data)}))
    sys.exit(0)


def fail(message: str) -> None:
    print(json.dumps({"success": False, "error": message}))
    sys.exit(1)


def run_json(handlers: Dict[str, Callable[[Dict[str, Any]], Any]],
             argv: Optional[List[str]] = None) -> None:
    """Dispatch `<command> '<json>'` to handlers[command](params) and emit."""
    argv = sys.argv if argv is None else argv
    command = argv[1]
    handler = handlers.get(command)
    if handler is None:
        fail(f"Unknown command '{command}'. Available: {', '.join(sorted(handlers))}")
    try:
        params = json.loads(argv[2])
        if not isinstance(params, dict):
            raise InputError("params must be a JSON object")
        data = handler(params)
    except Exception as e:  # noqa: BLE001 -- surfaced to the caller verbatim
        fail(str(e) or e.__class__.__name__)
    emit(data)


# ── Input accessors ──────────────────────────────────────────────────────────

def _label(key: str, label: Optional[str]) -> str:
    return label or key


def has(p: Dict[str, Any], key: str) -> bool:
    return p.get(key) is not None and p.get(key) != ''


def num(p: Dict[str, Any], key: str, *, label: Optional[str] = None,
        min: Optional[float] = None, max: Optional[float] = None,
        gt: Optional[float] = None, integer: bool = False) -> float:
    """Required finite number with optional bounds."""
    if not has(p, key):
        raise InputError(f"Missing required input: {_label(key, label)}")
    try:
        v = float(p[key])
    except (TypeError, ValueError):
        raise InputError(f"{_label(key, label)} must be a number (got {p[key]!r})")
    if not math.isfinite(v):
        raise InputError(f"{_label(key, label)} must be finite")
    if min is not None and v < min:
        raise InputError(f"{_label(key, label)} must be >= {min} (got {v})")
    if max is not None and v > max:
        raise InputError(f"{_label(key, label)} must be <= {max} (got {v})")
    if gt is not None and v <= gt:
        raise InputError(f"{_label(key, label)} must be > {gt} (got {v})")
    if integer:
        if v != int(v):
            raise InputError(f"{_label(key, label)} must be a whole number (got {v})")
        return int(v)
    return v


def opt_num(p: Dict[str, Any], key: str, default: Optional[float] = None, **kw) -> Optional[float]:
    """Optional number: `default` when absent (use only for genuinely optional
    inputs whose absence has an exact meaning, e.g. 0 synergies)."""
    if not has(p, key):
        return default
    return num(p, key, **kw)


def num_list(p: Dict[str, Any], key: str, *, label: Optional[str] = None,
             min_len: int = 1) -> List[float]:
    if not has(p, key):
        raise InputError(f"Missing required input: {_label(key, label)}")
    raw = p[key]
    if isinstance(raw, str):
        raw = [s for s in raw.replace(';', ',').split(',') if s.strip()]
    if not isinstance(raw, (list, tuple)):
        raise InputError(f"{_label(key, label)} must be a list of numbers")
    out = []
    for i, x in enumerate(raw):
        try:
            v = float(x)
        except (TypeError, ValueError):
            raise InputError(f"{_label(key, label)}[{i}] must be a number (got {x!r})")
        if not math.isfinite(v):
            raise InputError(f"{_label(key, label)}[{i}] must be finite")
        out.append(v)
    if len(out) < min_len:
        raise InputError(f"{_label(key, label)} needs at least {min_len} value(s)")
    return out


def text(p: Dict[str, Any], key: str, *, label: Optional[str] = None,
         choices: Optional[Iterable[str]] = None) -> str:
    if not has(p, key):
        raise InputError(f"Missing required input: {_label(key, label)}")
    v = str(p[key]).strip()
    if choices is not None:
        choices = list(choices)
        if v not in choices:
            raise InputError(f"{_label(key, label)} must be one of {', '.join(choices)} (got {v!r})")
    return v


def obj(p: Dict[str, Any], key: str, *, label: Optional[str] = None) -> Dict[str, Any]:
    v = p.get(key)
    if not isinstance(v, dict):
        raise InputError(f"Missing required input: {_label(key, label)} (object)")
    return v


def obj_list(p: Dict[str, Any], key: str, *, label: Optional[str] = None,
             min_len: int = 1) -> List[Dict[str, Any]]:
    v = p.get(key)
    if not isinstance(v, list) or not all(isinstance(x, dict) for x in v):
        raise InputError(f"Missing required input: {_label(key, label)} (list of objects)")
    if len(v) < min_len:
        raise InputError(f"{_label(key, label)} needs at least {min_len} entr{'y' if min_len == 1 else 'ies'}")
    return v


def pct(fraction: Optional[float]) -> Optional[float]:
    """Fraction -> percent units for *_pct output keys (None passes through)."""
    if fraction is None:
        return None
    try:
        f = float(fraction)
    except (TypeError, ValueError):
        return None
    return f * 100.0 if math.isfinite(f) else None
