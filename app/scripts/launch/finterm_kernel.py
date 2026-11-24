"""
The notebook's Python: one process per notebook, its cells run in it one after
another, so what a cell defines the next one has.

    python finterm_kernel.py          (NotebookKernel starts it; not for hand)

Two processes. This one talks to the app and reads what the other, which runs
the cells, writes. Apart, because a cell may crash its Python, and what it said
last is what matters then; or print from C with the interpreter held, and a
reader in the same process would wait for what waits for it.

The app and this process talk in lines of JSON, over stdin and stdout:

    in   {"op": "run", "id": ..., "n": 3, "code": "..."}   {"op": "interrupt", "id": ...}
    out  {"t": "ready", "python": "3.12.3"}
         {"t": "stream", "id", "name": "stdout"|"stderr", "text"}
         {"t": "result", "id", "text"}              the last expression's value
         {"t": "error", "id", "ename", "evalue", "traceback": [...]}
         {"t": "done", "id", "ok"}
         {"t": "ended", "how"}                      the cells' process is gone

A cell's stdout and stderr are pipes, so its C libraries' and its subprocesses'
output is streamed too; its stdin is empty (input() raises EOFError). End of
the app's input ends both.
"""
import codecs
import json
import os
import secrets
import subprocess
import sys
import threading
from multiprocessing.connection import Client, Listener

MAX_STREAM = 2_000_000  # characters of one cell's output; a loop that prints for ever is cut off
MAX_RESULT = 200_000
DRAIN_S = 3  # for output already written to be read; longer, and a subprocess still holds the pipe


# ── The app's side ───────────────────────────────────────────────────────────

class Stream:
    """One of the cells' output pipes, read here and sent on as text."""

    def __init__(self, pipe, name, kernel, mark):
        self.name = name
        self._pipe, self._kernel, self._mark = pipe, kernel, mark
        self._reached = threading.Semaphore(0)
        self._thread = threading.Thread(target=self._pump, daemon=True)
        self._thread.start()

    def drained(self):
        """Once the cells' process has put its mark in the pipe: until it is read."""
        self._reached.acquire(timeout=DRAIN_S)

    def finish(self):
        """Once it is gone: until the pipe is empty."""
        self._thread.join(DRAIN_S)

    def _pump(self):
        decoder = codecs.getincrementaldecoder("utf-8")("replace")
        mark, pending = self._mark, b""
        while data := os.read(self._pipe.fileno(), 65536):
            pending += data
            while (at := pending.find(mark)) >= 0:
                self._kernel.stream(self.name, decoder.decode(pending[:at], final=True))
                decoder.reset()
                pending = pending[at + len(mark):]
                self._reached.release()
            # The start of a mark may be at the end: it waits for the rest.
            keep = next((k for k in range(min(len(mark) - 1, len(pending)), 0, -1) if pending.endswith(mark[:k])), 0)
            self._kernel.stream(self.name, decoder.decode(pending[:len(pending) - keep]))
            pending = pending[len(pending) - keep:]
        self._kernel.stream(self.name, decoder.decode(pending, final=True))


class Kernel:
    def __init__(self):
        self._out = sys.stdout.buffer
        self._lock = threading.Lock()
        self._id, self._sent = None, 0
        key, mark = secrets.token_bytes(32), b"\0" + secrets.token_hex(16).encode() + b"\0"
        with Listener(authkey=key) as listener:
            self._cells = subprocess.Popen(
                [sys.executable, os.path.abspath(__file__), "cells"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), bufsize=0)
            # The key over stdin: a command line is everybody's to read.
            hello = {"address": listener.address, "key": key.hex(), "mark": mark.hex()}
            self._cells.stdin.write((json.dumps(hello) + "\n").encode())
            self._cells.stdin.close()
            self._streams = [Stream(self._cells.stdout, "stdout", self, mark), Stream(self._cells.stderr, "stderr", self, mark)]
            # One that dies before it connects must not be waited for.
            threading.Thread(target=self._watch, args=(listener.address, key), daemon=True).start()
            self._conn = listener.accept()

    def _watch(self, address, key):
        self._cells.wait()
        try:
            Client(address, authkey=key).close()  # ends an accept() that still waits
        except OSError:
            pass

    def send(self, **event):
        with self._lock:
            try:
                self._out.write((json.dumps(event) + "\n").encode("ascii"))
                self._out.flush()
            except OSError:
                self._end()  # the app is gone

    def stream(self, name, text):
        if not text or self._sent > MAX_STREAM:
            return
        self._sent += len(text)
        if self._sent > MAX_STREAM:
            text += "\n[more output than is shown]\n"
        self.send(t="stream", id=self._id, name=name, text=text)

    def _end(self):
        self._cells.kill()
        os._exit(0)

    def _read_app(self):
        for line in sys.stdin.buffer:
            try:
                msg = json.loads(line)
                if msg.get("op") == "run":  # one at a time: the app waits for its "done"
                    self._id, self._sent = msg["id"], 0
                self._conn.send(msg)
            except (ValueError, OSError):
                pass
        self._end()

    def run(self):
        threading.Thread(target=self._read_app, daemon=True).start()
        while True:
            try:
                event = self._conn.recv()
            except (EOFError, OSError):
                break
            if event["t"] in ("result", "error", "done"):
                for s in self._streams:  # what was printed before it goes before it
                    s.drained()
            self.send(**event)
        for s in self._streams:
            s.finish()
        code = self._cells.wait()
        self.send(t="ended", how=f"signal {-code}" if code < 0 else f"exit code {code}")
        os._exit(0)  # a thread still reads stdin


# ── The cells' side ──────────────────────────────────────────────────────────

