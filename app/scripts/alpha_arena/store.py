"""The database: competitions, what was decided and what was filled."""

from __future__ import annotations

import json
import sqlite3
import sys
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path

from finterm_paths import feature_dir

from .book import Book, Fill
from .traders import clean

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
               "Empty response from LLM%", "Failed to parse LLM response%", "No LLM available%", "No decision from LLM%")


def now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


class Busy(Exception):
    """A cycle of the competition is running already."""


class Store:
    def __init__(self, folder: Path | None = None):
        self.folder = Path(folder) if folder else feature_dir("alpha_arena")
        self.path = self.folder / "alpha_arena.db"
        if self._version() < 2:
            # One process at a time, and each step by the version found then.
            with self._lock("migrate", wait_s=60):
                if self._version() < 1:
                    self._drop_saved_keys()
                if self._version() < 2:
                    self._to_version_2()

    def _version(self) -> int:
        with self._db() as db:
            return db.execute("PRAGMA user_version").fetchone()[0]

    @contextmanager
    def _db(self):
        db = sqlite3.connect(self.path, timeout=30)
        db.row_factory = sqlite3.Row
        try:
            with db:
                yield db
        finally:
            db.close()

    def cycle_lock(self, competition_id: str):
        """One cycle of a competition at a time."""
        return self._lock("cycle-" + "".join(c if c.isalnum() or c in "-_" else "_" for c in competition_id))

    @contextmanager
    def _lock(self, name: str, wait_s: float = 0):
        """A lock the system holds: it goes with the process, however that ends."""
        lock = sqlite3.connect(self.folder / f".{name}.lock", timeout=wait_s, isolation_level=None)
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
            db.executescript(_SCHEMA)
            for competition_id, text in db.execute("SELECT competition_id, config FROM competitions").fetchall():
                config = read_config(text)  # {} of one that can't be read: nor be used
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
        the fills were wrong (see book.py): they are worked out again. All of it
        or none: what fails is tried again at the next start."""
        db = sqlite3.connect(self.path, timeout=30, isolation_level=None)
        db.row_factory = sqlite3.Row
        try:
            db.execute("BEGIN IMMEDIATE")
            for statement in _SCHEMA_2.split(";"):
                if statement.strip():
                    db.execute(statement)
            for pattern in _NOT_A_HOLD:
                db.execute("UPDATE decisions SET error = reasoning, reasoning = '', action = 'none', trade_status = 'none', "
                           "confidence = NULL WHERE action = 'hold' AND reasoning LIKE ?", (pattern,))
            configs = {r["competition_id"]: read_config(r["config"])
                       for r in db.execute("SELECT competition_id, config FROM competitions")}
            self._import_fills(db, configs)
            self._rebuild_snapshots(db, configs)
            db.execute("PRAGMA user_version = 2")
            db.execute("COMMIT")
        except BaseException:
            if db.in_transaction:
                db.execute("ROLLBACK")
            raise
        finally:
            db.close()

    def _import_fills(self, db: sqlite3.Connection, configs: dict[str, dict]):
        old = self.folder / "paper_trading.db"
        if not old.exists():
            return
        src = sqlite3.connect(old.resolve().as_uri() + "?mode=ro", uri=True, timeout=30)
        try:
            try:
                tables = {r[0] for r in src.execute("SELECT name FROM sqlite_master WHERE type = 'table'")}
            except sqlite3.OperationalError:
                raise  # it is in use: the next start
            except sqlite3.DatabaseError as e:
                print(f"alpha_arena: {old}: {e}", file=sys.stderr)  # not a database: nothing to take
                return
            if not {"pt_trades", "pt_portfolios"} <= tables:
                return
            for competition_id, config in configs.items():
                for model in config.get("models", []):
                    trades = src.execute(
                        "SELECT t.symbol, t.side, t.quantity, t.price, t.fee, t.timestamp FROM pt_trades t "
                        "JOIN pt_portfolios p ON p.id = t.portfolio_id WHERE p.name = ? ORDER BY t.timestamp, t.rowid",
                        (f"alpha_arena_{competition_id}_{model.get('name')}",)).fetchall()
                    db.executemany("INSERT INTO fills (competition_id, model_name, cycle_number, symbol, side, quantity, "
                                   "price, fee, timestamp) VALUES (?,?,0,?,?,?,?,?,?)",
                                   [(competition_id, model.get("name"), *t) for t in trades])
        finally:
            src.close()

    @staticmethod
    def _rebuild_snapshots(db: sqlite3.Connection, configs: dict[str, dict]):
        prices: dict[tuple, dict] = {}
        for r in db.execute("SELECT competition_id, cycle_number, symbol, price_at_decision FROM decisions "
                            "WHERE price_at_decision > 0"):
            prices.setdefault((r["competition_id"], r["cycle_number"]), {})[r["symbol"]] = r["price_at_decision"]
        fills: dict[tuple, list] = {}
        for r in db.execute("SELECT * FROM fills ORDER BY id"):
            fills.setdefault((r["competition_id"], r["model_name"]), []).append(r)
        books: dict[tuple, list] = {}  # (book, how many fills are in it), going on from cycle to cycle
        for snap in db.execute("SELECT * FROM snapshots ORDER BY competition_id, model_name, cycle_number").fetchall():
            who, at = (snap["competition_id"], snap["model_name"]), prices.get((snap["competition_id"], snap["cycle_number"]))
            config = configs.get(snap["competition_id"])
            until = _utc(snap["timestamp"])
            if not config or not at or until is None:  # nothing to work it out from
                db.execute("DELETE FROM snapshots WHERE id = ?", (snap["id"],))
                continue
            entry = books.setdefault(who, [Book.of(capital_of(config, who[1]), []), 0])
            book, rows = entry[0], fills.get(who, [])
            while entry[1] < len(rows) and ((t := _utc(rows[entry[1]]["timestamp"])) is None or t <= until):
                r = rows[entry[1]]
                book.apply(Fill(r["symbol"], r["side"], r["quantity"], r["price"], r["fee"]))
                entry[1] += 1
            value = book.value(at)
            db.execute("UPDATE snapshots SET portfolio_value = ?, cash = ?, pnl = ?, return_pct = ?, positions_count = ?, "
                       "trades_count = ? WHERE id = ?",
                       (value, book.cash, value - book.initial_capital, (value / book.initial_capital - 1) * 100,
                        len(book.positions), book.trades, snap["id"]))
            db.execute("INSERT OR IGNORE INTO cycles VALUES (?,?,?,?)",
                       (snap["competition_id"], snap["cycle_number"], json.dumps({"prices": at}), snap["timestamp"]))

    # ── Competitions ─────────────────────────────────────────────────────────

    def create(self, competition_id: str, config: dict):
        with self._db() as db:
            db.execute("INSERT INTO competitions VALUES (?,?,'created',0,NULL,NULL,?,?)",
                       (competition_id, json.dumps(config), now(), now()))

    def competition(self, competition_id: str) -> dict | None:
        with self._db() as db:
            row = db.execute("SELECT * FROM competitions WHERE competition_id = ?", (competition_id,)).fetchone()
        config = read_config(row["config"]) if row else {}
        return {**dict(row), "config": config} if config.get("models") else None

    def competitions(self, limit: int) -> list[dict]:
        with self._db() as db:
            rows = db.execute("SELECT * FROM competitions ORDER BY created_at DESC LIMIT ?", (limit,)).fetchall()
        found = [{**dict(r), "config": read_config(r["config"])} for r in rows]
        return [c for c in found if c["config"].get("models")]

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
                           (competition_id, d.model_name, number, d.action, clean(d.symbol), d.quantity,
                            d.confidence, clean(d.reasoning),
                            data.get("prices", {}).get(d.symbol), d.status, d.pnl, at, clean(d.error) or None, d.price))
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

    def next_cycle(self, competition_id: str) -> int:
        with self._db() as db:
            return 1 + max(db.execute("SELECT COALESCE(MAX(cycle_count), 0) FROM competitions WHERE competition_id = ?",
                                      (competition_id,)).fetchone()[0],
                           db.execute("SELECT COALESCE(MAX(cycle_number), 0) FROM cycles WHERE competition_id = ?",
                                      (competition_id,)).fetchone()[0])

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


def read_config(text: str) -> dict:
    try:
        config = json.loads(text)
    except ValueError:
        return {}
    return config if isinstance(config, dict) else {}


def _utc(timestamp: str) -> datetime | None:
    """The fills' times were UTC ("Z"), the snapshots' local without saying so."""
    try:
        return datetime.fromisoformat(str(timestamp).replace("Z", "+00:00")).astimezone(timezone.utc)
    except ValueError:
        return None


def capital_of(config: dict, name: str) -> float:
    """Every competitor starts with the same; older configs could say otherwise."""
    model = next((m for m in config.get("models", []) if m.get("name") == name), {})
    return float(config.get("initial_capital") or model.get("initial_capital") or 10000)
