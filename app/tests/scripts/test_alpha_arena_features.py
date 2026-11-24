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

# API keys: never in what is saved or returned, and taken out of what was.
import json  # noqa: E402
import sqlite3  # noqa: E402
import tempfile  # noqa: E402

from alpha_arena.core.database import AlphaArenaDatabase  # noqa: E402
from alpha_arena.types.models import CompetitionConfig, CompetitionModel, CompetitionStatus  # noqa: E402

KEY = "sk-must-not-be-saved"
cfg = CompetitionConfig(competition_id="c1", competition_name="t", symbols=["BTC/USD"], models=[
    CompetitionModel(name=n, provider="openai", model_id="m", api_key=KEY) for n in ("a", "b")])
check("the model itself holds its key", cfg.models[0].api_key == KEY)
check("a dump does not", KEY not in json.dumps(cfg.model_dump(), default=str))
check("nor its repr", KEY not in repr(cfg))
with tempfile.TemporaryDirectory() as d:
    path = os.path.join(d, "a.db")
    AlphaArenaDatabase(path).save_competition(cfg, CompetitionStatus.CREATED)
    con = sqlite3.connect(path)
    old = json.loads(con.execute("SELECT config FROM competitions").fetchone()[0])
    for m in old["models"]:
        m["api_key"] = KEY  # as 4.1.0 and before saved it
    con.execute("UPDATE competitions SET config = ?", (json.dumps(old),))
    con.execute("INSERT INTO competitions SELECT 'c2', config, status, cycle_count, start_time, end_time, created_at, "
                "updated_at FROM competitions")
    con.commit()
    con.execute("DELETE FROM competitions WHERE competition_id = 'c2'")  # its page stays in the file
    con.execute("PRAGMA user_version = 0")
    con.commit()
    con.close()
    db = AlphaArenaDatabase(path)
    with open(path, "rb") as f:
        check("a key saved earlier is gone from the file", KEY.encode() not in f.read())
    check("...and the competition is still there", len(db.get_competition("c1")["config"]["models"]) == 2)


if FAILURES:
    print(f"\n{len(FAILURES)} failure(s)")
    sys.exit(1)
print("\nall passed")
