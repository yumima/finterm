"""number_grounding.py — a model may quote a figure, never originate one.

The Python twin of app/src/ai_chat/NumberGrounding.h, for replies produced by
the agent runtime (agents, teams, workflows), which do not pass through the
C++ LlmService. THE TWO MUST AGREE: the rules, the exemptions and the marker
are the same, and tests/scripts/test_number_grounding.py pins the same cases
as tests/ai_chat/test_number_grounding.cpp. Change one, change both.

Every number in a reply is checked against the EVIDENCE — the text the model
was actually given (instructions, the query, every tool result). It survives
when it is in the evidence, allowing only for how it is written (rounding to
the digits stated, separators, K/M/B/T scales, percent vs fraction), or when
it is arithmetic on grounded figures stated in the same sentence. Anything
else is replaced by MARKER.

Not figures: source code (a fence tagged with a programming language, an
inline span that is an expression), list markers, identifiers (Q3, 10-K, S-1,
13F), index and rule names with their own number (S&P 500, Rule 144), bare
integers 0-10, and bare past or current years. A future year is a claim and is
checked. A figure merely wrapped in backticks is still a figure. A date or
clock time in the evidence grounds a date in the reply, never an amount.
"""

from __future__ import annotations

import bisect
import datetime as _dt
import json
import math
import re
from dataclasses import dataclass, field
from typing import Any, Iterable, List, Optional, Tuple

MARKER = "[unverified]"

DIRECTIVE = (
    "FIGURES: state a number only if it appears in the material supplied to you in this request "
    "(the conversation, the data provided, tool results). Never estimate, recall from memory, "
    "forecast or invent a figure. If you compute one, state the inputs it was computed from in the "
    "same sentence. If a figure is not in the material, say it is not available. "
    "Any figure that cannot be traced to the supplied material is removed before the user sees it."
)

_NUMBER = re.compile(
    r"([$€£¥])?"
    r"(\d{1,3}(?:,\d{3})+|\d+)(\.\d+)?([eE][-+]?\d+)?"
    r"(?:(\s?%|\s?percent\b|\s?bps\b|\s?basis points\b)"
    r"|(\s?(?:thousand|million|billion|trillion)\b|(?:MM|mn|bn|tn|[KkMBT])(?![A-Za-z]))"
    r"|(x)(?![A-Za-z]))?"
)
_FENCE = re.compile(r"```([A-Za-z0-9_+#-]*)[^\n]*\n.*?(?:```|\Z)", re.S)
_LANG = re.compile(
    r"^(?:python|py|cpp|c\+\+|c|h|java|javascript|js|typescript|ts|sql|bash|sh|shell|zsh|r|rust|go|ruby|"
    r"php|swift|kotlin|scala|matlab|julia|pine|pinescript|html|css|yaml|toml|xml|dockerfile|cmake|make|"
    r"lua|perl|powershell|ps1|vb|vba|excel)$", re.I)
_SPAN = re.compile(r"`[^`\n]*`")
_CODEISH = re.compile(r"[()=_\[\]{};<>]|[A-Za-z]\.[A-Za-z]")
_MONTH = r"(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Sept|Oct|Nov|Dec)[a-z]*"
_DATE = re.compile(
    r"\d{4}-\d{2}-\d{2}(?:[T ]\d{2}:\d{2}(?::\d{2}(?:\.\d+)?)?)?"
    r"|\b\d{1,2}/\d{1,2}/\d{2,4}\b"
    r"|\b\d{1,2}:\d{2}(?::\d{2})?\b"
    r"|\b" + _MONTH + r"\.? \d{1,2}(?:st|nd|rd|th)?(?:,? \d{4})?"
    r"|\b\d{1,2}(?:st|nd|rd|th)? " + _MONTH + r"(?:,? \d{4})?")
_GAP = re.compile(r"^\s?(?:-|–|—|to)\s?$")
_LOWER_SCALE = re.compile(r"^(bn|mn|tn|m|b|t|k)(?![A-Za-z])")
_IDENT = re.compile(r"^(?:[A-Z](?![A-Za-z0-9])|(?:st|nd|rd|th)\b)")
_NAME_WORD = re.compile(r"([A-Za-z&]+(?: [A-Za-z]+)?)[ -]$")
_STOP = re.compile(r"[.!?;](?=\s)|\n")
_ANY_NUMBER = {"form", "rule", "section", "schedule", "item", "step", "phase", "tier", "basel", "regulation"}
_PAIRS = {
    "s&p 500", "s&p 400", "s&p 600", "s&p 100", "s&p 1500", "russell 1000", "russell 2000", "russell 3000",
    "nasdaq 100", "dow 30", "ftse 100", "ftse 250", "nikkei 225", "cac 40", "dax 40", "dax 30", "stoxx 50",
    "stoxx 600", "csi 300", "kospi 200", "asx 200", "tsx 60", "fortune 500", "wilshire 5000",
}


