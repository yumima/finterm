"""The app's entry point (AlphaArenaService): one JSON request, one JSON answer.

    {"action": ..., "params": {...}, "connections": [...]}    (see llm.py)
"""

from __future__ import annotations

import contextlib
import json
import sys
import time

from . import arena, llm, market, news
from .store import Busy, Store


def _quotes(params: dict) -> dict:
    """What each exchange says of the symbol now."""
    rows = []
    for exchange in market.EXCHANGES:
        try:
            rows.append(market.quote(str(params.get("symbol")), exchange).to_dict())
        except market.MarketError as e:
            rows.append({"exchange": exchange, "error": str(e)})
    return {"symbol": params.get("symbol"), "quotes": rows}


def handle(action: str, params: dict, connections: list[dict]) -> dict:
    if action == "list_markets":
        return {"markets": [m.to_dict() for m in market.active_prediction_markets()]}
    if action == "get_quotes":
        return _quotes(params)
    if action == "get_news":
        symbol = str(params.get("symbol"))
        return {"news": {"symbol": symbol, "items": news.headlines(symbol), "error": "", "at": time.time()}}

    store = Store()
    if action == "create_competition":
        return arena.create(store, params, connections)
    if action == "list_competitions":
        return {"competitions": arena.listing(store, int(params.get("limit") or 20))}
    competition_id = str(params.get("competition_id") or "")
    if action == "run_cycle":
        return arena.run_cycle(store, competition_id, connections)
    if action == "get_state":
        return arena.state(store, competition_id)
    raise arena.Refused(f"Unknown action: {action}")


def main() -> int:
    try:
        request = json.loads(sys.argv[1])
        with contextlib.redirect_stdout(sys.stderr):  # a library's chatter is not the answer
            answer = {"success": True, **handle(request.get("action") or "", request.get("params") or {},
                                                request.get("connections") or [])}
    except (arena.Refused, market.MarketError, llm.NoAnswer, Busy) as e:
        answer = {"success": False, "error": str(e)}
    except Exception as e:
        import traceback
        traceback.print_exc()
        answer = {"success": False, "error": f"{type(e).__name__}: {e}"}
    print(json.dumps(answer))
    return 0


if __name__ == "__main__":
    sys.exit(main())
