"""The database: competitions, what was decided and what was filled."""

from __future__ import annotations

import json
import sqlite3
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path

from finterm_paths import feature_dir

from .book import Book, Fill

_SCHEMA = """
CREATE TABLE IF NOT EXISTS competitions (
    competition_id TEXT PRIMARY KEY, config TEXT NOT NULL, status TEXT NOT NULL, cycle_count INTEGER DEFAULT 0,
    start_time TEXT, end_time TEXT, created_at TEXT NOT NULL, updated_at TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS decisions (
    id INTEGER PRIMARY KEY AUTOINCREMENT, competition_id TEXT NOT NULL, model_name TEXT NOT NULL,
    cycle_number INTEGER NOT NULL, action TEXT NOT NULL, symbol TEXT NOT NULL, quantity REAL NOT NULL,
    confidence REAL NOT NULL, reasoning TEXT, price_at_decision REAL, trade_status TEXT, trade_pnl REAL,
    timestamp TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS snapshots (
    id TEXT PRIMARY KEY, competition_id TEXT NOT NULL, model_name TEXT NOT NULL, cycle_number INTEGER NOT NULL,
    portfolio_value REAL NOT NULL, cash REAL NOT NULL, pnl REAL NOT NULL, return_pct REAL NOT NULL,
    positions_count INTEGER DEFAULT 0, trades_count INTEGER DEFAULT 0, timestamp TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS idx_snapshots_competition ON snapshots(competition_id, model_name, cycle_number);
"""

_SCHEMA_2 = """
CREATE TABLE fills (
    id INTEGER PRIMARY KEY AUTOINCREMENT, competition_id TEXT NOT NULL, model_name TEXT NOT NULL,
    cycle_number INTEGER NOT NULL, symbol TEXT NOT NULL, side TEXT NOT NULL, quantity REAL NOT NULL,
    price REAL NOT NULL, fee REAL NOT NULL, timestamp TEXT NOT NULL);
CREATE INDEX idx_fills ON fills(competition_id, model_name, id);
CREATE TABLE cycles (
    competition_id TEXT NOT NULL, cycle_number INTEGER NOT NULL, data TEXT NOT NULL, timestamp TEXT NOT NULL,
    PRIMARY KEY (competition_id, cycle_number));
CREATE TABLE decisions_2 (
    id INTEGER PRIMARY KEY AUTOINCREMENT, competition_id TEXT NOT NULL, model_name TEXT NOT NULL,
    cycle_number INTEGER NOT NULL, action TEXT NOT NULL, symbol TEXT NOT NULL, quantity REAL NOT NULL,
    confidence REAL, reasoning TEXT, price_at_decision REAL, trade_status TEXT, trade_pnl REAL,
    timestamp TEXT NOT NULL, error TEXT, fill_price REAL);
INSERT INTO decisions_2 SELECT id, competition_id, model_name, cycle_number, action, symbol, quantity, confidence,
    reasoning, price_at_decision, trade_status, trade_pnl, timestamp, NULL, NULL FROM decisions;
DROP TABLE decisions;
ALTER TABLE decisions_2 RENAME TO decisions;
CREATE INDEX idx_decisions_competition ON decisions(competition_id, model_name, cycle_number);
DROP TABLE IF EXISTS leaderboard_history;
"""

# What the engine before wrote as the reasoning of a HOLD nobody had decided on.
_NOT_A_HOLD = ("Error: %", "local runtime unavailable: %", "anthropic runtime unavailable: %",
               "Empty response from LLM%", "Failed to parse LLM response%")


def now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


class Busy(Exception):
    """A cycle of the competition is running already."""


