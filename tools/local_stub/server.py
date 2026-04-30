"""
Local stub server for the FinceptTerminal localhost-only fork.

Replaces api.fincept.in with a process running on http://127.0.0.1:8765.
Handles signup, OTP verification, login, profile, subscription, and the rest
of the endpoints the C++ client hits at runtime. Users are stored in a local
SQLite file (~/.fincept-localhost/users.db) — nothing ever leaves this machine.

Usage:
    python3 tools/local_stub/server.py
        [--host 127.0.0.1] [--port 8765] [--db PATH]

Notes:
- OTP verification accepts any 6-digit code. Real fincept sends an email; we
  just log the "OTP" to stdout for convenience.
- Endpoints that aren't auth/profile (marine/, research/, quantlib/, chat/,
  forum, macro events) return an empty success envelope so the UI doesn't
  show errors for features that don't have a local backend.
- Stdlib only — no pip install needed. Python 3.8+.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import logging
import os
import secrets
import sqlite3
import sys
import threading
import time
import uuid
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

LOG = logging.getLogger("fincept-localstub")

DEFAULT_DB = Path.home() / ".fincept-localhost" / "users.db"


# ── Storage ──────────────────────────────────────────────────────────────────


class Store:
    """Thread-safe SQLite store for users and sessions."""

    def __init__(self, db_path: Path):
        db_path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()
        self._conn = sqlite3.connect(str(db_path), check_same_thread=False)
        self._conn.row_factory = sqlite3.Row
        self._init_schema()

    def _init_schema(self):
        with self._lock:
            c = self._conn.cursor()
            c.executescript(
                """
                CREATE TABLE IF NOT EXISTS users (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    username TEXT UNIQUE NOT NULL,
                    email TEXT UNIQUE NOT NULL,
                    password_hash TEXT NOT NULL,
                    phone TEXT,
                    country TEXT,
                    country_code TEXT,
                    api_key TEXT UNIQUE NOT NULL,
                    verified INTEGER DEFAULT 1,
                    tier TEXT NOT NULL DEFAULT 'free',
                    created_at TEXT DEFAULT (datetime('now')),
                    last_login_at TEXT
                );
                CREATE TABLE IF NOT EXISTS sessions (
                    token TEXT PRIMARY KEY,
                    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                    created_at TEXT DEFAULT (datetime('now'))
                );
                """
            )
            # Migrate old DBs that pre-date the `tier` column.
            cols = [r[1] for r in c.execute("PRAGMA table_info(users)")]
            if "tier" not in cols:
                c.execute("ALTER TABLE users ADD COLUMN tier TEXT NOT NULL DEFAULT 'free'")
            self._conn.commit()

    @staticmethod
    def hash_password(password: str, salt: str | None = None) -> str:
        salt = salt or secrets.token_hex(16)
        digest = hashlib.pbkdf2_hmac("sha256", password.encode(), salt.encode(), 200_000).hex()
        return f"{salt}${digest}"

    @staticmethod
    def verify_password(password: str, stored: str) -> bool:
        try:
            salt, digest = stored.split("$", 1)
        except ValueError:
            return False
        check = hashlib.pbkdf2_hmac("sha256", password.encode(), salt.encode(), 200_000).hex()
        return hmac.compare_digest(check, digest)

    def find_by_email(self, email: str) -> sqlite3.Row | None:
        with self._lock:
            return self._conn.execute(
                "SELECT * FROM users WHERE email = ?", (email.lower(),)
            ).fetchone()

    def find_by_username(self, username: str) -> sqlite3.Row | None:
        with self._lock:
            return self._conn.execute(
                "SELECT * FROM users WHERE username = ?", (username.lower(),)
            ).fetchone()

    def find_by_api_key(self, api_key: str) -> sqlite3.Row | None:
        with self._lock:
            return self._conn.execute(
                "SELECT * FROM users WHERE api_key = ?", (api_key,)
            ).fetchone()

    def create_user(self, username, email, password, phone, country, country_code) -> sqlite3.Row:
        api_key = secrets.token_urlsafe(32)
        with self._lock:
            self._conn.execute(
                """INSERT INTO users (username, email, password_hash, phone, country,
                                       country_code, api_key)
                   VALUES (?, ?, ?, ?, ?, ?, ?)""",
                (
                    username.lower(),
                    email.lower(),
                    self.hash_password(password),
                    phone,
                    country,
                    country_code,
                    api_key,
                ),
            )
            self._conn.commit()
        return self.find_by_email(email)

    def issue_session(self, user_id: int) -> str:
        token = secrets.token_urlsafe(32)
        with self._lock:
            self._conn.execute("DELETE FROM sessions WHERE user_id = ?", (user_id,))
            self._conn.execute(
                "INSERT INTO sessions (token, user_id) VALUES (?, ?)", (token, user_id)
            )
            self._conn.execute(
                "UPDATE users SET last_login_at = datetime('now') WHERE id = ?", (user_id,)
            )
            self._conn.commit()
        return token

    def revoke_sessions(self, user_id: int) -> None:
        with self._lock:
            self._conn.execute("DELETE FROM sessions WHERE user_id = ?", (user_id,))
            self._conn.commit()

    def update_password(self, user_id: int, new_password: str) -> None:
        with self._lock:
            self._conn.execute(
                "UPDATE users SET password_hash = ? WHERE id = ?",
                (self.hash_password(new_password), user_id),
            )
            self._conn.commit()

    def set_tier(self, user_id: int, tier: str) -> None:
        with self._lock:
            self._conn.execute("UPDATE users SET tier = ? WHERE id = ?", (tier, user_id))
            self._conn.commit()


# ── Response helpers ─────────────────────────────────────────────────────────


def envelope_ok(data: dict | list | None = None, message: str = "OK") -> dict:
    return {"success": True, "message": message, "data": data if data is not None else {}}


def envelope_err(message: str) -> dict:
    return {"success": False, "message": message}


# Tier strings recognised by AuthTypes::SessionData::has_paid_plan():
# basic | standard | pro | enterprise. `free` is the unpaid floor.
VALID_TIERS = {"free", "basic", "standard", "pro", "enterprise"}


def user_to_profile(row: sqlite3.Row) -> dict:
    return {
        "user_id": row["id"],
        "username": row["username"],
        "email": row["email"],
        "phone": row["phone"] or "",
        "country": row["country"] or "",
        "country_code": row["country_code"] or "",
        "account_type": row["tier"] or "free",
        "credit_balance": 999999,
        "created_at": row["created_at"],
        "last_login_at": row["last_login_at"] or row["created_at"],
    }


def user_to_subscription(row: sqlite3.Row) -> dict:
    return {
        "user_id": row["id"],
        "account_type": row["tier"] or "free",
        "credit_balance": 999999,
        "credits_expire_at": "",
        "support_type": "premium",
        "last_credit_purchase_at": "",
        "created_at": row["created_at"],
    }


# ── HTTP handler ─────────────────────────────────────────────────────────────


class Handler(BaseHTTPRequestHandler):
    server_version = "FinceptLocalStub/1.0"
    store: Store  # set on the server instance

    def log_message(self, fmt, *args):
        LOG.info("%s - %s", self.address_string(), fmt % args)

    # ── helpers ──
    def _read_json(self) -> dict:
        length = int(self.headers.get("Content-Length") or 0)
        if length == 0:
            return {}
        raw = self.rfile.read(length)
        try:
            return json.loads(raw or b"{}")
        except json.JSONDecodeError:
            return {}

    def _send(self, status: int, body: dict):
        payload = json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, X-API-Key, X-Session-Token")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
        self.end_headers()
        self.wfile.write(payload)

    def _ok(self, data=None, message="OK"):
        self._send(200, envelope_ok(data, message))

    def _err(self, status: int, message: str):
        self._send(status, envelope_err(message))

    def _current_user(self) -> sqlite3.Row | None:
        api_key = self.headers.get("X-API-Key") or self.headers.get("x-api-key")
        if not api_key:
            return None
        return self.store.find_by_api_key(api_key)

    # ── verbs ──
    def do_OPTIONS(self):
        self._send(200, {})

    def do_GET(self):
        self._dispatch("GET")

    def do_POST(self):
        self._dispatch("POST")

    def do_PUT(self):
        self._dispatch("PUT")

    def do_DELETE(self):
        self._dispatch("DELETE")

    # ── routing ──
    def _dispatch(self, method: str):
        url = urlparse(self.path)
        path = url.path.rstrip("/") or "/"
        query = parse_qs(url.query)

        if path == "/health":
            return self._ok({"status": "ok", "ts": int(time.time())})

        # — Auth: signup → OTP → login —
        if method == "POST" and path == "/user/register":
            return self._handle_register()
        if method == "POST" and path == "/user/verify-otp":
            return self._handle_verify_otp()
        if method == "POST" and path == "/user/login":
            return self._handle_login()
        if method == "POST" and path == "/user/verify-mfa":
            return self._handle_verify_mfa()
        if method == "POST" and path == "/user/forgot-password":
            return self._ok({"sent": True}, "Reset code sent (localhost: any code works)")
        if method == "POST" and path == "/user/reset-password":
            return self._handle_reset_password()
        if method == "POST" and path == "/user/logout":
            user = self._current_user()
            if user:
                self.store.revoke_sessions(user["id"])
            return self._ok({"logged_out": True})

        # — Session pulse / validation —
        if path in ("/user/session-pulse", "/user/validate-session", "/auth/status", "/auth/validate"):
            user = self._current_user()
            if not user:
                return self._err(401, "Unauthorized")
            return self._ok({"valid": True, "user_id": user["id"]})

        # — Profile —
        if method == "GET" and path == "/user/profile":
            user = self._current_user()
            if not user:
                return self._err(401, "Unauthorized")
            return self._ok(user_to_profile(user))
        if method == "PUT" and path == "/user/profile":
            return self._ok({"updated": True})

        # — Subscription / credits —
        if method == "GET" and path in ("/user/subscription", "/user/subscriptions"):
            user = self._current_user()
            if not user:
                return self._err(401, "Unauthorized")
            return self._ok(user_to_subscription(user))
        if method == "GET" and path == "/user/credits":
            return self._ok({"credit_balance": 999999})
        if method == "GET" and path == "/user/usage":
            return self._ok({"events": []})
        if method == "GET" and path == "/user/login-history":
            return self._ok({"history": []})
        if method == "GET" and path == "/user/transactions":
            return self._ok({"transactions": []})

        # — Tier change (localhost-only fork) —
        if method == "POST" and path == "/user/set-tier":
            user = self._current_user()
            if not user:
                return self._err(401, "Unauthorized")
            body = self._read_json()
            tier = (body.get("tier") or "").strip().lower()
            if tier not in VALID_TIERS:
                return self._err(400, f"tier must be one of {sorted(VALID_TIERS)}")
            self.store.set_tier(user["id"], tier)
            updated = self.store.find_by_api_key(user["api_key"])
            return self._ok({"account_type": updated["tier"]}, "Tier updated")

        # — MFA —
        if method == "POST" and path in ("/user/mfa/enable", "/user/mfa/disable"):
            return self._ok({"mfa": False})
        if method == "POST" and path == "/user/regenerate-api-key":
            user = self._current_user()
            if not user:
                return self._err(401, "Unauthorized")
            new_key = secrets.token_urlsafe(32)
            with self.store._lock:
                self.store._conn.execute(
                    "UPDATE users SET api_key = ? WHERE id = ?", (new_key, user["id"])
                )
                self.store._conn.commit()
            return self._ok({"api_key": new_key})

        # — Account deletion —
        if method == "DELETE" and path == "/user/account":
            user = self._current_user()
            if not user:
                return self._err(401, "Unauthorized")
            with self.store._lock:
                self.store._conn.execute("DELETE FROM users WHERE id = ?", (user["id"],))
                self.store._conn.commit()
            return self._ok({"deleted": True})

        # — Pricing / cashfree (no payments in localhost) —
        if path == "/cashfree/plans":
            return self._ok([])
        if path == "/user/generate-checkout-token":
            return self._err(403, "Payments disabled in localhost-only fork")
        if path.startswith("/user/subscribe"):
            return self._ok({"subscribed": True})

        # — Support tickets / feedback (no-ops) —
        if path.startswith("/support/"):
            return self._ok([])

        # — Macro / economic calendar —
        if path == "/macro/upcoming-events":
            return self._ok({"events": [], "total_count": 0})

        # — Maritime intelligence (empty results, UI tolerates this) —
        if path.startswith("/marine/"):
            return self._ok({"vessels": [], "found_count": 0})

        # — Geopolitics —
        if path.startswith("/research/news-events"):
            return self._ok({"events": [], "pagination": {"total_events": 0}})

        # — QuantLib —
        if path.startswith("/quantlib/"):
            return self._ok({})

        # — Chat (LLM) — disabled; user should configure their own provider in the UI —
        if path.startswith("/chat/"):
            return self._ok({"sessions": []})
        if path == "/research/chat":
            return self._ok({"reply": "[localhost stub] LLM disabled. Configure your own provider in Settings → LLM."})
        if path.startswith("/research/llm/"):
            return self._ok({"status": "disabled"})

        # — Forum (read-only empty) —
        if path.startswith("/forum/"):
            return self._ok([])

        # — Default catch-all: empty success envelope so the UI doesn't error —
        LOG.warning("Unhandled %s %s — returning empty envelope", method, path)
        return self._ok({})

    # ── auth handlers ──
    def _handle_register(self):
        body = self._read_json()
        username = (body.get("username") or "").strip().lower()
        email = (body.get("email") or "").strip().lower()
        password = body.get("password") or ""

        if not username or not email or not password:
            return self._err(422, "username, email and password are required")
        if self.store.find_by_email(email):
            return self._err(409, "An account with this email already exists.")
        if self.store.find_by_username(username):
            return self._err(409, "This username is already taken.")

        self.store.create_user(
            username,
            email,
            password,
            body.get("phone"),
            body.get("country"),
            body.get("country_code"),
        )
        otp = "000000"
        LOG.info("[localhost-stub] OTP for %s = %s (any 6-digit code works)", email, otp)
        return self._ok({"otp_sent": True, "email": email}, "OTP sent (any 6-digit code works locally)")

    def _handle_verify_otp(self):
        body = self._read_json()
        email = (body.get("email") or "").strip().lower()
        otp = (body.get("otp") or "").strip()
        if not email or not otp:
            return self._err(422, "email and otp are required")
        if not (otp.isdigit() and len(otp) == 6):
            return self._err(400, "OTP must be 6 digits")
        user = self.store.find_by_email(email)
        if not user:
            return self._err(404, "Account not found")
        token = self.store.issue_session(user["id"])
        return self._ok(
            {
                "api_key": user["api_key"],
                "session_token": token,
                "user_id": user["id"],
                "username": user["username"],
                "email": user["email"],
            },
            "Verified",
        )

    def _handle_login(self):
        body = self._read_json()
        email = (body.get("email") or "").strip().lower()
        password = body.get("password") or ""
        if not email or not password:
            return self._err(422, "email and password are required")
        user = self.store.find_by_email(email)
        if not user:
            return self._err(404, "Account not found. Please check your email.")
        if not Store.verify_password(password, user["password_hash"]):
            return self._err(401, "Incorrect email or password.")
        token = self.store.issue_session(user["id"])
        return self._ok(
            {
                "api_key": user["api_key"],
                "session_token": token,
                "user_id": user["id"],
                "username": user["username"],
                "email": user["email"],
                "active_session": False,
                "mfa_required": False,
            },
            "Login successful",
        )

    def _handle_verify_mfa(self):
        body = self._read_json()
        email = (body.get("email") or "").strip().lower()
        user = self.store.find_by_email(email)
        if not user:
            return self._err(404, "Account not found")
        token = self.store.issue_session(user["id"])
        return self._ok(
            {
                "api_key": user["api_key"],
                "session_token": token,
                "user_id": user["id"],
            }
        )

    def _handle_reset_password(self):
        body = self._read_json()
        email = (body.get("email") or "").strip().lower()
        new_password = body.get("new_password") or body.get("password") or ""
        user = self.store.find_by_email(email)
        if not user or not new_password:
            return self._err(422, "email and new_password are required")
        self.store.update_password(user["id"], new_password)
        return self._ok({"reset": True}, "Password updated")


# ── Entry ────────────────────────────────────────────────────────────────────


def main(argv: list[str] | None = None):
    p = argparse.ArgumentParser(description="FinceptTerminal localhost stub server")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=8765)
    p.add_argument("--db", type=Path, default=DEFAULT_DB, help=f"SQLite path (default: {DEFAULT_DB})")
    p.add_argument("--verbose", "-v", action="store_true")
    args = p.parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )

    Handler.store = Store(args.db)
    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    LOG.info("FinceptTerminal localhost stub on http://%s:%d  (db=%s)", args.host, args.port, args.db)
    LOG.info("Signup → OTP (any 6 digits) → login. Users persist across restarts.")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        LOG.info("Shutting down")
        httpd.server_close()


if __name__ == "__main__":
    main(sys.argv[1:])
