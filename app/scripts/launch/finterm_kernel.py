"""
The notebook's Python: one process per notebook, its cells run in it one after
another, so what a cell defines the next one has.

    python finterm_kernel.py          (NotebookKernel starts it; not for hand)

The app and this process talk in lines of JSON, over the stdin and stdout the
process was started with:

    in   {"op": "run", "id": ..., "n": 3, "code": "..."}   {"op": "interrupt"}
    out  {"t": "ready", "python": "3.12.3"}
         {"t": "stream", "id", "name": "stdout"|"stderr", "text"}
         {"t": "result", "id", "text"}              the last expression's value
         {"t": "error", "id", "ename", "evalue", "traceback": [...]}
         {"t": "done", "id", "ok"}

A cell sees none of that. Descriptors 1 and 2 are pipes read here, so what it
prints is streamed as it comes, its C libraries' and its subprocesses' output
too; descriptor 0 is empty (input() raises EOFError). End of input ends the
process: the app is gone.
"""
import _thread
import ast
import codecs
import faulthandler
import io
import json
import linecache
import os
import queue
import signal
import sys
import threading
import traceback
import types
import uuid

MAX_STREAM = 2_000_000  # characters of one cell's output; a loop that prints for ever is cut off
MAX_RESULT = 200_000


class Channel:
    """The app's side: events out, one line each, from any thread."""

    def __init__(self, fd):
        self._fd = fd
        self._lock = threading.Lock()

    def send(self, **event):
        data = memoryview((json.dumps(event) + "\n").encode("ascii"))
        with self._lock:
            try:
                while data:
                    data = data[os.write(self._fd, data):]
            except OSError:
                os._exit(0)  # the app is gone


class Stream:
    """Descriptor `fd` (1 or 2) as a pipe, its bytes sent on as text."""

    def __init__(self, fd, name, kernel):
        self.name = name
        self._kernel = kernel
        self._fd = fd
        read_end, write_end = os.pipe()
        os.dup2(write_end, fd)  # inheritable: a cell's subprocesses write here too
        os.close(write_end)
        self._read_end = read_end
        # What says "all of this cell's output has been read": no cell prints it.
        self._mark = b"\0" + uuid.uuid4().bytes.replace(b"\0", b"\1") + b"\0"
        self._reached = threading.Event()
        threading.Thread(target=self._pump, daemon=True).start()

    def text_file(self):
        raw = io.FileIO(self._fd, "w", closefd=False)
        return io.TextIOWrapper(io.BufferedWriter(raw), encoding="utf-8", errors="backslashreplace", newline="\n",
                                line_buffering=True)

    def drain(self):
        """Wait until everything written so far has been sent."""
        self._reached.clear()
        try:
            os.write(self._fd, self._mark)
        except OSError:
            return  # the cell closed it
        self._reached.wait(5)

    def _pump(self):
        decoder = codecs.getincrementaldecoder("utf-8")("replace")
        pending = b""
        while True:
            try:
                data = os.read(self._read_end, 65536)
            except OSError:
                return
            if not data:
                return
            pending += data
            while (at := pending.find(self._mark)) >= 0:
                self._kernel.stream(self.name, decoder.decode(pending[:at], final=True))
                decoder.reset()
                pending = pending[at + len(self._mark):]
                self._reached.set()
            # The start of a mark may be at the end: it waits for the rest.
            keep = next((k for k in range(min(len(self._mark) - 1, len(pending)), 0, -1)
                         if pending.endswith(self._mark[:k])), 0)
            self._kernel.stream(self.name, decoder.decode(pending[:len(pending) - keep]))
            pending = pending[len(pending) - keep:]


