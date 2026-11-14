#!/usr/bin/env python3
"""Quote prices keep their information (yfinance_data._round_price).

Every batch quote passes through _round_price. Prices from 1 to 100 were
rounded to 2 decimals, so EURUSD=X 1.105021 became 1.11 and the Markets WORLD
board showed EUR/USD 1.1100 — and, priced against an unrounded reference, a
+0.42% day when the pair fell 0.29%. Runs the SHIPPED function, dependencies
stubbed (no network).
"""

import os
import sys
import types

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
for _name in ("yfinance", "pandas", "numpy", "curl_cffi", "requests"):
    if _name not in sys.modules:
        try:
            __import__(_name)
        except ImportError:
            sys.modules[_name] = types.ModuleType(_name)

import yfinance_data as yd  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    print(("PASS : " if cond else "FAIL : ") + name + ("" if cond else f" {detail}"))
    if not cond:
        FAILURES.append(name)


def main():
    r = yd._round_price
    check("EUR/USD keeps 5 decimals", r(1.105021, "EURUSD=X") == 1.10502, r(1.105021))
    check("GBP/USD keeps 5 decimals", r(1.3208992) == 1.3209, r(1.3208992))
    check("USD/CNH keeps 5 decimals", r(6.6737799) == 6.67378, r(6.6737799))
    check("sub-unit keeps 6 decimals", r(0.677415) == 0.677415, r(0.677415))
    check("tiny keeps 10 decimals", r(0.00072123456) == 0.0007212346, r(0.00072123456))
    check("100 and up: 2 decimals", r(7583.004) == 7583.0 and r(159.875) == 159.88, (r(7583.004), r(159.875)))
    check("absent stays absent", r(None) is None)
    check("zero stays zero", r(0.0) == 0.0)
    if FAILURES:
        print(f"{len(FAILURES)} failure(s)")
        sys.exit(1)
    print("all passed")


if __name__ == "__main__":
    main()
