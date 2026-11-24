"""A competition: made, run a cycle at a time, and where it stands."""

from __future__ import annotations

import sys
import threading
import time
import uuid

from . import llm, market, news
from .book import MODES, Book, Fill, Limits, Rejected, order
from .features import technical_lines
from .store import Store
from .traders import (GRID_PROVIDER, Decision, Grid, NoDecision, crypto_prompt, crypto_system, prediction_prompt,
                      prediction_system, read_crypto, read_predictions)


class Refused(Exception):
    """A request that cannot be carried out; the message is for the reader."""


_DECISIONS_SHOWN = 100
_RECENT = 5
_REQUOTE_AFTER_S = 3


def is_grid(model: dict) -> bool:
    return model.get("provider") == GRID_PROVIDER


def is_prediction(config: dict) -> bool:
    return config.get("competition_type") == "polymarket"


def symbol_of(config: dict) -> str:
    return (config.get("symbols") or ["BTC/USD"])[0]


# ── Made ─────────────────────────────────────────────────────────────────────

def create(store: Store, params: dict, connections: list[dict]) -> dict:
    models = [{k: str(m.get(k) or "") for k in ("name", "key", "provider", "model_id")} for m in params.get("models") or []]
    if len(models) < 2:
        raise Refused("A competition needs two competitors or more")
    if len({m["name"] for m in models}) < len(models):
        raise Refused("Two competitors have the same name")
    mode = str(params.get("mode") or "baseline")
    if mode not in MODES:
        raise Refused(f"There is no mode {mode}")
    capital = float(params.get("initial_capital") or 0)
    if not 0 < capital < 1e12:
        raise Refused("The initial capital must be above zero")
    for m in models:
        if not is_grid(m):
            _check_connection(m, connections)

    config = {
        "competition_name": str(params.get("competition_name") or "Competition"), "models": models, "mode": mode,
        "initial_capital": capital, "cycle_interval_seconds": int(params.get("cycle_interval_seconds") or 150),
        "competition_type": "polymarket" if params.get("competition_type") == "polymarket" else "crypto",
    }
    if is_prediction(config):
        if any(is_grid(m) for m in models):
            raise Refused("The grid bot trades a crypto price, not prediction markets")
        ids = [str(i) for i in params.get("market_ids") or []]
        if not 1 <= len(ids) <= 10:
            raise Refused("Choose one to ten prediction markets")
        found = [market.prediction_market(i) for i in ids]
        shut = [m.question for m in found if not m.open]
        if shut:
            raise Refused(f"Not open for trading: {'; '.join(shut)}")
        config["markets"] = [{"id": m.id, "question": m.question, "outcomes": m.outcomes} for m in found]
        config["symbols"] = []
    else:
        symbol, exchange = str(params.get("symbol") or "BTC/USD"), str(params.get("exchange_id") or "kraken")
        quote = market.quote(symbol, exchange)  # it trades there, or there is nothing to create
        config |= {"symbols": [symbol], "exchange_id": exchange}
        if any(is_grid(m) for m in models):
            range_pct = min(50.0, max(0.5, float(params.get("grid_range_pct") or 5)))
            levels = min(100, max(2, int(params.get("grid_levels") or 10)))
            config["grid"] = vars(Grid.around(quote.price, range_pct, levels, capital))
    competition_id = "comp_" + uuid.uuid4().hex[:16]
    store.create(competition_id, config)
    return state(store, competition_id)


def _check_connection(model: dict, connections: list[dict]):
    try:
        c = llm.connection_for(model, connections)
    except llm.NoAnswer as e:
        raise Refused(str(e)) from e
    if not c.get("api_key") and not llm.is_local(c):
        raise Refused(f"{model['name']} has no API key: set one in Settings > LLM Config")


# ── Run ──────────────────────────────────────────────────────────────────────

def run_cycle(store: Store, competition_id: str, connections: list[dict]) -> dict:
    with store.cycle_lock(competition_id):
        competition = store.competition(competition_id)
        if competition is None:
            raise Refused(f"There is no competition {competition_id}")
        config, number = competition["config"], (competition["cycle_count"] or 0) + 1
        books = {m["name"]: store.book(competition_id, config, m["name"]) for m in config["models"]}
        run = _prediction_cycle if is_prediction(config) else _crypto_cycle
        data, decisions, fills = run(store, competition_id, config, number, books, connections)
        snapshots = standings(config, books, data["prices"])
        store.save_cycle(competition_id, number, data, decisions, fills, snapshots)
        return state(store, competition_id)


