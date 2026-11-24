#!/usr/bin/env python3
"""Alpha Arena's engine (scripts/alpha_arena).

Pinned here, each of them once wrong: a purchase takes its cost out of the
cash (the leaderboard ranked who had bought most, and after a first purchase
every later decision failed); an answer that holds no decision is not a HOLD;
a model is asked at its own endpoint with its own key; the database gives up
saved keys and the old numbers; a competition of prediction markets is one.
"""

import http.server
import json
import math
import os
import sqlite3
import sys
import tempfile
import threading
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts"))

from alpha_arena import arena, features, llm, market, news, traders  # noqa: E402
from alpha_arena.book import FEE_RATE, MODES, Book, Fill, Limits, Rejected, order  # noqa: E402
from alpha_arena.store import Busy, Store  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    print(("PASS : " if cond else "FAIL : ") + name + ("" if cond else f" {detail!r}"))
    if not cond:
        FAILURES.append(name)


def near(a, b, eps=1e-9):
    return a is not None and b is not None and abs(a - b) <= eps * max(1.0, abs(a), abs(b))


def raises(kind, f):
    try:
        f()
    except kind as e:
        return str(e) or True
    return None


# ── The book ─────────────────────────────────────────────────────────────────
b = Book(10000.0, 10000.0)
for _ in range(3):
    b.apply(Fill("BTC/USD", "buy", 0.02, 100000, 2.0))
check("a purchase takes its cost out of the cash", near(b.cash, 10000 - 3 * 2002), b.cash)
check("...and the value changes by its fees only", near(b.value({"BTC/USD": 100000}), 10000 - 6))
check("a position's gain is value", near(b.value({"BTC/USD": 110000}), 10000 - 6 + 600))
r = b.apply(Fill("BTC/USD", "sell", 0.06, 110000, 6.6))
check("a sale realizes it", near(r, 600 - 6.6) and near(b.cash, 10000 - 6 + 600 - 6.6) and not b.positions, (r, b))
b = Book(1000.0, 1000.0)
b.apply(Fill("X", "sell", 2, 100))
check("a short is a liability", near(b.cash, 1200) and near(b.value({"X": 100}), 1000) and near(b.value({"X": 90}), 1020))
check("buying more than it was covers and turns", near(b.apply(Fill("X", "buy", 3, 90)), 20) and near(b.held("X"), 1)
      and near(b.positions["X"].entry_price, 90))
check("without a price, what it was entered at", near(b.value({}), b.cash + 90))
b = Book.of(100.0, [Fill("pm:1:0", "buy", 100, 0.4), Fill("pm:1:0", "settle", 100, 1.0)])
check("a settlement pays and is no trade", near(b.cash, 160) and b.trades == 1 and b.win_rate() == 1.0)
check("no closed trade, no win rate", Book(1.0, 1.0).win_rate() is None)


def place(book, action, size, mode="max_leverage", **kw):
    f = order(book, MODES[mode], "X", action, size, kw.pop("bid", 99.0), kw.pop("ask", 101.0), {"X": 100.0}, **kw)
    book.apply(f)
    return f


b = Book(10000.0, 10000.0)
f = place(b, "buy", 1.0, "baseline")
check("a trade is cut to the mode's limit", near(f.quantity * f.price, 2500) and f.price == 101.0, f)
place(b, "buy", 1.0, "baseline")
check("the position too", near(b.held("X") * 101, 0.5 * b.value({"X": 100.0}), 0.02), b.held("X"))
check("...and then no more", raises(Rejected, lambda: place(b, "buy", 1.0, "baseline")))
place(b, "sell", 0.25)
check("a sale is at the bid, of what is held", b.held("X") > 0)
for _ in range(9):
    try:
        place(b, "sell", 1.0)
    except Rejected:
        break
check("it never goes short by selling", b.held("X") == 0.0)
check("nothing to sell is said", "nothing to sell" in (raises(Rejected, lambda: place(b, "sell", 1.0)) or ""))
for _ in range(9):
    try:
        place(b, "short", 1.0)
    except Rejected:
        break
check("a short stays within the limit", -0.97 * 10000 < b.held("X") * 100 < -0.85 * 10000, b.held("X"))
b = Book(100.0, 100.0)
for _ in range(20):
    try:
        place(b, "buy", 1.0)
    except Rejected:
        break
