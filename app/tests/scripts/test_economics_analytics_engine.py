#!/usr/bin/env python3
"""Analytics/economics/analytics_engine.py — each analyzer's own dispatcher.

The file was once spliced out of order: StatisticalAnalyzer._analyze_trend
held ForecastingEngine's linear-forecast body (an undefined `periods`, so a
NameError on every time-series call), and StatisticalAnalyzer's seasonality
and dispatcher sat inside ScenarioAnalyzer, shadowing its own calculate().
Pinned here:
  - StatisticalAnalyzer.calculate('time_series_analysis') runs and reports a
    trend (slope, R², p-value, direction) and a seasonality section;
  - ScenarioAnalyzer.calculate dispatches scenario analyses, not statistics;
  - ForecastingEngine.calculate('simple_methods') runs its linear trend.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

try:
    import numpy as np
    import pandas as pd
    import scipy  # noqa: F401
except ImportError:  # the engine needs the numeric stack; report SKIPPED
    sys.exit(77)

from Analytics.economics.analytics_engine import (  # noqa: E402
    StatisticalAnalyzer, ScenarioAnalyzer, ForecastingEngine,
)

FAILURES = []


def check(name, cond, detail=""):
    print(("PASS : " if cond else "FAIL : ") + name + ("" if cond else f" {detail}"))
    if not cond:
        FAILURES.append(name)


idx = pd.date_range("2020-01-31", periods=48, freq="ME")
rng = np.random.default_rng(7)
series = pd.Series(100 + 0.5 * np.arange(48) + 3 * np.sin(np.arange(48) * 2 * np.pi / 12)
                   + rng.normal(0, 0.3, 48), index=idx)

res = StatisticalAnalyzer().calculate("time_series_analysis", data=series)
trend = res.get("trend_analysis", {})
check("time_series_analysis runs", res.get("analysis_type") == "time_series_analysis", res)
check("trend reports slope and R²", "linear_trend" in trend and "r_squared" in trend["linear_trend"], trend)
check("rising series reads Increasing", trend.get("trend_direction") == "Increasing", trend.get("trend_direction"))
check("seasonality section present", "seasonality_analysis" in res, list(res))

try:
    ScenarioAnalyzer().calculate("time_series_analysis", data=series)
    check("ScenarioAnalyzer does not dispatch statistics", False, "accepted time_series_analysis")
except Exception as e:
    check("ScenarioAnalyzer does not dispatch statistics", "Unknown analysis type" in str(e), e)

fc = ForecastingEngine().calculate("simple_methods", data=series, forecast_periods=3,
                                   methods=["linear_trend"])
lin = fc.get("forecasts", {}).get("linear_trend", {})
check("ForecastingEngine linear trend forecasts 3 periods", len(lin.get("forecast_values", [])) == 3,
      str(fc)[:200])

if FAILURES:
    print(f"\n{len(FAILURES)} failure(s)")
    sys.exit(1)
print("\nall passed")
