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
check("output comes before the value", [e["t"] for e in r if e["t"] != "stream"] == ["result", "done"] and r[0]["t"] == "stream" and r[-3]["t"] == "stream", r)
check("a print without a newline is shown", text(k.run("print('a', end='')")) == "a")
check("unicode", text(k.run("print('é😀')")) == "é😀\n")
check("a lone surrogate does not break it", "\\ud83d" in text(k.run("print('\\ud83d')")))
check("stderr has its name", text(k.run("import sys\nprint('warn', file=sys.stderr)"), "stderr") == "warn\n")
# On Windows the cells' process has a hidden console: a program it starts
# without redirecting would write there, not to the cell (finterm_kernel's
# inherit_std_handles).
check("a subprocess's output too", text(k.run("import subprocess, sys\nsubprocess.run([sys.executable, '-c', 'print(7)'])\npass")) == "7\n")
check("...and its stderr", text(k.run("import subprocess, sys\nsubprocess.run([sys.executable, '-c', 'import sys; print(8, file=sys.stderr)'])\npass"), "stderr") == "8\n")
# cmd /c strips a line's first and last quote when it has more than two: wrap it once more
check("...through the shell too", text(k.run("import os, sys\nc = f'\"{sys.executable}\" -c \"print(9)\"'\n"
                                             "os.system(f'\"{c}\"' if os.name == 'nt' else c)\npass")).strip() == "9")
check("a STARTUPINFO's own handles are kept", os.name != "nt" or text(k.run(
    "import subprocess, sys, msvcrt, tempfile, os\ntf = tempfile.TemporaryFile()\nsi = subprocess.STARTUPINFO()\n"
    "si.dwFlags |= subprocess.STARTF_USESTDHANDLES\nh = msvcrt.get_osfhandle(tf.fileno())\nos.set_handle_inheritable(h, True)\n"
    "si.hStdOutput = si.hStdError = h\nsubprocess.run([sys.executable, '-c', 'print(6)'], startupinfo=si, close_fds=False)\n"
    "tf.seek(0)\nprint(tf.read().strip() == b'6')")) == "True\n")
check("a closed fd 1 does not stop a program starting", text(k.run(
    "import os, subprocess, sys\nkeep = os.dup(1)\nos.close(1)\ntry:\n    r = subprocess.run([sys.executable, '-c', 'pass']).returncode\n"
    "finally:\n    os.dup2(keep, 1)\n    os.close(keep)\nprint(r)")) == "0\n")
check("a captured subprocess is still captured", text(k.run("import subprocess, sys\nprint(subprocess.run([sys.executable, '-c', 'print(5)'], capture_output=True, text=True).stdout.strip() + '!')")) == "5!\n")
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
check("output is streamed while the cell runs", first["t"] == "stream" and first["text"].startswith("go"), first)
t0 = time.time()
k.send(op="interrupt", id=f"c{k.n}")
r = k.finish()
check("an interrupt ends a sleep", error(r)["ename"] == "KeyboardInterrupt" and time.time() - t0 < 5, r)
k.start("while True: pass")
time.sleep(0.5)
k.send(op="interrupt", id=f"c{k.n}")
check("...and a loop", error(k.finish())["ename"] == "KeyboardInterrupt")
k.send(op="interrupt", id=f"c{k.n}")
time.sleep(0.3)
check("an interrupt between cells is none", text(k.run("print(x)")) == "20\n")
hit = 0
for _ in range(20):
    k.start("import time\ntime.sleep(20)")
    k.send(op="interrupt", id=f"c{k.n}")
    hit += error(k.finish())["ename"] == "KeyboardInterrupt"
check("one that comes as its cell starts counts", hit == 20, hit)
k.start("for i in range(3):\n    print('.', end='')\n    time.sleep(0.4)")
seen = [k.read()["text"] for _ in range(2)]
k.finish()
check("a print without a newline is streamed", seen == [".", "."], seen)
if os.name == "posix":
    r = k.run("import ctypes\nctypes.PyDLL(None).write(1, b'x' * 200000, 200000)\npass")
    check("output from C with the interpreter held", len(text(r)) == 200000, len(text(r)))
    r = k.run("import os, sys\nif os.fork() == 0:\n    pass\nelse:\n    os.wait()\n    print('parent')")
    check("a fork that reaches the cell's end ends there", text(r) == "parent\n" and [e["t"] for e in r].count("done") == 1, r)
    check("...and leaves no second answer", text(k.run("print('next')")) == "next\n")
    r = k.run("import os, tempfile\nkeep = os.dup(1)\nf = tempfile.NamedTemporaryFile(delete=False)\nos.dup2(f.fileno(), 1)\nos.write(1, b'mine')\npass")
    r = k.run("os.dup2(keep, 1)\nopen(f.name, 'rb').read()")
    check("a cell's own file on descriptor 1 is its own", [e["text"] for e in r if e["t"] == "result"] == ["b'mine'"], r)

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

r = k2 = None
k2 = Kernel()
r = k2.run("class S(str):\n    pass\nclass R:\n    def __repr__(self): return S('mine')\nR()")
check("a value of the cell's own str type", [e["text"] for e in r if e["t"] == "result"] == ["mine"], r)
r = k2.run("class E(Exception):\n    def __str__(self): return S('bad')\nraise E()")
check("...and an error's", error(r)["evalue"] == "bad" and text(k2.run("print(1)")) == "1\n", r)
k2.start("pass")
k2.finish()
k2.send(op="interrupt", id=f"c{k2.n}")
time.sleep(0.2)
k2.send(op="run", id=f"c{k2.n}", n=1, code="print('again')")
check("a late interrupt does not stop the cell's next run", text(k2.finish()) == "again\n")
if os.name == "posix":
    k2.start("import multiprocessing as mp, os, signal, time\np = mp.get_context('fork').Process(target=time.sleep, args=(20,))\np.start()\nprint('up', flush=True)\nos.kill(os.getpid(), signal.SIGKILL)")
    t0, events = time.time(), []
    while not events or events[-1]["t"] != "ended":
        events.append(k2.read())
    check("a death with a forked child alive is told at once", time.time() - t0 < 12 and "up\n" in text(events), (time.time() - t0, events))

crash = Kernel()
crash.start("import os\nprint('last words', end='')\nos.write(2, b'free(): invalid pointer')\nos.abort()" if os.name == "posix"
            else "import os\nprint('last words', end='')\nos._exit(3)")
events = []
while not events or events[-1]["t"] != "ended":
    events.append(crash.read())
check("what a crash said last is shown", text(events) == "last words" and (os.name != "posix" or "invalid pointer" in text(events, "stderr")), events)
check("...and how it ended", events[-1]["how"] in ("signal 6", "exit code 3"), events[-1])
check("...and the process is over", crash.p.wait(timeout=10) == 0)

if FAILURES:
    print(f"\n{len(FAILURES)} failure(s)")
    sys.exit(1)
print("\nall passed")
