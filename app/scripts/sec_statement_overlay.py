#!/usr/bin/env python3
"""Statement lines shown as the company FILED them, where Yahoo's differ.

Measured (fundamentals_crosscheck.py, 300 companies, FY2022-2026): Yahoo's
"Operating Income" is its own adjusted figure and differs from the filed
OperatingIncomeLoss in 58% of company-years — RXO 2024 is +$30M in Yahoo and
-$56M in the 10-K — and a handful of equity lines are 0 where the company
filed a real number. A line labelled "Operating Income" should be the one the
company reported.

What this changes, and only this:

  operating income   A whole series or none of it, so a table never mixes the
                     two bases: the annual columns are switched only if the
                     company filed the line for EVERY year Yahoo shows, and the
                     quarterly columns (and the TTM sum built on them) only if
                     it filed EVERY quarter Yahoo shows. A fourth quarter is
                     never filed alone; it is the filed year minus the filed
                     nine months — used only when every filing of the two gives
                     the same answer, and never for a year in which the company
                     reported discontinued operations (the year is then recast
                     in the 10-K before the nine months are, and the difference
                     would be a number nobody filed).
  equity             "Stockholders Equity" that Yahoo reports as exactly 0 when
                     the company filed a figure: replaced by the filed
                     StockholdersEquity, or — when only total equity including
                     minorities was filed — removed, because the zero is wrong
                     and the parent-only figure is not known.

Yahoo equal to ANY figure the company filed for a period (as first reported or
as restated) is already a filed figure and is left alone. Lines whose
disagreement has not been classified (revenue, EPS, cash, liabilities) are
not touched: replacing a right figure with a wrong reading of a filing would
be worse than the problem. Yahoo's other derived rows (EBIT, Total Expenses)
stay on Yahoo's basis and may not reconcile with a replaced operating income.

Each change is listed in the payload under `sec_overrides`. No SEC answer
(foreign filer, no contact configured, a refusal, a slow response) means no
change and a stated reason — never a guess. One short request per tag, no
retries, and a network failure is not retried for ten minutes; when SEC cannot
be reached, figures cached from an earlier day are used rather than dropping
back to Yahoo's basis, so the same cell does not change between two loads.
"""

from __future__ import annotations

import json
import math
import os
import threading
import time
from datetime import date
from typing import Any, Dict, List, Optional, Tuple

import fundamentals_crosscheck as fx

OPINC_TAGS = ("OperatingIncomeLoss",)
OPINC_ROWS = ("Operating Income",)
EQUITY_ROW = "Stockholders Equity"
EQUITY_TAG = "StockholdersEquity"
EQUITY_TOTAL_TAG = "StockholdersEquityIncludingPortionAttributableToNoncontrollingInterest"
DISCONTINUED_TAG = "IncomeLossFromDiscontinuedOperationsNetOfTax"
KEPT_TAGS = OPINC_TAGS + (EQUITY_TAG, EQUITY_TOTAL_TAG, DISCONTINUED_TAG)

FACTS_TTL_S = 24 * 3600
BACKOFF_S = 600
TIMEOUT = (4, 8)  # connect, read — the statements are already in hand; do not hold them long


def _latest(cands: List[Dict[str, Any]]) -> Optional[Dict[str, Any]]:
    """The figure as most recently filed (a restatement supersedes the original)."""
    return max(cands, key=lambda c: (c.get("filed") or "", c.get("accn") or "")) if cands else None


def _same(a: float, b: float) -> bool:
    return abs(a - b) <= fx.REL_TOL * max(abs(a), abs(b), 1.0)


def _flows(facts: Dict[str, Any], tags: Tuple[str, ...], lo: int, hi: int,
           end: Optional[date] = None) -> List[Dict[str, Any]]:
    """Filed flow figures lasting lo..hi days — ending on `end` (± a week) when given."""
    out = []
    gaap = (facts.get("facts") or {}).get("us-gaap") or {}
    for tag in tags:
        for row in ((gaap.get(tag) or {}).get("units") or {}).get("USD") or []:
            e, s = fx._d(row.get("end", "")), fx._d(row.get("start", ""))
            if e is None or s is None or not isinstance(row.get("val"), (int, float)):
                continue
            if end is not None and abs((e - end).days) > fx.PERIOD_SLACK_DAYS:
                continue
            if lo <= (e - s).days <= hi:
                out.append({"tag": tag, "value": float(row["val"]), "start": row.get("start"), "end": row.get("end"),
                            "form": row.get("form"), "filed": row.get("filed"), "accn": row.get("accn")})
    return out


