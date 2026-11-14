#!/usr/bin/env python3
"""Treasury daily par yield curve — the decode the Markets WORLD board reads.

Pinned, with the network stubbed out:
  - a blank or "N/A" tenor decodes as None (absent), never 0.0;
  - rows come back newest first whatever order the file uses;
  - a refused request is {"error": ...}, not an empty success;
  - early January (one row this year) reaches back into last year's file so a
    day-over-day change exists.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

try:
    import requests  # noqa: F401
except ImportError:  # the script needs requests; report SKIPPED, not passed
    sys.exit(77)

import treasury_data as td  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    print(("PASS : " if cond else "FAIL : ") + name + ("" if cond else f" {detail}"))
    if not cond:
        FAILURES.append(name)


HEADER = 'Date,"1 Mo","2 Yr","5 Yr","10 Yr","20 Yr","30 Yr"\n'


class _Resp:
    def __init__(self, text, status=200):
        self.text = text
        self.status_code = status

    def raise_for_status(self):
        if self.status_code >= 400:
            raise td.requests.exceptions.HTTPError(f"HTTP {self.status_code}")


def main():
    csv = HEADER + "11/12/2026,4.12,4.86,4.95,5.10,5.48,5.43\n11/13/2026,4.10,,4.98,5.15,5.52,N/A\n"
    rows = td.parse_par_curve_csv(csv)
    check("newest first", [r["date"] for r in rows] == ["2026-11-13", "2026-11-12"], rows)
    check("blank tenor is None, not 0", rows[0]["y2"] is None, rows[0])
    check("N/A tenor is None", rows[0]["y30"] is None, rows[0])
    check("numbers parse", rows[1]["y2"] == 4.86 and rows[0]["y10"] == 5.15, rows)
    check("a junk date line is skipped", td.parse_par_curve_csv(HEADER + "Total,1,2,3,4,5,6\n") == [])

    # A refused request is an error, not "no rows".
    orig = td.session.get
    try:
        td.session.get = lambda url, timeout=0: _Resp("", 503)
        out = td.get_par_curve()
        check("refusal is an error", "error" in out and "rows" not in out, out)

        # One row this year: last year's file is read too.
        calls = []

        def fake(url, timeout=0):
            calls.append(url)
            if len(calls) == 1:
                return _Resp(HEADER + "01/02/2027,4.1,4.20,4.3,4.40,4.5,4.6\n")
            return _Resp(HEADER + "12/31/2026,4.1,4.25,4.3,4.38,4.5,4.6\n")

        td.session.get = fake
        out = td.get_par_curve()
        check("reaches into last year", len(calls) == 2 and len(out.get("rows", [])) == 2, out)
        check("cross-year order", [r["date"] for r in out["rows"]] == ["2027-01-02", "2026-12-31"], out)

        # 1 January: the new year's file does not exist yet — last year's answers.
        calls.clear()

        def jan1(url, timeout=0):
            calls.append(url)
            if len(calls) == 1:
                return _Resp("", 404)
            return _Resp(HEADER + "12/31/2026,4.1,4.25,4.3,4.38,4.5,4.6\n12/30/2026,4.1,4.2,4.3,4.3,4.5,4.6\n")

        td.session.get = jan1
        out = td.get_par_curve()
        check("missing new-year file falls back", len(out.get("rows", [])) == 2 and "error" not in out, out)
    finally:
        td.session.get = orig

    if FAILURES:
        print(f"{len(FAILURES)} failure(s)")
        sys.exit(1)
    print("all passed")


if __name__ == "__main__":
    main()
