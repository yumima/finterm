#!/usr/bin/env python3
"""The data daemon over loopback TCP — the transport Windows depends on.

CPython on Windows has no AF_UNIX, so the Unix-socket daemon died at start-up
on every Windows machine and the app showed no data at all. `--tcp` is that
platform's only connection to market data, and it cannot be exercised on the
Linux CI through the app, so it is driven here directly: the real daemon
process, a real socket, the real frame protocol.

Loopback is open to every local process, so the rest of this is about who the
daemon serves: only a client that opens with the host's token.
"""

import json
import os
import socket
import subprocess
import sys
import traceback
import uuid

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts", "yfinance_data.py")
TOKEN = uuid.uuid4().hex  # per run, as the app makes one per launch


def frame(obj):
    b = json.dumps(obj).encode("utf-8")
    return len(b).to_bytes(4, "big") + b


def read_frame(read):
    head = b""
    while len(head) < 4:
        chunk = read(4 - len(head))
        if not chunk:
            return None
        head += chunk
    n = int.from_bytes(head, "big")
    body = b""
    while len(body) < n:
        chunk = read(n - len(body))
        if not chunk:
            return None
        body += chunk
    return json.loads(body.decode("utf-8"))


class Daemon:
    def __init__(self, token=TOKEN):
        env = dict(os.environ)
        env.pop("FINTERM_DAEMON_TOKEN", None)
        if token is not None:
            env["FINTERM_DAEMON_TOKEN"] = token
        self.p = subprocess.Popen([sys.executable, SCRIPT, "--daemon", "--tcp"], stdin=subprocess.DEVNULL,
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=env)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        if self.p.poll() is None:
            self.p.kill()
        self.p.wait(timeout=10)
        self.p.stdout.close()

    def ready(self):
        return read_frame(self.p.stdout.read)

    def connect(self, port):
        s = socket.create_connection(("127.0.0.1", port), timeout=10)
        s.settimeout(20)
        return s


def test_serves_the_host_that_knows_the_token():
    with Daemon() as d:
        ready = d.ready()
        assert ready and ready.get("ready") and isinstance(ready.get("port"), int) and ready["port"] > 0, ready
        with d.connect(ready["port"]) as s:
            s.sendall(frame({"id": 0, "action": "auth", "payload": {"token": TOKEN}}))
            # A request with no network behind it: the daemon's own shutdown.
            s.sendall(frame({"id": 7, "action": "shutdown", "payload": {}}))
            reply = read_frame(s.recv)
        assert reply == {"id": 7, "ok": True, "result": {"shutdown": True}}, reply
        assert d.p.wait(timeout=15) == 0


def test_listens_on_loopback_only():
    with Daemon() as d:
        port = d.ready()["port"]
        # Bound to 127.0.0.1, not 0.0.0.0: unreachable from another machine.
        out = subprocess.run(["ss", "-ltnH"], capture_output=True, text=True).stdout if os.name == "posix" else ""
        lines = [l for l in out.splitlines() if l.split()[3].endswith(f":{port}")] if out else []
        assert not out or (lines and all(l.split()[3].startswith("127.0.0.1:") for l in lines)), lines


def test_a_client_without_the_token_is_served_nothing():
    for first in ({"id": 1, "action": "auth", "payload": {"token": "wrong"}},
                  {"id": 1, "action": "quote", "payload": {"symbol": "AAPL"}},
                  {"id": 1, "action": "shutdown", "payload": {}}):
        with Daemon() as d:
            with d.connect(d.ready()["port"]) as s:
                s.sendall(frame(first))
                assert read_frame(s.recv) is None, first      # closed without a single reply
            assert d.p.wait(timeout=15) == 0


def test_no_token_means_no_port():
    with Daemon(token=None) as d:
        assert d.ready() is None and d.p.wait(timeout=15) == 0


def main():
    try:
        import yfinance  # noqa: F401 — the daemon module imports it at load
    except Exception:
        print("skipped: the app's Python environment (yfinance) is not present")
        return 77
    failures = []
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"PASS {name}")
            except Exception:
                traceback.print_exc()
                failures.append(name)
    if failures:
        print(f"{len(failures)} failure(s): {', '.join(failures)}")
        return 1
    print("all daemon TCP transport tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