def filed_quarter_values(facts: Dict[str, Any], tags: Tuple[str, ...], end: date) -> Tuple[List[float], str]:
    """Every value the filings give for the three months ending `end`, and how.

    A quarter filed as such: each filing of it (as reported, as restated). A
    fourth quarter: the filed year minus the filed nine months of the same
    fiscal year, for every pairing of their filings — the caller uses it only
    when they all agree, so a year and a nine-month figure from different
    restatement vintages can never be subtracted into a number nobody filed.
    """
    derived_basis = f"{tags[0]} (year − nine months)"
    direct = _flows(facts, tags, 80, 100, end)
    if direct:
        # newest filing first
        direct.sort(key=lambda c: (c.get("filed") or "", c.get("accn") or ""), reverse=True)
        return [c["value"] for c in direct], direct[0]["tag"]
    out: List[float] = []
    for year in _flows(facts, tags, 350, 380, end):
        y_start = fx._d(year["start"])
        if y_start is None or _discontinued_in(facts, y_start, end):
            return [], derived_basis
        for nine in _flows(facts, tags, 260, 285):
            n_start = fx._d(nine["start"])
            if y_start and n_start and abs((n_start - y_start).days) <= fx.PERIOD_SLACK_DAYS:
                out.append(year["value"] - nine["value"])
    return out, derived_basis


def _discontinued_in(facts: Dict[str, Any], start: date, end: date) -> bool:
    """Did the company report a discontinued operation in a period ending
    inside this fiscal year? Then its year and its nine months may be on
    different bases, and their difference is not a filed quarter."""
    rows = ((((facts.get("facts") or {}).get("us-gaap") or {}).get(DISCONTINUED_TAG) or {}).get("units") or {}).get("USD") or []
    for row in rows:
        e = fx._d(row.get("end", ""))
        if e and start < e <= end and isinstance(row.get("val"), (int, float)) and row["val"] != 0:
            return True
    return False


