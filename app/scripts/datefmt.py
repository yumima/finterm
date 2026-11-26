"""strftime with the no-padding forms (%-d, %-H, %-m, %-I, %-M) on every OS.

"%-d" is a glibc/BSD extension: Windows' strftime raises "Invalid format
string", which broke the IPO calendar's projections and the weekly brief there
(found when the tests first ran on Windows, 2026-11-26).
"""
import re

_NOPAD = re.compile(r"%-([dHmIM])")
_FIELD = {"d": "day", "H": "hour", "m": "month", "M": "minute"}


def fmt(d, spec):
    """d.strftime(spec), with %-X written out as the unpadded number."""
    def one(m):
        k = m.group(1)
        if k == "I":
            return str(d.hour % 12 or 12)
        return str(getattr(d, _FIELD[k]))
    return d.strftime(_NOPAD.sub(lambda m: one(m).replace("%", "%%"), spec))