check("the cash never goes below zero", b.cash >= 0, b.cash)
check("no price, no trade", raises(Rejected, lambda: place(Book(100.0, 100.0), "buy", 1.0, ask=0.0)))
check("a cap on the trade's value", near(order(Book(100.0, 100.0), Limits(1, 1), "X", "buy", 1.0, 1, 1, {}, max_value=7).quantity, 7))

# ── Answers ──────────────────────────────────────────────────────────────────
d = traders.read_crypto("m", "BTC/USD", 'Thinking {a}...\n```json\n{"action":"BUY","size_pct":"15%","confidence":0.6,"reasoning":"r"}\n```')
check("a decision in prose and a fence", (d.action, d.size, d.confidence) == ("buy", 0.15, 0.6), d)
check("the last one counts", traders.read_crypto("m", "s", '{"action":"buy","size_pct":5} no: {"action":"hold"}').action == "hold")
for name, text in {"an empty answer": "", "prose": "I would buy.", "no JSON after all": '{"action": "buy", "size_pct": ',
                   "an unknown action": '{"action":"yolo"}', "no size": '{"action":"buy"}',
                   "a size that is none": '{"action":"buy","size_pct":"lots"}', "a size out of range": '{"action":"buy","size_pct":900}',
                   "NaN": '{"action":"buy","size_pct":NaN}', "a confidence out of range": '{"action":"hold","confidence":85}'}.items():
    check(f"no decision: {name}", raises(traders.NoDecision, lambda t=text: traders.read_crypto("m", "s", t)), text)
pm = [market.PredictionMarket("7", "Rain?", ["Yes", "No"], [0.4, 0.6], 0.39, 0.41, 5000.0, 1.0, "", True, False)]
ds = traders.read_predictions("m", pm, '[{"market_id":7,"outcome":"no","action":"buy","size_pct":10}]')
check("a prediction market's decision", (ds[0].symbol, ds[0].action, ds[0].size) == ("pm:7:1", "buy", 0.1), ds)
check("an empty list is a decision: none of them", traders.read_predictions("m", pm, "[]") == [])
check("a market not in it", traders.read_predictions("m", pm, '[{"market_id":8,"action":"buy","outcome":"Yes","size_pct":1}]')[0].action == "none")
check("an outcome it does not have", traders.read_predictions("m", pm, '[{"market_id":7,"action":"buy","outcome":"Maybe","size_pct":1}]')[0].action == "none")
check("prose is none", raises(traders.NoDecision, lambda: traders.read_predictions("m", pm, "nothing today")))
check("the second outcome's prices mirror the first's", [round(x, 9) for x in pm[0].bid_ask(1)] == [0.59, 0.61] and pm[0].bid_ask(0) == (0.39, 0.41))

# ── The grid bot ─────────────────────────────────────────────────────────────
g = traders.Grid.around(100.0, 5, 10, 10000.0)
check("the grid's lines", near(g.lower, 95) and near(g.upper, 105) and near(g.step, 1))
check("it starts with half", g.target(100.0, 0) == 5)
check("it does not trade back and forth at one line", g.target(99.99, 5) == 5 and g.target(100.01, 5) == 5)
check("a line down buys, a line up sells", g.target(98.99, 5) == 6 and g.target(101.01, 5) == 4)
check("outside, all or none", g.target(50, 3) == 10 and g.target(200, 3) == 0)
check("all units cost less than the capital", sum(g.unit * (105 - i) for i in range(1, 11)) < 10000)
book, lows = Book(10000.0, 10000.0), []
for price in [100, 99, 98, 97, 98, 99, 100, 101, 102, 101, 100, 99, 98, 99, 100]:
    q = market.Quote("X", "t", price, price, price)
    dec = g.decide("grid", q, book)
    fills = []
    arena._carry_out(dec, book, Limits(1, 1), price, price, {"X": price}, fills, allow_short=False)
check("a price that comes back leaves it a profit", book.value({"X": 100.0}) > 10000, book.value({"X": 100.0}))