def cells():
    import ast
    import faulthandler
    import io
    import linecache
    import queue
    import signal
    import traceback
    import types

    hello = json.loads(sys.stdin.buffer.readline())
    if sys.platform == "linux":  # with the other process, also while C code holds the interpreter
        try:
            import ctypes
            ctypes.CDLL(None).prctl(1, signal.SIGKILL)
        except (OSError, AttributeError):
            pass
    conn = Client(hello["address"], authkey=bytes.fromhex(hello["key"]))
    mark, pid = bytes.fromhex(hello["mark"]), os.getpid()
    marks = [os.dup(1), os.dup(2)]  # a cell may put something else there
    null = os.open(os.devnull, os.O_RDONLY)
    os.dup2(null, 0)
    os.close(null)
    sys.stdin = open(0, encoding="utf-8", closefd=False)
    faulthandler.enable(os.fdopen(os.dup(2), "w"))

    class Whole(io.RawIOBase):
        """A descriptor, written at once and whole."""

        def __init__(self, fd, name):
            self._fd, self.name = fd, name

        def fileno(self):
            return self._fd

        def writable(self):
            return True

        def write(self, data):
            view = memoryview(data).cast("B")
            size = len(view)
            while view:
                view = view[os.write(self._fd, view):]
            return size

    def text_file(fd, name):
        return io.TextIOWrapper(Whole(fd, name), encoding="utf-8", errors="backslashreplace", newline="\n", write_through=True)

    sys.stdout, sys.stderr = text_file(1, "<stdout>"), text_file(2, "<stderr>")
    sys.__stdout__, sys.__stderr__ = sys.stdout, sys.stderr

    state = {"id": None, "in_cell": False, "interrupt": None}
    waiting = queue.Queue()

    def on_sigint(_signum, _frame):
        if state["in_cell"] and state["interrupt"] == state["id"]:
            state["interrupt"] = None
            raise KeyboardInterrupt

    def read():
        while True:
            try:
                msg = conn.recv()
            except (EOFError, OSError):
                os._exit(0)
            if msg.get("op") == "run":
                waiting.put(msg)
            elif msg.get("op") == "interrupt":
                state["interrupt"] = msg.get("id")
                if os.name == "posix":
                    os.kill(os.getpid(), signal.SIGINT)  # also wakes a sleep or a blocked read
                else:
                    signal.raise_signal(signal.SIGINT)  # its C handler sets the event a sleep waits for

    def drain():
        """What was printed before this is read before what is sent after it."""
        for fd, f in zip(marks, (sys.__stdout__, sys.__stderr__)):
            try:
                f.flush()
            except (OSError, ValueError):
                pass
            view = memoryview(mark)
            while view:
                view = view[os.write(fd, view):]

    def send(**event):
        drain()
        conn.send(event)

    def error(exc, name):
        state["in_cell"] = False
        tb = exc.__traceback__
        while tb is not None and tb.tb_frame.f_code.co_filename != name:
            tb = tb.tb_next  # this file's frames say nothing
        if tb is None:
            lines = traceback.format_exception_only(exc) if isinstance(exc, (SyntaxError, KeyboardInterrupt)) else \
                traceback.format_exception(type(exc), exc, exc.__traceback__)
        else:
            lines = traceback.format_exception(type(exc), exc, tb)
        try:
            evalue = str(exc)
        except Exception:
            evalue = ""
        send(t="error", id=state["id"], ename=type(exc).__name__, evalue=evalue,
             traceback="".join(lines).rstrip("\n").split("\n"))

    def cell(code, name):
        linecache.cache[name] = (len(code), None, code.splitlines(True), name)  # for its tracebacks
        try:
            tree = compile(code, name, "exec", ast.PyCF_ONLY_AST, dont_inherit=True)
            last = tree.body.pop() if tree.body and isinstance(tree.body[-1], ast.Expr) else None
            body = compile(tree, name, "exec", dont_inherit=True)
            value_of = last and compile(ast.Expression(last.value), name, "eval", dont_inherit=True)
            try:
                state["in_cell"] = True
                if state["interrupt"] == state["id"]:  # it came before the cell could have it
                    state["interrupt"] = None
                    raise KeyboardInterrupt
                exec(body, main.__dict__)
                value = eval(value_of, main.__dict__) if value_of else None
                text = None if value is None else repr(value)
            finally:
                state["in_cell"] = False
            if value is not None and os.getpid() == pid:
                main._ = value
                send(t="result", id=state["id"], text=text[:MAX_RESULT] + " …" if len(text) > MAX_RESULT else text)
            return True
        except SystemExit as e:
            if e.code in (None, 0):
                return True  # the cell ends; the notebook goes on
            exc = e
        except BaseException as e:  # KeyboardInterrupt too
            exc = e
        if os.getpid() == pid:
            error(exc, name)
        return False

    signal.signal(signal.SIGINT, on_sigint)
    # Not this folder: the cells import libraries and finterm's scripts (PYTHONPATH).
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != here]
    sys.argv = [""]
    main = types.ModuleType("__main__")
    main.__builtins__ = __builtins__
    sys.modules["__main__"] = main
    threading.Thread(target=read, daemon=True).start()
    conn.send({"t": "ready", "python": sys.version.split()[0]})
    while True:
        msg = waiting.get()
        state["id"] = msg["id"]
        ok = cell(str(msg.get("code", "")), f"<cell {msg.get('n', 0)}>")
        if os.getpid() != pid:
            os._exit(0)  # a fork that reached the end of its cell
        send(t="done", id=msg["id"], ok=ok)


if __name__ == "__main__":
    if sys.argv[1:] == ["cells"]:
        cells()
    else:
        Kernel().run()
