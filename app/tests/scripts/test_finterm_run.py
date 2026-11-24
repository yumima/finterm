"""launch/finterm_run.py: a script's arguments come over stdin, never on the
command line, and the script can't tell. PythonRunner starts every script
through it."""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

APP = Path(__file__).resolve().parents[2]
fails = 0


def check(name, ok, got=""):
    global fails
    if not ok:
        fails += 1
        print(f"FAIL - {name}: {got}")


PROBE = '''
import atexit, json, sys, pickle, threading
class K: pass
pickle.dumps(K())                       # __main__ is this script
atexit.register(lambda: pickle.dumps(K()) and print("atexit ok", file=sys.stderr))
threading.Timer(0.2, lambda: pickle.dumps(K()) and print("thread ok", file=sys.stderr)).start()
print(json.dumps({"argv": sys.argv[1:], "name": __name__, "file": __file__, "p0": sys.path[0],
                  "a0": sys.argv[0], "rest": sys.stdin.read()}))
'''

with tempfile.TemporaryDirectory() as d:
    d = Path(d).resolve()
    S, cwd = d / "scripts", d / "cwd"
    (S / "launch").mkdir(parents=True)
    (S / "pkg").mkdir()
    cwd.mkdir()
    RUN = S / "launch" / "finterm_run.py"
    shutil.copy(APP / "scripts" / "launch" / "finterm_run.py", RUN)
    (S / "probe.py").write_text(PROBE)
    (S / "pkg" / "__init__.py").write_text("")
    (S / "pkg" / "sib.py").write_text("X = 7\n")
    (S / "pkg" / "mod.py").write_text("from .sib import X\n" + PROBE)
    (S / "boom.py").write_text("import sys\ndef f():\n    raise ValueError('boom ' + sys.argv[1])\nf()\n")
    (S / "hook.py").write_text("import sys\nsys.excepthook = lambda *a: print('HOOK', file=sys.stderr)\n1/0\n")
    (S / "bye.py").write_text("import sys; print('{}'); sys.exit(3)\n")
    (S / "latin.py").write_bytes(b"# -*- coding: latin-1 -*-\nprint(len('\xe9'))\n")
    (d / "outside.py").write_text("print('ran outside')\n")

    def run(rel, args, rest=b"", raw=None):
        data = raw if raw is not None else json.dumps(args).encode() + b"\n" + rest
        env = dict(os.environ, PYTHONPATH=str(S))
        p = subprocess.run([sys.executable, str(RUN), rel], input=data, cwd=cwd, capture_output=True, timeout=60, env=env)
        return p.returncode, p.stdout.decode(), p.stderr.decode()

    # Quotes, a newline, not ASCII, longer than any command line allows.
    secret = "sk-öäü'\" \\n \n" + "x" * 300_000
    args = ["cmd", json.dumps({"k": secret}), "", "@/etc/hostname"]
    for rel, p0 in (("probe.py", S), ("pkg/mod.py", cwd)):
        code, out, err = run(rel, args, rest=b"more")
        check(f"{rel} runs", code == 0, err[-400:])
        o = json.loads(out) if out else {}
        check(f"{rel}: arguments arrive as given", o.get("argv") == args, o.get("argv", [])[:1])
        check(f"{rel}: it is __main__", o.get("name") == "__main__", o.get("name"))
        check(f"{rel}: sys.path[0]", o and Path(o["p0"]).resolve() == p0, o.get("p0"))
        check(f"{rel}: argv[0] and __file__ are the script", o and Path(o["a0"]) == S / rel and Path(o["file"]) == S / rel, o.get("a0"))
        check(f"{rel}: the rest of stdin is the script's", o.get("rest") == "more", o.get("rest"))
        check(f"{rel}: still __main__ in its threads and at exit", "thread ok" in err and "atexit ok" in err, err)

    code, out, err = run("probe.py", [], raw=b"")
    check("no arguments at all", code == 0 and json.loads(out)["argv"] == [], err[-200:])
    code, out, err = run("boom.py", ["arg"])
    check("a failing script exits 1", code == 1, code)
    check("its traceback begins at the script", "boom.py" in (err.splitlines() + ["", ""])[1], err)
    check("...without the launcher's frames", "finterm_run.py" not in err, err)
    check("a script's own excepthook is used", "HOOK" in run("hook.py", [])[2])
    check("a script's exit code is kept", run("bye.py", [])[0] == 3)
    check("a coding line is honoured", run("latin.py", [])[1].strip() == "1")
    check("arguments that are no list are refused", run("probe.py", None, raw=b'{"a":1}\n')[0] != 0)
    code, out, err = run("pkg/nothere.py", [])
    check("a script that isn't there fails", code != 0 and "nothere" in err, err)
    for rel in (str(d / "outside.py"), "../outside.py", "pkg/../../outside.py"):
        code, out, err = run(rel, [])
        check(f"nothing outside scripts/ is run ({rel})", code != 0 and "ran outside" not in out, out)
    if hasattr(os, "symlink") and os.name != "nt":
        os.symlink(d / "outside.py", S / "link.py")
        check("nor through a link", "ran outside" not in run("link.py", [])[1])

# The agents' run_python_script tool takes any name in scripts/.
check("the launcher is not a top-level script", not (APP / "scripts" / "finterm_run.py").exists())
# The rule itself: in PythonRunner a request's arguments reach nothing but the JSON line.
src = (APP / "src" / "python" / "PythonRunner.cpp").read_text(encoding="utf-8")
uses = re.findall(r"[^\n]*\breq\.args\b[^\n]*", src)
check("PythonRunner writes the arguments to stdin only", len(uses) == 1 and "well_formed(req.args)" in uses[0], uses)

print("finterm_run:", "ok" if not fails else f"{fails} failed")
sys.exit(1 if fails else 0)