# ── Indicators, headlines ────────────────────────────────────────────────────
xs = [100 + 5 * math.sin(i / 3) + 0.2 * i for i in range(200)]
check("SMA", near(features.sma(xs, 20), sum(xs[-20:]) / 20))
check("RSI of a rise is 100, of a fall 0, flat 50", (features.rsi(list(range(1, 40))), features.rsi(list(range(40, 1, -1))), features.rsi([5.0] * 30)) == (100.0, 0.0, 50.0))
check("too few closes say nothing", features.rsi(xs[:14]) is None and features.macd(xs[:33]) is None and features.sma([], 20) is None)
check("a line for each indicator", len(features.technical_lines(xs, xs[-1])) == 7, features.technical_lines(xs, xs[-1]))
check("a small price keeps its digits", features.price_text(3.434e-06) == "$0.000003434", features.price_text(3.434e-06))
check("tone of words in their forms", [news.tone(t) for t in ("Bitcoin rallies as inflows jump", "Ether plunges, traders fear losses", "Bitcoin holds steady", "Coin dropped")] == ["positive", "negative", "neutral", "negative"])

# ── A model's connection ─────────────────────────────────────────────────────
conns = [{"key": "p1", "name": "A (openai)", "provider": "openai", "model_id": "x", "api_key": "sk-one", "base_url": "https://one/v1"},
         {"key": "p2", "name": "B (openai)", "provider": "openai", "model_id": "y", "api_key": "sk-two", "base_url": "https://two/v1"}]
c = llm.connection_for({"name": "B (openai)", "key": "p2", "provider": "openai", "model_id": "y2"}, conns)
check("a model is asked with its own key at its own endpoint", (c["api_key"], c["base_url"], c["model_id"]) == ("sk-two", "https://two/v1", "y2"), c)
check("an older competition's model is found by name", llm.connection_for({"name": "A (openai)", "provider": "openai", "model_id": "x"}, conns)["key"] == "p1")
check("a profile that is gone is said, not replaced by another's key", raises(llm.NoAnswer, lambda: llm.connection_for({"name": "A (openai)", "key": "gone", "provider": "openai", "model_id": "x"}, conns)))
check("nor one whose provider was changed", raises(llm.NoAnswer, lambda: llm.connection_for({"name": "A", "key": "p1", "provider": "anthropic", "model_id": "x"}, conns)))
check("an error does not show the key", "sk-secret-1" not in llm._why(RuntimeError("bad sk-secret-1"), {"api_key": "sk-secret-1"}))

try:
    import langchain_openai  # noqa: F401
    HAVE_CLIENT = True
except ImportError:
    HAVE_CLIENT = False
    print("SKIP : langchain_openai is not installed: the endpoint is not asked")
if HAVE_CLIENT:
    SEEN = []

    class Engine(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            SEEN.append((self.path, self.headers.get("Authorization"), body))
            status, text, finish = 200, '{"action":"hold"}', "stop"
            if body["model"] == "thinker" and "reasoning_effort" not in body:
                text, finish = "", "length"
            if body["model"] == "denied":
                status = 401
            out = json.dumps({"error": {"message": "no"}} if status != 200 else {
                "id": "1", "object": "chat.completion", "created": 0, "model": body["model"],
                "choices": [{"index": 0, "finish_reason": finish, "message": {"role": "assistant", "content": text}}]}).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(out)))
            self.end_headers()
            self.wfile.write(out)

        def log_message(self, *_):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Engine)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    conn = {"provider": "openai", "name": "n", "api_key": "sk-mine", "model_id": "m",
            "base_url": f"http://127.0.0.1:{server.server_port}/proxy/v1", "max_tokens": 500}
    check("the profile's base URL is used", llm.ask(conn, "s", "p") == '{"action":"hold"}' and SEEN[-1][:2] == ("/proxy/v1/chat/completions", "Bearer sk-mine"), SEEN)
    check("a model that only thought is asked not to", llm.ask({**conn, "model_id": "thinker"}, "s", "p") and SEEN[-1][2].get("reasoning_effort") == "none")
    check("a refused key is said", "refused the API key" in (raises(llm.NoAnswer, lambda: llm.ask({**conn, "model_id": "denied"}, "s", "p")) or ""))
    check("an engine that is not there is said", "could not be reached" in (raises(llm.NoAnswer, lambda: llm.ask({**conn, "base_url": "http://127.0.0.1:9/v1"}, "s", "p")) or ""))
    server.shutdown()