def apply_overlay(payload: Dict[str, Any], facts: Dict[str, Any]) -> List[Dict[str, Any]]:
    """Make the changes described in the module docstring, in place. Returns
    the list of changes. Pure: no network — the tests drive this."""
    done: List[Dict[str, Any]] = []

    def yahoo_of(cells: Dict[str, Any], rows: Tuple[str, ...]) -> Optional[Tuple[str, float]]:
        return next(((r, float(cells[r])) for r in rows
                     if isinstance(cells.get(r), (int, float)) and math.isfinite(cells[r])), None)

    def record(stmt: str, period: str, row: str, old: Optional[float], new: Optional[float], basis: str) -> None:
        done.append({"statement": stmt, "period": str(period)[:10], "line": row, "yahoo": old, "sec": new,
                     "basis": basis})

    def switch_series(stmt: str, periods: List[str], filed_for) -> bool:
        """All of `periods` or none: plan every replacement first, apply only
        if every period Yahoo carries the line for has a filed figure."""
        plan = []
        for period in periods:
            cells = payload[stmt][period]
            y = yahoo_of(cells, OPINC_ROWS)
            end = fx._d(period)
            if y is None or end is None:
                continue  # a cell Yahoo left empty stays empty
            values, basis = filed_for(end)
            if not values:
                return False  # one period with nothing filed: the series stays Yahoo's
            if any(_same(y[1], v) for v in values):
                continue  # already a filed figure (as reported or restated)
            if basis.endswith("(year − nine months)") and not all(_same(values[0], v) for v in values):
                return False  # filings disagree on the derived quarter
            plan.append((period, y[0], y[1], values[0], basis))
        for period, row, old, new, basis in plan:
            payload[stmt][period][row] = new
            record(stmt, period, row, old, new, basis)
        return True

    annual = payload.get("income_statement") or {}

    def annual_filed(end: date) -> Tuple[List[float], str]:
        cands = fx.sec_candidates(facts, OPINC_TAGS, "USD", "flow", end)
        cands.sort(key=lambda c: (c.get("filed") or "", c.get("accn") or ""), reverse=True)
        return [c["value"] for c in cands], OPINC_TAGS[0]

    switch_series("income_statement", sorted(annual, reverse=True), annual_filed)

    # Every quarterly column moves together, and the TTM sum with them: three
    # filed quarters beside an adjusted one — or a filed quarter compared with
    # the adjusted one a year earlier — would be figures on two bases.
    quarterly = payload.get("quarterly_income_statement") or {}
    ttm = payload.get("ttm") if isinstance(payload.get("ttm"), dict) else None
    before = len(done)
    if quarterly and switch_series("quarterly_income_statement", sorted(quarterly, reverse=True),
                                   lambda end: filed_quarter_values(facts, OPINC_TAGS, end)):
        if len(done) > before and ttm is not None and "operating_income" in ttm:
            named = set(ttm.get("quarters") or [])
            four = [p for p in quarterly if str(p)[:10] in named] if len(named) == 4 else []
            if len(four) != 4:
                four = sorted(quarterly, reverse=True)[:4]
            vals = [yahoo_of(quarterly[p], OPINC_ROWS) for p in four]
            if len(vals) == 4 and all(v is not None for v in vals):
                ttm["operating_income"] = float(sum(v[1] for v in vals))

    for period, cells in (payload.get("balance_sheet") or {}).items():
        end = fx._d(period)
        if end is None or cells.get(EQUITY_ROW) != 0.0:
            continue  # only a reported ZERO is repaired ("missing became zero")
        parent = _latest(fx.sec_candidates(facts, (EQUITY_TAG,), "USD", "instant", end))
        total = _latest(fx.sec_candidates(facts, (EQUITY_TOTAL_TAG,), "USD", "instant", end))
        if parent and parent["value"] != 0.0:
            cells[EQUITY_ROW] = parent["value"]
            record("balance_sheet", period, EQUITY_ROW, 0.0, parent["value"], EQUITY_TAG)
        elif total and total["value"] != 0.0:
            # The zero is contradicted by a filed total, but the parent-only
            # figure this row means was not filed: absent, not a guess.
            del cells[EQUITY_ROW]
            record("balance_sheet", period, EQUITY_ROW, 0.0, None,
                   "removed: zero contradicted by filed total equity")
    return done


def filed_annual_operating_income(facts: Dict[str, Any], series: List[Dict[str, Any]]) -> Optional[List[Dict[str, Any]]]:
    """A [{date, value}] operating-income series on the filed basis — all of it
    or None, by the same rule as the statement table."""
    payload = {"income_statement": {r["date"]: {OPINC_ROWS[0]: r["value"]} for r in series}}
    before = {r["date"]: r["value"] for r in series}
    apply_overlay(payload, facts)
    after = [{"date": d, "value": payload["income_statement"][d][OPINC_ROWS[0]]} for d in before]
    return after if any(a["value"] != before[a["date"]] for a in after) else None


# ── Network side ─────────────────────────────────────────────────────────────

_lock = threading.Lock()
_backoff_until = 0.0          # monotonic; SEC failed, do not ask again before this
_no_facts_until: Dict[str, float] = {}


def _cache_path(*parts: str) -> str:
    import finterm_paths
    return os.path.join(str(finterm_paths.cache_dir("sec_facts")), *parts)


def _read_cache(path: str, ttl: float) -> Any:
    try:
        if time.time() - os.path.getmtime(path) < ttl:
            with open(path, encoding="utf-8") as f:
                return json.load(f)
    except (OSError, ValueError):
        pass
    return None


def _write_cache(path: str, obj: Any) -> None:
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp = f"{path}.{os.getpid()}.{threading.get_ident()}.tmp"  # two writers never share a temp file
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(obj, f)
        os.replace(tmp, path)
    except OSError:
        pass