def _code_ranges(text: str) -> List[Tuple[int, int]]:
    """Ranges that are SOURCE CODE: a fence tagged with a programming language,
    an inline span that reads as an expression. Backticks alone exempt nothing."""
    out = [(m.start(), m.end()) for m in _FENCE.finditer(text) if _LANG.match(m.group(1) or "")]
    out += [(m.start(), m.end()) for m in _SPAN.finditer(text) if _CODEISH.search(m.group(0))]
    return out


@dataclass
class Num:
    value: float      # magnitude with the scale applied, sign dropped
    tol: float        # half a unit of the last digit written, scaled
    start: int
    end: int
    percent: bool = False
    currency: bool = False
    scaled: bool = False
    date: bool = False    # part of a date or clock time
    exempt: bool = False


def _scale(unit: str) -> Tuple[float, bool]:
    u = unit.lower()
    if unit == "K" or u in ("k", "thousand"):
        return 1e3, True
    if unit in ("M", "MM") or u in ("mn", "million"):
        return 1e6, True
    if unit == "B" or u in ("bn", "billion"):
        return 1e9, True
    if unit == "T" or u in ("tn", "trillion"):
        return 1e12, True
    return 1.0, False


def _follows_name(text: str, pos: int, digits: str) -> bool:
    """Does the bare integer complete a proper name — "S&P 500", "Rule 144"?
    An index is matched WITH its own number, and the word must stand alone."""
    m = _NAME_WORD.search(text[max(0, pos - 24):pos])
    if not m:
        return False
    last = m.group(1).lower().split(" ")[-1]
    return last in _ANY_NUMBER or (last + " " + digits) in _PAIRS


def extract(text: str, current_year: int, lenient: bool = False) -> List[Num]:
    """Every number written in `text`. `lenient` reads evidence: nothing is
    skipped as an identifier or as code."""
    code = [] if lenient else _code_ranges(text)
    dates = [(m.start(), m.end()) for m in _DATE.finditer(text)]

    def within(rs, pos):
        return any(a <= pos < b for a, b in rs)

    out: List[Num] = []
    for m in _NUMBER.finditer(text):
        ds, end = m.start(2), m.end(0)
        if within(code, ds):
            continue
        prev = text[ds - 1] if ds > 0 else " "
        prev2 = text[ds - 2] if ds > 1 else " "
        nxt = text[end] if end < len(text) else " "
        if prev.isdigit() or (prev == "." and prev2.isdigit()):
            continue
        whole, frac, exp = m.group(2), m.group(3) or "", m.group(4) or ""
        has_unit = bool(m.group(5) or m.group(6) or m.group(7))
        plain_int = not frac and not exp and "," not in whole
        lower_scale, lower_len = 1.0, 0
        if not has_unit and m.group(1):
            lm = _LOWER_SCALE.match(text[end:end + 3])
            if lm:
                u = lm.group(1)
                lower_scale = 1e3 if u == "k" else 1e6 if u[0] == "m" else 1e9 if u[0] == "b" else 1e12
                lower_len = len(u)
        if not lenient:
            if prev.isalpha() or prev == "_":
                continue
            if not has_unit and plain_int and lower_len == 0:
                if _IDENT.match(text[end:end + 4]):
                    continue
                if nxt == "-" and end + 1 < len(text) and text[end + 1].isupper():
                    continue
                if _follows_name(text, ds, whole):
                    continue
            if prev == "-" and prev2.isupper() and plain_int:
                continue
        try:
            v = float(whole.replace(",", "") + frac + exp)
        except ValueError:
            continue
        unit = 10.0 ** -(len(frac) - 1 if frac else 0)
        if exp:
            unit *= 10.0 ** float(exp[1:])
        sc, is_scale = _scale((m.group(6) or "").strip())
        if lower_len:
            sc, is_scale = lower_scale, True
        is_date = within(dates, ds)
        if is_date:
            sc, is_scale = 1.0, False
        g5 = m.group(5) or ""
        n = Num(
            value=abs(v) * sc,
            tol=0.5 * unit * sc * (1.0 + 1e-9) + 1e-12,
            start=m.start(0),
            end=end + lower_len,
            percent=("%" in g5) or ("percent" in g5),
            currency=bool(m.group(1)),
            scaled=is_scale,
            date=is_date,
        )
        bare = not m.group(1) and not has_unit and plain_int
        if bare:
            ls = n.start
            while ls > 0 and text[ls - 1] in " \t":
                ls -= 1
            line_start = ls == 0 or text[ls - 1] == "\n"
            marker = line_start and nxt in ".)" and (end + 1 >= len(text) or text[end + 1].isspace())
            year = len(whole) == 4 and 1900 <= v <= current_year
            n.exempt = marker or v <= 10.0 or year
        out.append(n)
    # A range shares its unit: "5–10%", "$2 to $3 billion".
    for i in range(len(out) - 2, -1, -1):
        a, b = out[i], out[i + 1]
        if not _GAP.match(text[a.end:b.start]):
            continue
        if b.percent and not a.percent:
            a.percent, a.exempt = True, False
        if b.scaled and not a.scaled and a.value < 1e3:
            sc = 10.0 ** (3.0 * math.floor(math.log10(b.value) / 3.0)) if b.value > 0 else 1.0
            a.value *= sc
            a.tol *= sc
            a.scaled, a.exempt = True, False
    return out


