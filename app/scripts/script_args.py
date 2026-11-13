"""
Shared input handling for scripts launched by the app's PythonRunner.

PythonRunner passes the request JSON as argv[1]. Arguments larger than 8 KB
are spilled to a temp file and passed as "@/path/to/file" — the script reads
that file and deletes it. Manual runs may pipe the JSON on stdin instead.

(news_nlp.py / news_geolocation.py / news_correlation.py each carry their own
copy of resolve_arg(); new scripts should import this module instead.)
"""
import os
import sys


def resolve_arg(arg):
    """If arg starts with '@', return that file's content and delete the file."""
    if arg and arg.startswith("@"):
        path = arg[1:]
        with open(path, "r", encoding="utf-8") as f:
            data = f.read()
        try:
            os.remove(path)
        except OSError:
            pass
        return data
    return arg


def read_input():
    """Raw JSON text: argv[1] (resolving an "@file" spill), else stdin.

    stdin is read only when no argument was given — PythonRunner leaves the
    child's stdin open, so reading it unconditionally would block.
    """
    if len(sys.argv) > 1 and sys.argv[1].strip():
        return resolve_arg(sys.argv[1])
    return sys.stdin.read()
