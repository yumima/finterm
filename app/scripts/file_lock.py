"""An exclusive lock on an open file, on every OS.

fcntl does not exist on Windows: the weekly brief, social posting and the
Screener's cache imported it inside their lock and failed there outright, and
the Form 4 scan ran unlocked (found when the test suite first ran on Windows,
2026-11-26). POSIX: flock. Windows: a one-byte msvcrt lock at offset 0 (allowed
past the end of an empty file). Both belong to the open file — another open()
of the same path, in this process or another, is kept out — and both go away
when the file is closed.
"""
import os
import time

if os.name == "nt":
    import msvcrt

    def _try(f):
        f.seek(0)
        try:
            msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
            return True
        except OSError:
            return False

    def unlock(f):
        f.seek(0)
        try:
            msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
        except OSError:
            pass
else:
    import fcntl

    def _try(f):
        try:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except OSError:
            return False

    def unlock(f):
        fcntl.flock(f, fcntl.LOCK_UN)


def try_lock(f):
    """Take the lock now, or return False."""
    return _try(f)


def lock(f, poll_s=0.05):
    """Wait until the lock is ours. (Windows' blocking msvcrt mode gives up
    after ten seconds, so both platforms poll the same way.)"""
    while not _try(f):
        time.sleep(poll_s)


def open_lock_file(path):
    """The lock file, opened for locking (created if missing, never truncated)."""
    return open(path, "a+")
