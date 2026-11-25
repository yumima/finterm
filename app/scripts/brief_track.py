"""The public record of the weekly brief's stance: every one published, and what
the S&P 500 did after it.

site/_data/calls.json, kept by publishing (brief_drafts.publish) and shown by
site/track-record.md. A week's first published stance is the one that counts;
a later edition that says otherwise is listed beside it, never instead of it.
Everything is measured from the first close AFTER publication: what the market
did before a stance was public is no part of its record. Outcomes are
directions and the rule's total against always being invested — the index
itself may not be republished.
"""

import json
from datetime import datetime, time, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

QUARTER = 63  # sessions, as in the backtest (plans/research/market_regime)
ET = ZoneInfo("America/New_York")


def call_of(regime, stance, label):
    """What a published issue carries of its stance (issue.json "call")."""
    return {"close": regime["date"].isoformat(), "state": regime["state"], "label": label, "stance": stance}


def add(data, issue, now):
    """The record with @issue's stance in it (issue.json's contents), published
    at @now (an aware time)."""
    call = {k: v for k, v in (issue.get("call") or {}).items() if k in ("close", "state", "label", "stance")}
    if not call:
        return data
    at = now.astimezone(ET).isoformat(timespec="minutes")
    calls = [dict(c) for c in data.get("calls") or []]
    mine = next((c for c in calls if c["issue"] == issue["issue"]), None)
    if mine is None:
        calls.append({"issue": issue["issue"], "url": issue.get("url", ""), **call, "published": at})
    elif (last := (mine.get("revised") or [mine])[-1])["state"] != call["state"]:
        mine.setdefault("revised", []).append({**call, "published": at})
    calls.sort(key=lambda c: c["published"])
    return {**data, "calls": calls}


def _close(d):
    """A session's close: 13:00 on the half days (the day after Thanksgiving,
    December 24, July 3)."""
    half = (d.month == 11 and d.weekday() == 4 and 23 <= d.day <= 29) or (d.month, d.day) in ((12, 24), (7, 3))
    return time(13, 0) if half else time(16, 0)


def _from(published, days):
    """Index of the first close after publication, or None when not yet."""
    p = datetime.fromisoformat(published).astimezone(ET)
    first = p.date() if p.time() < _close(p.date()) else p.date() + timedelta(days=1)
    return next((i for i, d in enumerate(days) if d >= first), None)


def outcomes(data, prices):
    """The record with what came of each call. @prices: [(date, close)], oldest
    first. A window that has not run its course is pending (None), except a
    10% fall, which is known the day it happens."""
    px = sorted(prices)
    days = [d for d, _ in px]
    out, starts = [], []
    for c in data.get("calls") or []:
        c = dict(c)
        at = _from(c["published"], days)
        week = quarter = fall = None
        if at is not None:
            starts.append((at, c["stance"]))
            p0 = px[at][1]
            due = days[at] + timedelta(days=7)
            if days[-1] >= due:
                i = max(k for k, d in enumerate(days) if d <= due)
                week = "up" if px[i][1] > p0 else "down"
            ahead = px[at + 1:at + 1 + QUARTER]
            if len(ahead) == QUARTER:
                quarter = "up" if ahead[-1][1] > p0 else "down"
            if any(p <= 0.9 * p0 for _, p in ahead):
                fall = "yes"
            elif len(ahead) == QUARTER:
                fall = "no"
        c.update(since=days[at].isoformat() if at is not None else None, week=week, quarter=quarter, fall=fall)
        out.append(c)
    total = {}
    if starts:
        rule = always = 1.0
        stance, k = None, 0
        for i in range(starts[0][0] + 1, len(px)):
            while k < len(starts) and starts[k][0] <= i - 1:  # held from that close on
                stance, k = starts[k][1], k + 1
            r = px[i][1] / px[i - 1][1] - 1
            rule *= 1 + stance * r
            always *= 1 + r
        if days[-1] > days[starts[0][0]]:
            total = {"rule": round((rule - 1) * 100, 1), "always": round((always - 1) * 100, 1),
                     "from": days[starts[0][0]].isoformat(), "to": days[-1].isoformat()}
    scored = [c for c in out if c["week"]]
    return {**data, "calls": out, "total": total,
            "weeks": {"scored": len(scored), "up": sum(c["week"] == "up" for c in scored)}}


def update(path, issue, now, prices):
    """Keep @path (calls.json) for a published @issue; with no @prices the calls
    keep the outcomes they had. A file that cannot be read is left alone: a
    record is never started over on top of one."""
    path = Path(path)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data.get("calls"), list):
            raise ValueError("no calls")
    except FileNotFoundError:
        data = {"calls": []}
    except (OSError, ValueError, AttributeError) as e:
        print(f"track record: {path.name} is unreadable ({e}); left as it is")
        return None
    data = add(data, issue, now)
    if prices:
        data = outcomes(data, prices)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return data