class Evidence:
    """The figures a request was given: quantities, and (kept apart) the
    components of dates and clock times."""

    def __init__(self, current_year: Optional[int] = None):
        self.year = current_year or _dt.date.today().year
        self._values: List[float] = []
        self._dates: List[float] = []
        self._sorted = True

    def add(self, text: Any) -> None:
        if text is None:
            return
        if not isinstance(text, str):
            try:
                text = json.dumps(text, default=str)
            except Exception:
                text = str(text)
        for n in extract(text, self.year, lenient=True):
            (self._dates if n.date else self._values).append(n.value)
        self._sorted = False

    @staticmethod
    def _in(xs: List[float], v: float, tol: float) -> bool:
        i = bisect.bisect_left(xs, v - tol)
        return i < len(xs) and xs[i] <= v + tol

    def has(self, v: float, tol: float) -> bool:
        if not self._sorted:
            self._values.sort()
            self._dates.sort()
            self._sorted = True
        return self._in(self._values, v, tol)

    def grounds(self, n: Num) -> bool:
        if self.has(n.value, n.tol):
            return True
        amount = n.percent or n.currency or n.scaled
        # The day in "2026-11-14" grounds a date in the reply, never "14%".
        if not amount and self._in(self._dates, n.value, n.tol):
            return True
        if n.percent and self.has(n.value / 100.0, n.tol / 100.0):
            return True
        if n.scaled:
            # Evidence quoted "in thousands"/"in millions" — only where the
            # quotient is itself a real figure, or any stray 3 grounds "$3 billion".
            return any(n.value / k >= 100.0 and self.has(n.value / k, n.tol / k) for k in (1e3, 1e6))
        return False


def _derived(n: Num, ops: List[float]) -> bool:
    def hit(x: float) -> bool:
        if not math.isfinite(x):
            return False
        x = abs(x)
        return abs(x - n.value) <= n.tol or (n.percent and abs(x * 100.0 - n.value) <= n.tol)

    for i, a in enumerate(ops):
        for j, b in enumerate(ops):
            if i == j:
                continue
            if hit(a - b) or hit(a + b) or hit(a * b) or hit(a * b / 100.0):
                return True
            if b != 0 and (hit(a / b) or hit((a - b) / b)):
                return True
    return False


@dataclass
class Result:
    text: str
    removed: List[str] = field(default_factory=list)
    checked: int = 0
    json: bool = False   # the reply is (or wraps) JSON a parser will read


def _check_text(text: str, ev: Evidence) -> Result:
    nums = extract(text, ev.year)
    if not nums:
        return Result(text)
    # A derivation is accepted only from operands in the SAME sentence that
    # are real grounded figures — never an exempt one (a list marker, "5
    # reasons", a year), which was not verified.
    breaks = [m.start() for m in _STOP.finditer(text)]
    para = [bisect.bisect_left(breaks, n.start) for n in nums]
    grounded = [(not n.exempt) and ev.grounds(n) for n in nums]
    ok = [n.exempt or g for n, g in zip(nums, grounded)]
    checked = sum(1 for n in nums if not n.exempt)
    for i, n in enumerate(nums):
        if ok[i]:
            continue
        ops = [nums[j].value for j in range(len(nums)) if j != i and para[j] == para[i] and grounded[j]][:8]
        ok[i] = _derived(n, ops)
    removed: List[str] = []
    out = text
    for i in range(len(nums) - 1, -1, -1):
        if ok[i]:
            continue
        removed.insert(0, text[nums[i].start:nums[i].end].strip())
        out = out[:nums[i].start] + MARKER + out[nums[i].end:]
    return Result(out, removed, checked)


