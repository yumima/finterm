#!/usr/bin/env python3
"""The notebook's kernel (scripts/launch/finterm_kernel.py).

A cell used to be a process of its own in the screens' data queue: killed at
30 s, without the variables of the cell before, three of "run all" at once in
any order. Pinned here: one namespace, in order, with no deadline; output as it
comes, from subprocesses too; errors with the cell's own lines; an interrupt
that ends the cell and not the notebook; nothing a cell does reaches the
protocol.
"""

import json
import os
import subprocess
import sys
import threading
import time

KERNEL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts", "launch", "finterm_kernel.py")
FAILURES = []


def check(name, cond, detail=""):
    print(("PASS : " if cond else "FAIL : ") + name + ("" if cond else f" {detail!r}"))
    if not cond:
        FAILURES.append(name)


class Kernel:
    def __init__(self):
        self.p = subprocess.Popen([sys.executable, KERNEL], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.n = 0
        assert self.read()["t"] == "ready"

    def read(self):
        return json.loads(self.p.stdout.readline())

    def send(self, **msg):
        self.p.stdin.write((json.dumps(msg) + "\n").encode())
        self.p.stdin.flush()

    def start(self, code):
        self.n += 1
        self.send(op="run", id=f"c{self.n}", n=self.n, code=code)

    def finish(self):
        events = []
        while not events or events[-1]["t"] != "done":
            events.append(self.read())
        assert all(e["id"] == f"c{self.n}" for e in events), events
        return events

    def run(self, code):
        self.start(code)
        return self.finish()


def text(events, name="stdout"):
    return "".join(e["text"] for e in events if e["t"] == "stream" and e["name"] == name)


def error(events):
    return next((e for e in events if e["t"] == "error"), None)


watchdog = threading.Timer(90, lambda: os._exit(3))
watchdog.daemon = True
watchdog.start()
k = Kernel()

k.run("x = 20\ndef f():\n    return x + 1\n")
check("a cell has what the one before defined", text(k.run("print(f())")) == "21\n")
r = k.run("y = 5\ny * 2")
check("the last expression's value is shown", [e["text"] for e in r if e["t"] == "result"] == ["10"], r)
check("...and an assignment or None shows nothing", not any(e["t"] == "result" for e in k.run("None")))
r = k.run("print('first')\n3")
check("output comes before the value", [e["t"] for e in r] == ["stream", "result", "done"], r)
check("a print without a newline is shown", text(k.run("print('a', end='')")) == "a")
check("unicode", text(k.run("print('é😀')")) == "é😀\n")
check("a lone surrogate does not break it", "\\ud83d" in text(k.run("print('\\ud83d')")))
check("stderr has its name", text(k.run("import sys\nprint('warn', file=sys.stderr)"), "stderr") == "warn\n")
check("a subprocess's output too", text(k.run("import subprocess, sys\nsubprocess.run([sys.executable, '-c', 'print(7)'])\npass")) == "7\n")
check("a write to the descriptor", text(k.run("import os\nos.write(1, b'raw')\npass")) == "raw")

r = k.run("a = 1\nraise ValueError('no')")
e = error(r)
check("an error is named", e and (e["ename"], e["evalue"]) == ("ValueError", "no"), r)
check("...with the cell's line and no kernel frame",
      any("raise ValueError('no')" in t for t in e["traceback"]) and not any("finterm_kernel" in t for t in e["traceback"]),
      e["traceback"])
check("...and is not ok", r[-1]["ok"] is False)
check("what ran before it stays", text(k.run("print(a)")) == "1\n")
e = error(k.run("if True print(1)"))
check("a syntax error", e and e["ename"] == "SyntaxError" and not any("finterm_kernel" in t for t in e["traceback"]), e)
check("input() has nothing to read", error(k.run("input()"))["ename"] == "EOFError")
check("a cell cannot read the protocol", text(k.run("import os\nprint(os.read(0, 10))")) == "b''\n")
check("sys.exit() ends the cell only", k.run("import sys\nsys.exit()")[-1]["ok"] and text(k.run("print(x)")) == "20\n")
check("sys.exit(2) is an error", error(k.run("sys.exit(2)"))["ename"] == "SystemExit")
check("pickle finds the notebook's functions", text(k.run("import pickle\nprint(pickle.loads(pickle.dumps(f))())")) == "21\n")
check("a repr that raises is an error", error(k.run("class B:\n    def __repr__(s): 1/0\nB()"))["ename"] == "ZeroDivisionError")

k.start("print('go', flush=True)\nimport time\ntime.sleep(60)")
first = k.read()
check("output is streamed while the cell runs", first["t"] == "stream" and first["text"] == "go\n", first)
t0 = time.time()
k.send(op="interrupt")
r = k.finish()
check("an interrupt ends a sleep", error(r)["ename"] == "KeyboardInterrupt" and time.time() - t0 < 5, r)
k.start("while True: pass")
time.sleep(0.5)
k.send(op="interrupt")
check("...and a loop", error(k.finish())["ename"] == "KeyboardInterrupt")
k.send(op="interrupt")
time.sleep(0.3)
check("an interrupt between cells is none", text(k.run("print(x)")) == "20\n")

r = k.run("for i in range(300000): print('0123456789')\nprint('end')")
check("endless output is cut off and said", 2_000_000 <= len(text(r)) < 2_200_000 and "more output" in text(r), len(text(r)))
check("...for that cell only", text(k.run("print('ok')")) == "ok\n")
check("a mark in pieces: zero bytes pass", text(k.run("print('\\0a\\0')")) == "\0a\0\n")

k.p.stdin.close()
check("it ends when the app is gone", k.p.wait(timeout=10) == 0)
busy = Kernel()
busy.start("import time\ntime.sleep(60)")
time.sleep(0.3)
busy.p.stdin.close()
check("...in the middle of a cell too", busy.p.wait(timeout=10) == 0)

if FAILURES:
    print(f"\n{len(FAILURES)} failure(s)")
    sys.exit(1)
print("\nall passed")