class Kernel:
    def __init__(self):
        self._in_cell = False  # only then is an interrupt the cell's
        self._id = None
        self._sent = 0
        self._cells = queue.Queue()

        inp = os.fdopen(os.dup(0), "rb")
        self.app = Channel(os.dup(1))
        faulthandler.enable(os.fdopen(os.dup(2), "w"))  # a crash is told on the real stderr
        null = os.open(os.devnull, os.O_RDONLY)
        os.dup2(null, 0)
        os.close(null)
        sys.stdin = open(0, encoding="utf-8", closefd=False)
        self.streams = [Stream(1, "stdout", self), Stream(2, "stderr", self)]
        sys.stdout, sys.stderr = (s.text_file() for s in self.streams)
        sys.__stdout__, sys.__stderr__ = sys.stdout, sys.stderr

        signal.signal(signal.SIGINT, self._on_sigint)
        # Not this folder: the cells import libraries and finterm's scripts (PYTHONPATH).
        here = os.path.dirname(os.path.abspath(__file__))
        sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != here]
        sys.argv = [""]
        self.main = types.ModuleType("__main__")
        self.main.__builtins__ = __builtins__
        sys.modules["__main__"] = self.main
        threading.Thread(target=self._read, args=(inp,), daemon=True).start()

    # ── What comes in ────────────────────────────────────────────────────────

    def _read(self, inp):
        for line in inp:
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            if msg.get("op") == "run":
                self._cells.put(msg)
            elif msg.get("op") == "interrupt":
                self._interrupt()
        os._exit(0)  # the app is gone: so is whatever the cell was doing

    def _interrupt(self):
        if os.name == "posix":
            os.kill(os.getpid(), signal.SIGINT)  # also wakes a sleep or a blocked read
        else:
            _thread.interrupt_main()

    def _on_sigint(self, _signum, _frame):
        if self._in_cell:
            raise KeyboardInterrupt

    # ── What goes out ────────────────────────────────────────────────────────

    def stream(self, name, text):
        if not text or self._id is None or self._sent > MAX_STREAM:
            return
        self._sent += len(text)
        if self._sent > MAX_STREAM:
            text += "\n[more output than is shown]\n"
        self.app.send(t="stream", id=self._id, name=name, text=text)

    # ── Cells ────────────────────────────────────────────────────────────────

    def run(self):
        self.app.send(t="ready", python=sys.version.split()[0])
        while True:
            msg = self._cells.get()
            self._id, self._sent = msg["id"], 0
            ok = self._cell(str(msg.get("code", "")), f"<cell {msg.get('n', 0)}>")
            self._after_output()
            self.app.send(t="done", id=self._id, ok=ok)

    def _cell(self, code, name):
        linecache.cache[name] = (len(code), None, code.splitlines(True), name)  # for its tracebacks
        try:
            tree = compile(code, name, "exec", ast.PyCF_ONLY_AST, dont_inherit=True)
            last = tree.body.pop() if tree.body and isinstance(tree.body[-1], ast.Expr) else None
            body = compile(tree, name, "exec", dont_inherit=True)
            value_of = last and compile(ast.Expression(last.value), name, "eval", dont_inherit=True)
            try:
                self._in_cell = True
                exec(body, self.main.__dict__)
                value = eval(value_of, self.main.__dict__) if value_of else None
                text = None if value is None else repr(value)
            finally:
                self._in_cell = False
            if value is not None:
                self.main._ = value
                if len(text) > MAX_RESULT:
                    text = text[:MAX_RESULT] + " …"
                self._after_output()
                self.app.send(t="result", id=self._id, text=text)
            return True
        except SystemExit as e:
            if e.code in (None, 0):
                return True  # the cell ends; the notebook goes on
            self._error(e, name)
        except BaseException as e:  # KeyboardInterrupt too
            self._error(e, name)
        return False

    def _after_output(self):
        """What the cell printed goes before what follows it."""
        for f, s in zip((sys.__stdout__, sys.__stderr__), self.streams):
            try:
                f.flush()
            except (OSError, ValueError):
                pass
            s.drain()

    def _error(self, exc, name):
        self._in_cell = False
        self._after_output()
        tb = exc.__traceback__
        while tb is not None and tb.tb_frame.f_code.co_filename != name:
            tb = tb.tb_next  # this file's frames say nothing
        if tb is None:
            lines = traceback.format_exception_only(exc) if isinstance(exc, SyntaxError) else \
                traceback.format_exception(type(exc), exc, exc.__traceback__)
        else:
            lines = traceback.format_exception(type(exc), exc, tb)
        try:
            evalue = str(exc)
        except Exception:
            evalue = ""
        self.app.send(t="error", id=self._id, ename=type(exc).__name__, evalue=evalue,
                      traceback="".join(lines).rstrip("\n").split("\n"))


if __name__ == "__main__":
    Kernel().run()