def _check_json(v: Any, ev: Evidence, acc: Result) -> Any:
    if isinstance(v, str):
        r = _check_text(v, ev)
        acc.removed += r.removed
        acc.checked += r.checked
        return r.text
    if isinstance(v, bool) or v is None:
        return v
    if isinstance(v, (int, float)):
        written = repr(v) if isinstance(v, int) else format(v, ".15g")
        ns = extract(written, ev.year)
        if len(ns) != 1 or ns[0].exempt:
            return v
        acc.checked += 1
        if ev.grounds(ns[0]):
            return v
        acc.removed.append(written)
        return None
    if isinstance(v, list):
        return [_check_json(e, ev, acc) for e in v]
    if isinstance(v, dict):
        return {k: _check_json(e, ev, acc) for k, e in v.items()}
    return v


def enforce(reply: str, ev: Evidence) -> Result:
    """Enforce the rule on a whole reply; embedded JSON (an object, or an
    array such as a plan) stays parseable."""
    if not isinstance(reply, str) or not reply:
        return Result(reply if isinstance(reply, str) else "")
    ob, ab = reply.find("{"), reply.find("[")
    obj, arr = (ob, reply.rfind("}")), (ab, reply.rfind("]"))
    tries = [obj, arr] if ob >= 0 and (ab < 0 or ob < ab) else [arr, obj]
    for lo, hi in tries:
        if lo < 0 or hi <= lo:
            continue
        try:
            root = json.loads(reply[lo:hi + 1])
        except ValueError:
            continue
        if not isinstance(root, (dict, list)):
            continue
        acc = Result("")
        checked = _check_json(root, ev, acc)
        head, tail = _check_text(reply[:lo], ev), _check_text(reply[hi + 1:], ev)
        body = reply[lo:hi + 1] if not acc.removed else json.dumps(checked, ensure_ascii=False)
        return Result(head.text + body + tail.text, head.removed + acc.removed + tail.removed,
                      head.checked + acc.checked + tail.checked, json=True)
    return _check_text(reply, ev)


def footnote(removed: int) -> str:
    return "\n\n(%d %s removed: not in the data this answer was given.)" % (
        removed, "figure was" if removed == 1 else "figures were")


def evidence_from(*sources: Any, current_year: Optional[int] = None) -> Evidence:
    """Evidence from whatever a run can show for itself: strings, dicts, and
    agent run outputs (their messages and tool executions, members included)."""
    ev = Evidence(current_year)
    seen: set = set()

    def walk(x: Any, depth: int = 0) -> None:
        if x is None or depth > 6 or id(x) in seen:
            return
        if isinstance(x, dict) and "role" in x:
            # A chat message as a dict: what the model was GIVEN counts, its
            # own earlier turns do not (a figure must not ground itself).
            if x.get("role") != "assistant":
                walk(x.get("content"), depth + 1)
            return
        if isinstance(x, (str, int, float, dict)):
            ev.add(x)
            return
        seen.add(id(x))
        if isinstance(x, (list, tuple)):
            for e in x:
                walk(e, depth + 1)
            return
        # A run output / message / tool execution: read what it was shown,
        # never its own answer text (.content of the output itself is the
        # reply being checked; on a MESSAGE it is what the model was given).
        for attr in ("messages", "tools", "member_responses", "additional_input", "input"):
            walk(getattr(x, attr, None), depth + 1)
        role = getattr(x, "role", None)
        if role is not None and role != "assistant":
            walk(getattr(x, "content", None), depth + 1)
        for attr in ("tool", "result", "tool_args", "tool_result"):
            walk(getattr(x, attr, None), depth + 1)

    for s in sources:
        walk(s)
    return ev


def ground(reply: str, *sources: Any) -> str:
    """Reply with every untraceable figure removed, and a note saying so."""
    r = enforce(reply, evidence_from(*sources))
    if not r.removed:
        return reply
    return r.text if r.json else r.text + footnote(len(r.removed))
