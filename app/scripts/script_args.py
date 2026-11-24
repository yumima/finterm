"""
Shared input handling for scripts launched by the app's PythonRunner, which
gives a script its request as argv[1] (through finterm_run.py, so of any size).
Manual runs may pipe the JSON on stdin instead.
"""
import sys


def read_input():
    """Raw JSON text: argv[1], else stdin."""
    if len(sys.argv) > 1 and sys.argv[1].strip():
        return sys.argv[1]
    return sys.stdin.read()