# ── A competition ────────────────────────────────────────────────────────────
PRICE = {"now": 100.0}
market.quote = lambda symbol, exchange: market.Quote(symbol, exchange, PRICE["now"], PRICE["now"] - 0.1, PRICE["now"] + 0.1)
market.hourly_closes = lambda symbol, exchange: xs
news.headlines = lambda symbol, limit=12: [{"title": "Bitcoin rallies 4%", "publisher": "P", "published": "", "url": "", "tone": "positive"}]
ANSWERS, ASKED = {}, []


def fake_ask(connection, system, prompt):
    ASKED.append((connection["name"], prompt))
    a = ANSWERS[connection["name"]]
    a = a.pop(0) if isinstance(a, list) else a
    if isinstance(a, Exception):
        raise a
    return a


llm.ask = fake_ask
conns = [{"key": k, "name": k, "provider": "openai", "model_id": "m", "api_key": "sk", "base_url": "https://x/v1"} for k in ("a", "b")]
models = [{"key": k, "name": k, "provider": "openai", "model_id": "m"} for k in ("a", "b")]
grid = {"key": "grid", "name": "Grid bot", "provider": "rules", "model_id": "grid"}
params = {"competition_name": "T", "models": models + [grid], "initial_capital": 10000, "symbol": "BTC/USD", "mode": "baseline"}
with tempfile.TemporaryDirectory() as folder:
    store = Store(Path(folder))
    s = arena.create(store, params, conns)
    cid = s["competition_id"]
    check("a new competition is level", [r["rank"] for r in s["leaderboard"]] == [1, 1, 1] and s["cycle_number"] == 0, s)
    check("no key is saved", "sk" not in json.dumps(store.competition(cid)["config"]))
    check("one competitor is none", raises(arena.Refused, lambda: arena.create(store, {**params, "models": models[:1]}, conns)))
    check("a model without a key is said", "API key" in raises(arena.Refused, lambda: arena.create(store, params, [{**c, "api_key": ""} for c in conns])))
    check("...but a local engine needs none", arena.create(store, params, [{**c, "api_key": "", "base_url": "http://localhost:1/v1"} for c in conns]))

    ANSWERS.update(a='{"action":"buy","size_pct":20,"confidence":0.7,"reasoning":"Up 4% on the news, to 123456 soon."}', b=llm.NoAnswer("no answer after 300 s"))
    s = arena.run_cycle(store, cid, conns)
    by = {d["model_name"]: d for d in s["decisions"]}
    check("a decision is carried out", by["a"]["status"] == "executed" and near(by["a"]["quantity"] * 100.1, 2000), by["a"])
    check("a figure it was not given is removed", "123456" not in by["a"]["reasoning"] and "4%" in by["a"]["reasoning"], by["a"]["reasoning"])
    check("no answer is no decision, with the reason", (by["b"]["action"], by["b"]["error"]) == ("none", "no answer after 300 s"), by["b"])
    check("the grid bot takes part", by["Grid bot"]["status"] == "executed" and s["grid"]["held"] > 0, by["Grid bot"])
    board = {r["model_name"]: r for r in s["leaderboard"]}
    check("buying is not a profit", board["a"]["total_pnl"] < 0 and near(board["b"]["portfolio_value"], 10000) and s["leaderboard"][0]["model_name"] == "b", s["leaderboard"])
    check("the model saw the indicators and headlines", "RSI(14h)" in ASKED[0][1] and "Bitcoin rallies" in ASKED[0][1])

    PRICE["now"] = 110.0
    ANSWERS.update(a='{"action":"sell","size_pct":100,"confidence":0.9}', b=["I buy!", '{"action":"short","size_pct":10}'])
    s = arena.run_cycle(store, cid, conns)
    by = {d["model_name"]: d for d in s["decisions"] if d["cycle_number"] == 2}
    check("a second cycle works with a position open", by["a"]["status"] == "executed" and by["a"]["pnl"] > 0, by["a"])
    check("the prompt says what it holds", any("Position: long" in p for n, p in ASKED if n == "a"))
    check("an answer that can't be read is asked for again, once", by["b"]["action"] == "short" and by["b"]["status"] == "executed", by["b"])
    m = {x["model_name"]: x for x in s["metrics"]}
    check("metrics", m["a"]["closed"] == 1 and m["a"]["win_rate"] == 1.0 and m["b"]["no_decision"] == 1 and m["a"]["cycles"] == 2, m)
    check("no losses, no profit factor", m["a"]["profit_factor"] is None)

    ANSWERS.update(a='{"action":"hold"}', b="still no JSON")
    s = arena.run_cycle(store, cid, conns)
    by = {d["model_name"]: d for d in s["decisions"] if d["cycle_number"] == 3}
    check("a HOLD is a decision, an unreadable answer is not", by["a"]["status"] == "hold" and by["b"]["action"] == "none", by)
    ANSWERS.update(a='{"action":"hold"}', b='{"action":"hold"}')
    s = arena.run_cycle(store, cid, [c for c in conns if c["key"] == "a"])
    check("a model gone from Settings is said", any(d["model_name"] == "b" and "Settings" in d["error"] for d in s["decisions"]))

    real, arena._REQUOTE_AFTER_S = arena._REQUOTE_AFTER_S, -1
    calls = {"n": 0}

    def flaky(symbol, exchange):
        calls["n"] += 1
        if calls["n"] > 1:
            raise market.MarketError("Kraken answered 503")
        return market.Quote(symbol, exchange, 110.0, 109.9, 110.1)

    good, market.quote = market.quote, flaky
    ANSWERS.update(a='{"action":"buy","size_pct":20}')
    s = arena.run_cycle(store, cid, conns)
    check("no price to fill at: decided, not filled", any(d["cycle_number"] == 5 and d["model_name"] == "a" and d["status"] == "rejected" for d in s["decisions"]) and "503" in " ".join(s["notes"]), s["notes"])
    market.quote, arena._REQUOTE_AFTER_S = good, real
    before = store.competition(cid)["cycle_count"]
    market.quote = lambda *_: (_ for _ in ()).throw(market.MarketError("Kraken has no X"))
    check("no market, no cycle", raises(market.MarketError, lambda: arena.run_cycle(store, cid, conns)) and store.competition(cid)["cycle_count"] == before)
    market.quote = good
    with store.cycle_lock(cid):
        check("one cycle of a competition at a time", raises(Busy, lambda: arena.run_cycle(Store(Path(folder)), cid, conns)))
    check("...and free again after", arena.run_cycle(store, cid, conns)["cycle_number"] == before + 1)

    monk = arena.create(store, {**params, "models": models, "mode": "monk"}, conns)["competition_id"]
    ANSWERS.update(a='{"action":"buy","size_pct":10,"confidence":0.6}', b='{"action":"buy","size_pct":50,"confidence":0.8}')
    by = {d["model_name"]: d for d in arena.run_cycle(store, monk, conns)["decisions"]}
    check("monk mode is kept to", by["a"]["status"] == "rejected" and near(by["b"]["quantity"] * 110.1, 1000), by)

    # Prediction markets
    M = {"7": market.PredictionMarket("7", "Rain?", ["Yes", "No"], [0.4, 0.6], 0.39, 0.41, 5000.0, 1.0, "", True, False)}
    market.prediction_market = lambda i: M[i]
    pp = {"competition_type": "polymarket", "market_ids": ["7"], "models": models, "initial_capital": 1000, "mode": "max_leverage"}
    check("the grid bot has no place there", raises(arena.Refused, lambda: arena.create(store, {**pp, "models": models + [grid]}, conns)))
    check("a competition of no markets is none", raises(arena.Refused, lambda: arena.create(store, {**pp, "market_ids": []}, conns)))
    pid = arena.create(store, pp, conns)["competition_id"]
    ANSWERS.update(a='[{"market_id":"7","outcome":"Yes","action":"buy","size_pct":41}]', b='[{"market_id":"7","outcome":"No","action":"buy","size_pct":10}]')
    s = arena.run_cycle(store, pid, conns)
    by = {d["model_name"]: d for d in s["decisions"]}
    check("shares are bought at the ask", near(by["a"]["price"], 0.41) and near(by["a"]["quantity"], 1000) and near(by["b"]["price"], 0.61), by)
    check("...and are named", by["a"]["label"] == "Yes — Rain?")
    M["7"] = market.PredictionMarket("7", "Rain?", ["Yes", "No"], [0.995, 0.005], 0.99, 1.0, 5000.0, 1.0, "", True, False)
    ANSWERS.update(a="[]", b="[]")
    s = arena.run_cycle(store, pid, conns)
    check("near 1 is not decided", not s["over"] and store.book(pid, store.competition(pid)["config"], "a").held("pm:7:0") > 0)
    M["7"] = market.PredictionMarket("7", "Rain?", ["Yes", "No"], [1.0, 0.0], None, None, 0.0, 1.0, "", False, True)
    s = arena.run_cycle(store, pid, conns)
    board = {r["model_name"]: r for r in s["leaderboard"]}
    check("a decided market pays out", s["over"] and near(board["a"]["portfolio_value"], 1000 - 410 * (1 + FEE_RATE) + 1000) and board["b"]["total_pnl"] < -99, board)

