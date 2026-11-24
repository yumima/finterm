"""
Runs one of finterm's scripts with its arguments taken from stdin.

    finterm_run.py <script, relative to scripts/>      (arguments on stdin)

PythonRunner starts every script through this file. What a script is asked
often holds what is nobody else's business — API keys, a wallet's private key,
the user's positions — and any account on the machine can read a command line
(`ps`, /proc/<pid>/cmdline, Task Manager). So only the script's name is there;
the arguments are the first line of stdin, a JSON list of strings. Windows also
ends a command line at 32 KB; a line of stdin has no such end.

The script sees what it always saw: its arguments in sys.argv, itself as
__main__ (for good, so also in its threads and exit handlers), and sys.path[0]
as `python file` has it — or `python -m` for one in a sub-folder, which runs as
a module so that its relative imports work.

Only scripts/ is run from, and this file lives in a folder of it: the agents'
run_python_script tool takes a bare name in scripts/ and must not reach it.
"""
import importlib.util
import json
import os
import sys
import types

SCRIPTS = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def _resolve(rel):
    """(path, module name or None) of a script under SCRIPTS."""
    path = os.path.realpath(os.path.join(SCRIPTS, rel))
    inside = os.path.relpath(path, SCRIPTS).replace(os.sep, "/")
    if os.path.isabs(rel) or inside.startswith("../") or not inside.endswith(".py"):
        sys.exit(f"finterm_run.py: not one of finterm's scripts: {rel}")
    return path, inside[:-3].replace("/", ".") if "/" in inside else None


def _code(path, name, main):
    """The script's code, with `main` made to look as Python makes __main__."""
    if name is None:
        main.__file__ = path
        with open(path, "rb") as f:  # bytes: compile() honours a coding line
            return compile(f.read(), path, "exec", dont_inherit=True)
    spec = importlib.util.find_spec(name)  # imports the packages it is in
    if spec is None or spec.origin is None or os.path.realpath(spec.origin) != path:
        sys.exit(f"finterm_run.py: {name} is not {path}")
    main.__spec__, main.__loader__, main.__package__ = spec, spec.loader, spec.parent
    main.__file__ = path
    return spec.loader.get_code(name)


def _from_script(tb):
    """The traceback from the script's first frame on: ours says nothing."""
    here = os.path.abspath(__file__)
    while tb is not None and (os.path.abspath(tb.tb_frame.f_code.co_filename) == here
                              or tb.tb_frame.f_code.co_filename.startswith("<frozen importlib")):
        tb = tb.tb_next
    return tb


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: finterm_run.py script.py   (its arguments: a JSON list on stdin's first line)")
    path, name = _resolve(sys.argv[1])

    # Bytes, so the platform's text encoding has no say; readline, so whatever
    # follows the first line is still the script's to read.
    line = sys.stdin.buffer.readline()
    args = json.loads(line.decode("utf-8")) if line.strip() else []
    if not isinstance(args, list) or not all(isinstance(a, str) for a in args):
        sys.exit("finterm_run.py: the arguments must be a JSON list of strings")

    sys.argv = [path, *args]
    sys.path[0] = os.path.dirname(path) if name is None else os.getcwd()
    script = types.ModuleType("__main__")
    script.__builtins__ = __builtins__
    sys.modules["__main__"] = script
    try:
        exec(_code(path, name, script), script.__dict__)
    except Exception as exc:  # SystemExit passes: it is the script's own answer
        tb = _from_script(exc.__traceback__) or exc.__traceback__
        sys.excepthook(type(exc), exc.with_traceback(tb), tb)
        sys.stderr.flush()
        sys.exit(1)


if __name__ == "__main__":
    main()