def _crypto_cycle(store, competition_id, config, number, books, connections):
    symbol, exchange = symbol_of(config), config.get("exchange_id") or "kraken"
    quote = market.quote(symbol, exchange)  # without a price there is no cycle
    notes: list[str] = []
    try:
        closes = market.hourly_closes(symbol, exchange)
    except market.MarketError as e:
        closes, _ = [], notes.append(f"No indicators: {e}")
    items, news_error = [], ""
    try:
        items = news.headlines(symbol)
    except Exception as e:  # news is a help, not a need
        news_error = " ".join(str(e).split())[:200]
        notes.append(f"No headlines: {news_error}")

    limits, started = MODES[config["mode"]], time.time()
    system = crypto_system(config["mode"], limits, config.get("custom_prompt") or "")
    technical = technical_lines(closes, quote.price)
    asks = {}
    for m in config["models"]:
        if not is_grid(m):
            recent = [d for d in store.decisions(competition_id, _RECENT, m["name"])]
            asks[m["name"]] = (m, crypto_prompt(quote, technical, items, books[m["name"]], recent, number))
    read = {name: (lambda text, n=name: [read_crypto(n, symbol, text)]) for name in asks}
    decided = _ask_all(asks, system, read, connections, symbol)
    for m in config["models"]:
        if is_grid(m):
            decided[m["name"]] = [Grid(**config["grid"]).decide(m["name"], quote, books[m["name"]])]

    # The models may have taken minutes: what they decided is filled at the price now.
    bid, ask = quote.bid, quote.ask
    if time.time() - started > _REQUOTE_AFTER_S:
        try:
            quote = market.quote(symbol, exchange)
            bid, ask = quote.bid, quote.ask
        except market.MarketError as e:  # what was decided is kept; none of it is filled
            bid = ask = 0.0
            notes.append(f"No trades: {e}")
    decisions, fills = [], []
    for m in config["models"]:
        for d in decided[m["name"]]:
            _carry_out(d, books[m["name"]], Limits(1, 1) if is_grid(m) else limits, bid, ask, {symbol: quote.price},
                       fills, allow_short=not is_grid(m))
            decisions.append(d)
    data = {"prices": {symbol: quote.price}, "quote": quote.to_dict(), "notes": notes,
            "news": {"symbol": symbol, "items": items, "error": news_error, "at": time.time()}}
    return data, decisions, fills


def _prediction_cycle(store, competition_id, config, number, books, connections):
    prices = dict(store.last_cycle(competition_id).get("prices") or {})
    notes: list[str] = []

    def fetch() -> list[market.PredictionMarket]:
        got = []
        for m in config.get("markets") or []:
            try:
                got.append(market.prediction_market(m["id"]))
            except market.MarketError as e:
                note = f"{m['question']}: {e}"
                if note not in notes:
                    notes.append(note)
        prices.update({m.symbol(i): m.prices[i] for m in got for i in (0, 1)})
        return got

    markets = fetch()
    if not markets:
        raise Refused("; ".join(notes) or "The competition has no markets")
    fills: list = []
    for m in markets:
        if m.settled:
            for name, book in books.items():
                for i in (0, 1):
                    if held := book.held(m.symbol(i)):
                        fill = Fill(m.symbol(i), "settle", held, m.prices[i])
                        book.apply(fill)
                        fills.append((name, fill))
    tradable = [m for m in markets if m.open]
    decisions: list[Decision] = []
    if tradable:
        limits, started = MODES[config["mode"]], time.time()
        system = prediction_system(config["mode"], limits, config.get("custom_prompt") or "")
        asks = {m["name"]: (m, prediction_prompt(tradable, books[m["name"]], prices, number)) for m in config["models"]}
        read = {name: (lambda text, n=name: read_predictions(n, tradable, text)) for name in asks}
        decided = _ask_all(asks, system, read, connections, "")
        if time.time() - started > _REQUOTE_AFTER_S:
            tradable = [m for m in fetch() if m.open]
        now = {m.id: m for m in tradable}
        for m in config["models"]:
            for d in decided[m["name"]]:
                current = now.get(d.market.id) if d.market else None
                if d.action not in ("none", "hold") and current is None:
                    d.status, d.error = "rejected", "the market closed, or could not be read again"
                else:
                    bid, ask = current.bid_ask(d.outcome) if current else (0.0, 0.0)
                    cap = 0.1 * current.liquidity if current and current.liquidity else None
                    _carry_out(d, books[m["name"]], limits, bid, ask, prices, fills, allow_short=False, max_value=cap)
                decisions.append(d)
    else:
        notes.append("Every market is closed" + ("" if all(m.settled for m in markets) else "; not all are decided yet"))
    data = {"prices": prices, "notes": notes, "over": len(markets) == len(config["markets"]) and all(m.settled for m in markets)}
    return data, decisions, fills


