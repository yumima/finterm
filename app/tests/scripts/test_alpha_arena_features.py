#!/usr/bin/env python3
"""Alpha Arena technical features come from agno_trading's indicators.

agno_trading/__init__.py used to import modules that were never committed,
so `from agno_trading.tools.technical_indicators import ...` always raised
ImportError and DefaultFeaturesPipeline returned an empty TechnicalFeatures
for every symbol. Pinned here: the import works, and with 60 prices the
pipeline fills the moving averages, RSI, MACD and Bollinger fields.
"""

import asyncio
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

try:
    import pydantic  # noqa: F401  (alpha_arena's types)
except ImportError:
    sys.exit(77)

FAILURES = []


def check(name, cond, detail=""):
    print(("PASS : " if cond else "FAIL : ") + name + ("" if cond else f" {detail}"))
    if not cond:
        FAILURES.append(name)


try:
    from agno_trading.tools.technical_indicators import calculate_sma  # noqa: F401
    check("agno_trading.tools.technical_indicators imports", True)
except ImportError as e:
    check("agno_trading.tools.technical_indicators imports", False, e)

from alpha_arena.core.features_pipeline import DefaultFeaturesPipeline  # noqa: E402

prices = [100 + 5 * math.sin(i / 3) + 0.2 * i for i in range(60)]
pipe = DefaultFeaturesPipeline(enable_technical=True, enable_sentiment=False)
tech = asyncio.run(pipe._compute_technical(prices, prices[-1]))
for field in ("sma_20", "sma_50", "ema_12", "ema_26", "rsi_14", "macd", "bollinger_upper"):
    check(f"technical feature {field} filled", getattr(tech, field, None) is not None, getattr(tech, field, None))
check("SMA-20 is the mean of the last 20", abs(tech.sma_20 - sum(prices[-20:]) / 20) < 1e-9, tech.sma_20)

if FAILURES:
    print(f"\n{len(FAILURES)} failure(s)")
    sys.exit(1)
print("\nall passed")