class Store:
    def __init__(self, folder: Path | None = None):
        self.folder = Path(folder) if folder else feature_dir("alpha_arena")
        self.path = self.folder / "alpha_arena.db"
        with self._db() as db:
            db.executescript(_SCHEMA)
        self._drop_saved_keys()
        self._to_version_2()

    @contextmanager
    def _db(self):
        db = sqlite3.connect(self.path, timeout=30)
        db.row_factory = sqlite3.Row
        try:
            with db:
                yield db
        finally:
            db.close()

    @contextmanager
    def cycle_lock(self, competition_id: str):
        """One cycle of a competition at a time. A lock the system holds: it goes
        with the process, however that ends."""
        name = "".join(c if c.isalnum() or c in "-_" else "_" for c in competition_id)
        lock = sqlite3.connect(self.folder / f".cycle-{name}.lock", timeout=0, isolation_level=None)
        try:
            try:
                lock.execute("BEGIN EXCLUSIVE")
            except sqlite3.OperationalError:
                raise Busy("a cycle of this competition is running already") from None
            yield
        finally:
            lock.close()

    # ── Older databases ──────────────────────────────────────────────────────

    def _drop_saved_keys(self):
        """Configs were saved with their models' API keys until 4.1.0. Once per
        database: take them out, then rewrite the file, for what deleted and
        replaced rows left in its free pages."""
        db = sqlite3.connect(self.path, timeout=30)
        try:
            if db.execute("PRAGMA user_version").fetchone()[0] >= 1:
                return
            for competition_id, text in db.execute("SELECT competition_id, config FROM competitions").fetchall():
                config = json.loads(text)
                for model in config.get("models", []):
                    model.pop("api_key", None)
                db.execute("UPDATE competitions SET config = ? WHERE competition_id = ?",
                           (json.dumps(config), competition_id))
            db.commit()
            db.execute("VACUUM")
            db.execute("PRAGMA user_version = 1")
            db.commit()
        finally:
            db.close()

    def _to_version_2(self):
        """Portfolios from fills. The numbers of their own that were kept beside
        the fills were wrong (see book.py): they are worked out again."""
        db = sqlite3.connect(self.path, timeout=30, isolation_level=None)
        db.row_factory = sqlite3.Row
        try:
            db.execute("BEGIN IMMEDIATE")  # two processes may come here at once
            if db.execute("PRAGMA user_version").fetchone()[0] >= 2:
                db.execute("ROLLBACK")
                return
            for statement in _SCHEMA_2.split(";"):
                if statement.strip():
                    db.execute(statement)
            for pattern in _NOT_A_HOLD:
                db.execute("UPDATE decisions SET error = reasoning, reasoning = '', action = 'none', trade_status = 'none', "
                           "confidence = NULL WHERE action = 'hold' AND reasoning LIKE ?", (pattern,))
            self._import_fills(db)
            self._rebuild_snapshots(db)
            db.execute("PRAGMA user_version = 2")
            db.execute("COMMIT")
        except BaseException:
            if db.in_transaction:
                db.execute("ROLLBACK")
            raise
        finally:
            db.close()

    def _import_fills(self, db: sqlite3.Connection):
        old = self.folder / "paper_trading.db"
        if not old.exists():
            return
        src = sqlite3.connect(f"file:{old.as_posix()}?mode=ro", uri=True)
        try:
            for competition in db.execute("SELECT competition_id, config FROM competitions").fetchall():
                for model in json.loads(competition["config"]).get("models", []):
                    name = f"alpha_arena_{competition['competition_id']}_{model['name']}"
                    for t in src.execute(
                            "SELECT t.symbol, t.side, t.quantity, t.price, t.fee, t.timestamp FROM pt_trades t "
                            "JOIN pt_portfolios p ON p.id = t.portfolio_id WHERE p.name = ? ORDER BY t.timestamp, t.rowid",
                            (name,)).fetchall():
                        db.execute("INSERT INTO fills (competition_id, model_name, cycle_number, symbol, side, quantity, "
                                   "price, fee, timestamp) VALUES (?,?,0,?,?,?,?,?,?)",
                                   (competition["competition_id"], model["name"], *t))
        except sqlite3.OperationalError:
            pass  # not a paper trading database after all: nothing to take
        finally:
            src.close()

    @staticmethod
    def _rebuild_snapshots(db: sqlite3.Connection):
        configs = {r["competition_id"]: json.loads(r["config"])
                   for r in db.execute("SELECT competition_id, config FROM competitions")}
        for snap in db.execute("SELECT * FROM snapshots").fetchall():
            config = configs.get(snap["competition_id"])
            at = db.execute("SELECT symbol, price_at_decision FROM decisions WHERE competition_id = ? AND cycle_number = ? "
                            "AND price_at_decision IS NOT NULL LIMIT 1",
                            (snap["competition_id"], snap["cycle_number"])).fetchone()
            if config is None or at is None:
                db.execute("DELETE FROM snapshots WHERE id = ?", (snap["id"],))
                continue
            # The fills' times are UTC ("Z"), the snapshots' were local.
            until = datetime.fromisoformat(snap["timestamp"]).astimezone(timezone.utc).isoformat()
            fills = [Fill(r["symbol"], r["side"], r["quantity"], r["price"], r["fee"]) for r in db.execute(
                "SELECT * FROM fills WHERE competition_id = ? AND model_name = ? AND replace(timestamp,'Z','+00:00') <= ? "
                "ORDER BY id", (snap["competition_id"], snap["model_name"], until))]
            book = Book.of(capital_of(config, snap["model_name"]), fills)
            prices = {at["symbol"]: at["price_at_decision"]}
            value = book.value(prices)
            db.execute("UPDATE snapshots SET portfolio_value = ?, cash = ?, pnl = ?, return_pct = ?, positions_count = ?, "
                       "trades_count = ? WHERE id = ?",
                       (value, book.cash, value - book.initial_capital, (value / book.initial_capital - 1) * 100,
                        len(book.positions), book.trades, snap["id"]))
            db.execute("INSERT OR IGNORE INTO cycles VALUES (?,?,?,?)",
                       (snap["competition_id"], snap["cycle_number"], json.dumps({"prices": prices}), snap["timestamp"]))

    # ── Competitions ─────────────────────────────────────────────────────────

    def create(self, competition_id: str, config: dict):
        with self._db() as db:
            db.execute("INSERT INTO competitions VALUES (?,?,'created',0,NULL,NULL,?,?)",
                       (competition_id, json.dumps(config), now(), now()))

    def competition(self, competition_id: str) -> dict | None:
        with self._db() as db:
            row = db.execute("SELECT * FROM competitions WHERE competition_id = ?", (competition_id,)).fetchone()
        return {**dict(row), "config": json.loads(row["config"])} if row else None

    def competitions(self, limit: int) -> list[dict]:
        with self._db() as db:
            rows = db.execute("SELECT * FROM competitions ORDER BY created_at DESC LIMIT ?", (limit,)).fetchall()
        return [{**dict(r), "config": json.loads(r["config"])} for r in rows]

    def book(self, competition_id: str, config: dict, name: str) -> Book:
        with self._db() as db:
            rows = db.execute("SELECT * FROM fills WHERE competition_id = ? AND model_name = ? ORDER BY id",
                              (competition_id, name)).fetchall()
        return Book.of(capital_of(config, name),
                       [Fill(r["symbol"], r["side"], r["quantity"], r["price"], r["fee"]) for r in rows])

    def save_cycle(self, competition_id: str, number: int, data: dict, decisions: list, fills: list, snapshots: list):
        """A cycle, whole or not at all. `fills`: (model, Fill), `snapshots`: dicts."""
        at = now()
        with self._db() as db:
            db.execute("INSERT INTO cycles VALUES (?,?,?,?)", (competition_id, number, json.dumps(data), at))
            for d in decisions:
                db.execute("INSERT INTO decisions (competition_id, model_name, cycle_number, action, symbol, quantity, "
                           "confidence, reasoning, price_at_decision, trade_status, trade_pnl, timestamp, error, fill_price) "
                           "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                           (competition_id, d.model_name, number, d.action, d.symbol, d.quantity,
                            d.confidence, d.reasoning,
                            data.get("prices", {}).get(d.symbol), d.status, d.pnl, at, d.error or None, d.price))
            for name, f in fills:
                db.execute("INSERT INTO fills (competition_id, model_name, cycle_number, symbol, side, quantity, price, fee, "
                           "timestamp) VALUES (?,?,?,?,?,?,?,?,?)",
                           (competition_id, name, number, f.symbol, f.side, f.quantity, f.price, f.fee, at))
            for s in snapshots:
                db.execute("INSERT OR REPLACE INTO snapshots VALUES (?,?,?,?,?,?,?,?,?,?,?)",
                           (f"{competition_id}:{s['model_name']}:{number}", competition_id, s["model_name"], number,
                            s["portfolio_value"], s["cash"], s["total_pnl"], s["return_pct"], s["positions_count"],
                            s["trades_count"], at))
            db.execute("UPDATE competitions SET status = 'running', cycle_count = ?, updated_at = ?, "
                       "start_time = COALESCE(start_time, ?) WHERE competition_id = ?", (number, at, at, competition_id))

    def last_cycle(self, competition_id: str) -> dict:
        with self._db() as db:
            row = db.execute("SELECT data FROM cycles WHERE competition_id = ? ORDER BY cycle_number DESC LIMIT 1",
                             (competition_id,)).fetchone()
        return json.loads(row["data"]) if row else {}

    def decisions(self, competition_id: str, limit: int, model_name: str | None = None) -> list[dict]:
        where, args = "competition_id = ?", [competition_id]
        if model_name is not None:
            where, args = where + " AND model_name = ?", [*args, model_name]
        with self._db() as db:
            rows = db.execute(f"SELECT * FROM decisions WHERE {where} ORDER BY cycle_number DESC, id LIMIT ?",
                              (*args, limit)).fetchall()
        return [{"cycle_number": r["cycle_number"], "model_name": r["model_name"], "action": r["action"],
                 "symbol": r["symbol"], "quantity": r["quantity"], "confidence": r["confidence"],
                 "reasoning": r["reasoning"] or "", "error": r["error"] or "", "status": r["trade_status"] or "",
                 "price": r["fill_price"], "pnl": r["trade_pnl"], "timestamp": r["timestamp"]} for r in rows]

    def failures(self, competition_id: str) -> dict[str, int]:
        with self._db() as db:
            rows = db.execute("SELECT model_name, COUNT(*) n FROM decisions WHERE competition_id = ? AND action = 'none' "
                              "GROUP BY model_name", (competition_id,)).fetchall()
        return {r["model_name"]: r["n"] for r in rows}

    def values(self, competition_id: str, model_name: str) -> list[float]:
        """The portfolio's value at the end of each cycle."""
        with self._db() as db:
            rows = db.execute("SELECT portfolio_value FROM snapshots WHERE competition_id = ? AND model_name = ? "
                              "ORDER BY cycle_number", (competition_id, model_name)).fetchall()
        return [r["portfolio_value"] for r in rows]


def capital_of(config: dict, name: str) -> float:
    """Every competitor starts with the same; older configs could say otherwise."""
    model = next((m for m in config.get("models", []) if m.get("name") == name), {})
    return float(config.get("initial_capital") or model.get("initial_capital") or 10000)