def _carry_out(d: Decision, book: Book, limits: Limits, bid: float, ask: float, prices: dict, fills: list, **kw):
    if d.action == "none":
        d.status = "none"
        return
    if d.action == "hold" or d.size <= 0:
        d.status = "hold" if d.action == "hold" else "rejected"
        d.error = "" if d.action == "hold" else "its size is 0"
        return
    try:
        if limits.min_confidence and (d.confidence is None or d.confidence < limits.min_confidence):
            raise Rejected(f"its confidence is below {limits.min_confidence}")
        fill = order(book, limits, d.symbol, d.action, d.size, bid, ask, prices, **kw)
    except Rejected as e:
        d.status, d.error = "rejected", str(e)
        return
    d.status, d.quantity, d.price, d.pnl = "executed", fill.quantity, fill.price, book.apply(fill)
    fills.append((d.model_name, fill))


def _ask_all(asks: dict, system: str, read: dict, connections: list[dict], symbol: str) -> dict[str, list[Decision]]:
    """Every model at once, but those on one local engine one after another: it
    has one model in memory at a time."""
    decided: dict[str, list[Decision]] = {}
    queues: dict[str, list] = {}
    for name, (model, prompt) in asks.items():
        try:
            c = llm.connection_for(model, connections)
        except llm.NoAnswer as e:
            decided[name] = [Decision(name, symbol, "none", error=str(e))]
            continue
        queues.setdefault(str(c.get("base_url")) if llm.is_local(c) else name, []).append((name, c, prompt))

    def work(queue):
        for name, c, prompt in queue:
            try:
                decided[name] = _decide(c, system, prompt, read[name])
            except (llm.NoAnswer, NoDecision) as e:
                decided[name] = [Decision(name, symbol, "none", error=str(e))]
            except Exception as e:  # ours, and still not a HOLD
                print(f"alpha_arena: {name}: {type(e).__name__}: {e}", file=sys.stderr)
                decided[name] = [Decision(name, symbol, "none", error=f"{type(e).__name__}: {e}"[:200])]

    threads = [threading.Thread(target=work, args=(q,)) for q in queues.values()]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    return decided


def _decide(connection: dict, system: str, prompt: str, read) -> list[Decision]:
    asked = prompt
    for last in (False, True):
        text = llm.ask(connection, system, asked)
        try:
            found = read(text)
            break
        except NoDecision as e:
            if last:
                raise
            asked = f"{prompt}\n\nYour last answer could not be used: {e}. Answer with the JSON and nothing else."
    grounding = llm.grounding()
    for d in found:  # a figure it was not given is not shown as one
        if d.reasoning:
            d.reasoning = grounding.ground(d.reasoning, system, prompt)
    return found


# ── Where it stands ──────────────────────────────────────────────────────────

def standings(config: dict, books: dict[str, Book], prices: dict[str, float]) -> list[dict]:
    rows = []
    for m in config["models"]:
        book = books[m["name"]]
        value = book.value(prices)
        rows.append({"model_name": m["name"], "portfolio_value": value, "total_pnl": value - book.initial_capital,
                     "return_pct": (value / book.initial_capital - 1) * 100, "trades_count": book.trades, "cash": book.cash,
                     "positions_count": len(book.positions), "win_rate": book.win_rate()})
    rows.sort(key=lambda r: -r["portfolio_value"])
    for i, r in enumerate(rows):  # the same value is the same rank
        r["rank"] = rows[i - 1]["rank"] if i and rows[i - 1]["portfolio_value"] == r["portfolio_value"] else i + 1
    return rows


