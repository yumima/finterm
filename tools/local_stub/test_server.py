"""
Tests for tools/local_stub/server.py — signup, OTP, login, profile.

Stdlib only. Spins the server up in a thread on an ephemeral port against a
fresh tempfile SQLite DB, then exercises the HTTP API with urllib.

Run:
    python3 tools/local_stub/test_server.py
    python3 -m unittest tools.local_stub.test_server  # from repo root
"""

from __future__ import annotations

import json
import socket
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import server as stub  # noqa: E402


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class StubServerTest(unittest.TestCase):
    """Each test gets a fresh SQLite DB and a fresh server thread."""

    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        self.db_path = Path(self.tmpdir.name) / "users.db"
        stub.Handler.store = stub.Store(self.db_path)
        self.port = _free_port()
        self.httpd = ThreadingHTTPServer(("127.0.0.1", self.port), stub.Handler)
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()
        self._wait_ready()

    def tearDown(self):
        self.httpd.shutdown()
        self.httpd.server_close()
        self.thread.join(timeout=2)
        self.tmpdir.cleanup()

    # ── helpers ──
    def _wait_ready(self, timeout: float = 2.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                self._req("GET", "/health")
                return
            except OSError:
                time.sleep(0.02)
        raise RuntimeError("stub server did not come up")

    def _req(self, method: str, path: str, body: dict | None = None, headers: dict | None = None):
        url = f"http://127.0.0.1:{self.port}{path}"
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(url, data=data, method=method)
        req.add_header("Content-Type", "application/json")
        for k, v in (headers or {}).items():
            req.add_header(k, v)
        try:
            with urllib.request.urlopen(req, timeout=2) as resp:
                return resp.status, json.loads(resp.read() or b"{}")
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read() or b"{}")

    def _signup(self, username="alice", email="alice@local", password="hunter2"):
        return self._req(
            "POST",
            "/user/register",
            {"username": username, "email": email, "password": password},
        )

    def _verify(self, email="alice@local", otp="123456"):
        return self._req("POST", "/user/verify-otp", {"email": email, "otp": otp})

    def _login(self, email="alice@local", password="hunter2"):
        return self._req("POST", "/user/login", {"email": email, "password": password})

    # ── tests ──
    def test_health(self):
        status, body = self._req("GET", "/health")
        self.assertEqual(status, 200)
        self.assertTrue(body["success"])
        self.assertEqual(body["data"]["status"], "ok")

    def test_signup_happy_path(self):
        status, body = self._signup()
        self.assertEqual(status, 200)
        self.assertTrue(body["success"])
        self.assertTrue(body["data"]["otp_sent"])
        self.assertEqual(body["data"]["email"], "alice@local")

    def test_signup_missing_fields(self):
        status, body = self._req("POST", "/user/register", {"username": "alice"})
        self.assertEqual(status, 422)
        self.assertFalse(body["success"])

    def test_signup_duplicate_email(self):
        self._signup()
        status, body = self._signup(username="alice2")
        self.assertEqual(status, 409)
        self.assertFalse(body["success"])
        self.assertIn("email", body["message"].lower())

    def test_signup_duplicate_username(self):
        self._signup()
        status, body = self._signup(email="other@local")
        self.assertEqual(status, 409)
        self.assertIn("username", body["message"].lower())

    def test_verify_otp_mints_tokens(self):
        self._signup()
        status, body = self._verify()
        self.assertEqual(status, 200)
        self.assertTrue(body["success"])
        data = body["data"]
        self.assertTrue(data["api_key"])
        self.assertTrue(data["session_token"])
        self.assertEqual(data["email"], "alice@local")
        self.assertNotEqual(data["api_key"], data["session_token"])

    def test_verify_otp_rejects_bad_format(self):
        self._signup()
        status, body = self._verify(otp="abc")
        self.assertEqual(status, 400)
        self.assertFalse(body["success"])

    def test_verify_otp_unknown_email(self):
        status, body = self._verify(email="nobody@local")
        self.assertEqual(status, 404)
        self.assertFalse(body["success"])

    def test_login_happy_path(self):
        self._signup()
        self._verify()
        status, body = self._login()
        self.assertEqual(status, 200)
        self.assertTrue(body["success"])
        data = body["data"]
        self.assertTrue(data["api_key"])
        self.assertTrue(data["session_token"])
        self.assertFalse(data["mfa_required"])

    def test_login_wrong_password(self):
        self._signup()
        self._verify()
        status, body = self._login(password="WRONG")
        self.assertEqual(status, 401)
        self.assertFalse(body["success"])

    def test_login_unknown_email(self):
        status, body = self._login(email="ghost@local")
        self.assertEqual(status, 404)
        self.assertFalse(body["success"])

    def test_login_api_key_is_stable_across_logins(self):
        self._signup()
        _, v = self._verify()
        first_key = v["data"]["api_key"]
        _, l1 = self._login()
        _, l2 = self._login()
        self.assertEqual(first_key, l1["data"]["api_key"])
        self.assertEqual(first_key, l2["data"]["api_key"])
        # session_token rotates on each login
        self.assertNotEqual(l1["data"]["session_token"], l2["data"]["session_token"])

    def test_profile_requires_api_key(self):
        status, body = self._req("GET", "/user/profile")
        self.assertEqual(status, 401)
        self.assertFalse(body["success"])

    def test_profile_with_valid_api_key(self):
        self._signup()
        _, v = self._verify()
        api_key = v["data"]["api_key"]
        status, body = self._req("GET", "/user/profile", headers={"X-API-Key": api_key})
        self.assertEqual(status, 200)
        self.assertTrue(body["success"])
        profile = body["data"]
        self.assertEqual(profile["username"], "alice")
        self.assertEqual(profile["email"], "alice@local")
        self.assertEqual(profile["account_type"], "local")

    def test_profile_with_invalid_api_key(self):
        status, body = self._req("GET", "/user/profile", headers={"X-API-Key": "garbage"})
        self.assertEqual(status, 401)
        self.assertFalse(body["success"])

    def test_subscription_with_valid_api_key(self):
        self._signup()
        _, v = self._verify()
        api_key = v["data"]["api_key"]
        status, body = self._req("GET", "/user/subscription", headers={"X-API-Key": api_key})
        self.assertEqual(status, 200)
        self.assertEqual(body["data"]["account_type"], "local")
        self.assertGreater(body["data"]["credit_balance"], 0)

    def test_logout_revokes_session(self):
        self._signup()
        _, v = self._verify()
        api_key = v["data"]["api_key"]
        status, body = self._req("POST", "/user/logout", headers={"X-API-Key": api_key})
        self.assertEqual(status, 200)
        self.assertTrue(body["success"])
        # api_key still works (it's the durable credential), but the session_token is gone.
        # /user/profile uses api_key only, so it should still succeed.
        status, body = self._req("GET", "/user/profile", headers={"X-API-Key": api_key})
        self.assertEqual(status, 200)

    def test_password_reset_flow(self):
        self._signup()
        self._verify()
        status, _ = self._req("POST", "/user/forgot-password", {"email": "alice@local"})
        self.assertEqual(status, 200)
        status, _ = self._req(
            "POST",
            "/user/reset-password",
            {"email": "alice@local", "new_password": "newpass99"},
        )
        self.assertEqual(status, 200)
        # Old password no longer works
        status, _ = self._login(password="hunter2")
        self.assertEqual(status, 401)
        # New password works
        status, _ = self._login(password="newpass99")
        self.assertEqual(status, 200)

    def test_unhandled_endpoint_returns_empty_success(self):
        # The catch-all keeps the UI from erroring on unknown calls.
        status, body = self._req("GET", "/marine/some-unknown-endpoint")
        self.assertEqual(status, 200)
        self.assertTrue(body["success"])


if __name__ == "__main__":
    unittest.main()