class _NoSuch(Exception):
    """SEC answered 404: the thing asked for does not exist (not a failure)."""


class _Unavailable(Exception):
    """SEC refused or did not answer: unknown, not "does not exist"."""


def _get(url: str) -> Any:
    """One short SEC request (sec_http: contact identity, shared pacing, no
    retries — the statements are already in hand; do not hold them long)."""
    import sec_http
    r, status = sec_http.get(url, timeout=TIMEOUT)
    if status == 404:
        raise _NoSuch(url)
    if r is None:
        raise _Unavailable(f"HTTP {status}" if status else "SEC did not answer")
    return r.json()


def cik(symbol: str) -> Optional[str]:
    """CIK for a ticker (fundamentals_crosscheck.cik_for — sec_directory)."""
    return fx.cik_for(symbol)


def _facts(cik_: str) -> Optional[Dict[str, Any]]:
    """The tags this module reads, for one registrant: one small request per
    tag (the whole company-facts document is megabytes). None when the
    registrant reports none of them. On a network failure, figures cached on an
    earlier day are returned rather than nothing."""
    path = _cache_path(f"CIK{int(cik_):010d}.json")
    slim = _read_cache(path, FACTS_TTL_S)
    if isinstance(slim, dict) and "facts" in slim:
        return slim
    try:
        gaap: Dict[str, Any] = {}
        for tag in KEPT_TAGS:
            try:
                doc = _get(f"https://data.sec.gov/api/xbrl/companyconcept/CIK{int(cik_):010d}/us-gaap/{tag}.json")
            except _NoSuch:
                continue  # the company does not report this tag
            if isinstance(doc, dict) and isinstance(doc.get("units"), dict):
                gaap[tag] = {"units": doc["units"]}
    except Exception:
        stale = _read_cache(path, float("inf"))
        if isinstance(stale, dict) and "facts" in stale:
            return stale
        raise
    slim = {"facts": {"us-gaap": gaap}}
    _write_cache(path, slim)  # an empty set is cached too: "reports none of them" is an answer
    return slim if gaap else None


def facts_for(symbol: str) -> Tuple[Optional[Dict[str, Any]], Dict[str, Any]]:
    """(facts or None, status). Never raises. A NETWORK failure starts a
    back-off so a slow or refusing SEC is paid for once, not on every
    statement; a failure particular to one symbol does not."""
    global _backoff_until
    now = time.monotonic()
    with _lock:
        if now < _backoff_until:
            return None, {"status": "unavailable", "reason": "SEC unavailable; retrying later"}
        if now < _no_facts_until.get(symbol.upper(), 0.0):
            return None, {"status": "no_facts"}
    try:
        cik_ = cik(symbol)
        if not cik_:
            return None, {"status": "not_a_registrant"}
        facts = _facts(cik_)
        if facts is None:
            with _lock:
                _no_facts_until[symbol.upper()] = now + FACTS_TTL_S
            return None, {"status": "no_facts", "cik": cik_}
        return facts, {"status": "applied", "cik": cik_}
    except (_Unavailable, fx.SourceUnavailable, _NoSuch, OSError) as e:
        with _lock:
            _backoff_until = time.monotonic() + BACKOFF_S
        return None, {"status": "unavailable", "reason": f"{type(e).__name__}: {str(e)[:120]}"}
    except Exception as e:  # noqa: BLE001 — this symbol's document, not SEC: no global back-off
        with _lock:
            _no_facts_until[symbol.upper()] = now + BACKOFF_S
        return None, {"status": "error", "reason": f"{type(e).__name__}: {str(e)[:120]}"}


def overlay(symbol: str, payload: Dict[str, Any]) -> None:
    """Apply the filed figures to a get_financials payload, in place, and
    record what was done under `sec_overrides` / `sec_overlay`."""
    facts, status = facts_for(symbol)
    if facts is not None:
        done = apply_overlay(payload, facts)
        payload["sec_overrides"] = done
        status["replaced"] = len(done)
    payload["sec_overlay"] = status