def metrics(book: Book, values: list[float], failures: int) -> dict:
    """None: not enough to say. No annual figure is made of cycles minutes apart."""
    path = [book.initial_capital, *values]
    returns = [(b / a - 1) * 100 for a, b in zip(path, path[1:]) if a > 0]
    peak, drawdown = path[0], 0.0
    for v in path:
        peak = max(peak, v)
        drawdown = max(drawdown, (peak - v) / peak * 100 if peak > 0 else 0.0)
    mean = sum(returns) / len(returns) if returns else 0.0
    wins, losses = [p for p in book.closed if p > 0], [p for p in book.closed if p < 0]
    return {
        "cycles": len(values),
        "return_pct": (path[-1] / path[0] - 1) * 100 if values else None,
        "max_drawdown_pct": drawdown if values else None,
        "volatility_pct": (sum((r - mean) ** 2 for r in returns) / (len(returns) - 1)) ** 0.5 if len(returns) > 1 else None,
        "best_cycle_pct": max(returns) if returns else None,
        "worst_cycle_pct": min(returns) if returns else None,
        "trades": book.trades, "closed": len(book.closed), "win_rate": book.win_rate(),
        "profit_factor": sum(wins) / -sum(losses) if losses else None,
        "average_win": sum(wins) / len(wins) if wins else None,
        "average_loss": sum(losses) / len(losses) if losses else None,
        "fees": book.fees, "no_decision": failures,
    }


def state(store: Store, competition_id: str) -> dict:
    competition = store.competition(competition_id)
    if competition is None:
        raise Refused(f"There is no competition {competition_id}")
    config, last = competition["config"], store.last_cycle(competition_id)
    prices = last.get("prices") or {}
    books = {m["name"]: store.book(competition_id, config, m["name"]) for m in config["models"]}
    failures = store.failures(competition_id)
    labels = {f"pm:{m['id']}:{i}": f"{m['outcomes'][i]} — {m['question']}" for m in config.get("markets") or [] for i in (0, 1)}
    decisions = store.decisions(competition_id, _DECISIONS_SHOWN)
    for d in decisions:
        d["label"] = labels.get(d["symbol"], d["symbol"])
    symbol = None if is_prediction(config) else symbol_of(config)
    grid = None
    for m in config["models"]:
        if is_grid(m) and config.get("grid"):
            g, price = Grid(**config["grid"]), prices.get(symbol)
            held = books[m["name"]].held(symbol)
            grid = {"model_name": m["name"], "lower": g.lower, "upper": g.upper, "levels": g.levels, "unit": g.unit,
                    "price": price, "held": held, "rows": g.rows(price or 0, held)}
    return {
        "competition_id": competition_id, "name": config.get("competition_name"), "status": competition["status"],
        "type": "polymarket" if is_prediction(config) else "crypto", "symbol": symbol, "mode": config["mode"],
        "exchange": config.get("exchange_id"), "interval": config.get("cycle_interval_seconds"),
        "models_count": len(config["models"]), "cycle_number": competition["cycle_count"] or 0,
        "price": prices.get(symbol) if symbol else None, "leaderboard": standings(config, books, prices),
        "decisions": decisions, "grid": grid, "news": last.get("news"), "notes": last.get("notes") or [],
        "over": bool(last.get("over")),
        "metrics": [{"model_name": m["name"],
                     **metrics(books[m["name"]], store.values(competition_id, m["name"]), failures.get(m["name"], 0))}
                    for m in config["models"]],
    }


def listing(store: Store, limit: int) -> list[dict]:
    return [{"competition_id": c["competition_id"], "name": c["config"].get("competition_name"), "status": c["status"],
             "cycle_count": c["cycle_count"] or 0, "created_at": c["created_at"],
             "models": [m.get("name") for m in c["config"].get("models", [])]} for c in store.competitions(limit)]
