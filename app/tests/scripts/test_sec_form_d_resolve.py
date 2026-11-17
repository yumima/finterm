#!/usr/bin/env python3
"""resolve_cik must not bind a company to a feeder vehicle named after it.

2026-11-17: Anthropic's dossier resolved to "Anthropic - Incepto AGI Funds,
LLC" — the only filer whose name began with "anthropic" — because the feeder
test knew "fund" but not "funds". Its Form D officers were then shown as
Anthropic's leadership. No network: search_filings is replaced.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import sec_form_d_data as fd  # noqa: E402


def hits(*names):
    return [{"ciks": [str(1000 + i)], "display_names": [f"{n}  (CIK {1000 + i:010d})"], "filed_date": "2026-01-01"}
            for i, n in enumerate(names)]


def run(query, names):
    fd.search_filings = lambda *a, **k: hits(*names)
    fd._LAST_SEARCH_FAILED = False
    fd._LAST_SEARCH_PARTIAL = False
    return fd.resolve_cik(query)


def main():
    failures = []

    def check(cond, msg):
        print(("ok   " if cond else "FAIL ") + msg)
        if not cond:
            failures.append(msg)

    r = run("Anthropic", ["Anthropic - Incepto AGI Funds, LLC"])
    check(r["cik"] == "", f"a feeder named '... Funds, LLC' is not the company: {r}")
    for feeder in ["Anduril Investors LLC", "Stripe Partners SPV I", "Databricks Vehicle LLC"]:
        r = run(feeder.split()[0], [feeder])
        check(r["cik"] == "", f"'{feeder}' is a feeder, not the company: {r}")
    r = run("Ramp", ["Ramp Business Corp"])
    check(r["cik"] == "0000001000", f"a unique operating-company prefix still resolves: {r}")
    r = run("Anthropic", ["Anthropic PBC", "Anthropic - Incepto AGI Funds, LLC"])
    check(r["cik"] == "0000001000", f"the exact name wins over the feeder: {r}")
    if failures:
        sys.exit(1)


if __name__ == "__main__":
    main()