# ── Databases of before ──────────────────────────────────────────────────────
KEY = "sk-must-not-be-saved"
with tempfile.TemporaryDirectory() as folder:
    db = sqlite3.connect(os.path.join(folder, "alpha_arena.db"))
    from alpha_arena import store as store_module  # noqa: E402
    db.executescript(store_module._SCHEMA)
    db.execute("CREATE TABLE leaderboard_history (id INTEGER PRIMARY KEY, leaderboard TEXT)")
    config = {"competition_name": "old", "mode": "baseline", "symbols": ["BTC/USD"], "initial_capital": 10000.0,
              "models": [{"name": n, "provider": "openai", "model_id": "m", "api_key": KEY, "initial_capital": 10000.0} for n in ("a", "b")]}
    for i in ("c1", "c2"):
        db.execute("INSERT INTO competitions VALUES (?,?,'running',1,NULL,NULL,'2026-11-01','2026-11-01')", (i, json.dumps(config)))
    row = "INSERT INTO decisions (competition_id, model_name, cycle_number, action, symbol, quantity, confidence, reasoning, price_at_decision, trade_status, timestamp) VALUES ('c1',?,1,?,'BTC/USD',0,0.5,?,50000,'executed','2026-11-02T10:00:00')"
    db.execute(row, ("a", "buy", "cheap"))
    db.execute(row, ("b", "hold", "local runtime unavailable: timed out"))
    db.execute("INSERT INTO snapshots VALUES ('s1','c1','a',1,12500,9998,0,25,1,1,'2026-11-02T10:00:05')")
    db.execute("DELETE FROM competitions WHERE competition_id = 'c2'")  # its page stays in the file
    db.commit()
    db.close()
    old = sqlite3.connect(os.path.join(folder, "paper_trading.db"))
    old.executescript("CREATE TABLE pt_portfolios (id TEXT, name TEXT); CREATE TABLE pt_trades (portfolio_id TEXT, symbol TEXT, side TEXT, quantity REAL, price REAL, fee REAL, timestamp TEXT);")
    old.execute("INSERT INTO pt_portfolios VALUES ('p','alpha_arena_c1_a')")
    old.execute("INSERT INTO pt_trades VALUES ('p','BTC/USD','buy',0.05,50000,2.5,'2026-11-01T00:00:00Z')")
    old.commit()
    old.close()
    store = Store(Path(folder))
    with open(store.path, "rb") as fh:
        check("a key saved earlier is gone from the file", KEY.encode() not in fh.read())
    s = arena.state(store, "c1")
    board = {r["model_name"]: r for r in s["leaderboard"]}
    check("an old purchase is no profit any more", near(board["a"]["portfolio_value"], 9997.5) and near(board["a"]["cash"], 7497.5), board)
    check("...in its cycles' values too", store.values("c1", "a") == [9997.5], store.values("c1", "a"))
    by = {d["model_name"]: d for d in s["decisions"]}
    check("a HOLD nobody decided on is none", by["b"]["action"] == "none" and "timed out" in by["b"]["error"] and by["a"]["action"] == "buy", by)
    check("it is done once", arena.state(Store(Path(folder)), "c1")["leaderboard"] == s["leaderboard"])

if FAILURES:
    print(f"\n{len(FAILURES)} failure(s)")
    sys.exit(1)
print("\nall passed")
