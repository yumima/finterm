#!/usr/bin/env python3
"""Single-flight and caching behaviour of the yfinance daemon dispatch.

The cache is checked BEFORE the upstream call and populated AFTER it, so N
concurrent misses of the same key each saw a miss and each hit Yahoo. Six
network workers colliding on one ticker is routine — a portfolio refresh, a
chart repaint and a technicals recompute regularly coincide — and that
duplicate load is what pushes the account into 429 rate limiting, which a
user experiences as missing data rather than as slowness.

These tests drive the real `_daemon_dispatch` with a stubbed inner call, so
they exercise the actual locking rather than a re-implementation.
"""

import os
import sys
import threading
import time
import traceback

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))

# The daemon module imports yfinance/pandas at module scope, which a plain CI
# interpreter does not have — and a suite that only ever reports "skipped" is
# not a test. These tests exercise _daemon_dispatch's locking and stub the
# upstream call entirely, so the heavy imports are stand-ins: inject empty
# modules and the real dispatch code under test loads unchanged.
import types  # noqa: E402

for _name in ("yfinance", "pandas", "numpy", "requests", "curl_cffi"):
    if _name not in sys.modules:
        try:
            __import__(_name)
        except ImportError:
            sys.modules[_name] = types.ModuleType(_name)

import yfinance_data as yd  # noqa: E402

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"PASS : {name}")
    else:
        print(f"FAIL : {name} {detail}")
        FAILURES.append(name)


def reset_state():
    with yd._cache_lock:
        yd._cache.clear()
    with yd._inflight_lock:
        yd._inflight.clear()


def test_concurrent_misses_make_one_upstream_call():
    reset_state()
    calls = []
    barrier_released = threading.Event()

    def slow_inner(action, payload):
        calls.append(payload["symbol"])
        barrier_released.wait(timeout=5)  # hold the leader open
        return {"symbol": payload["symbol"], "price": 1.23}

    original = yd._daemon_dispatch_with_crumb_retry
    yd._daemon_dispatch_with_crumb_retry = slow_inner
    try:
        results = [None] * 8
        def worker(i):
            results[i] = yd._daemon_dispatch("quote", {"symbol": "AAPL"})

        threads = [threading.Thread(target=worker, args=(i,)) for i in range(8)]
        for t in threads:
            t.start()
        time.sleep(0.3)          # let followers queue behind the leader
        barrier_released.set()   # release the leader
        for t in threads:
            t.join(timeout=10)

        check("single-flight: 8 concurrent misses make 1 upstream call",
              len(calls) == 1, f"made {len(calls)}")
        check("single-flight: every caller still gets the result",
              all(r and r.get("price") == 1.23 for r in results),
              f"{results}")
    finally:
        yd._daemon_dispatch_with_crumb_retry = original


def test_different_keys_still_run_in_parallel():
    reset_state()
    calls = []
    lock = threading.Lock()

    def inner(action, payload):
        with lock:
            calls.append(payload["symbol"])
        time.sleep(0.05)
        return {"symbol": payload["symbol"], "price": 1.0}

    original = yd._daemon_dispatch_with_crumb_retry
    yd._daemon_dispatch_with_crumb_retry = inner
    try:
        syms = ["AAPL", "MSFT", "NVDA", "TSLA"]
        threads = [threading.Thread(target=yd._daemon_dispatch, args=("quote", {"symbol": s}))
                   for s in syms]
        start = time.time()
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=10)
        elapsed = time.time() - start

        # Each distinct key makes exactly one call — the real "not serialised"
        # property. A tight wall-clock budget would just flake on a loaded
        # runner, so the timing check is a generous sanity bound.
        check("each distinct key makes exactly one call", sorted(calls) == sorted(syms), f"{calls}")
        check("distinct keys are not serialised behind each other",
              elapsed < 2.0, f"took {elapsed:.3f}s")
    finally:
        yd._daemon_dispatch_with_crumb_retry = original


def test_leader_failure_releases_followers():
    reset_state()
    calls = []
    lock = threading.Lock()
    hold = threading.Event()

    def failing_inner(action, payload):
        with lock:
            calls.append(1)
        # A real rate-limited call takes time to fail. An instantly-failing
        # leader would finish before the followers even look, so they would
        # each become their own leader and the test would measure nothing.
        hold.wait(timeout=5)
        raise RuntimeError("upstream exploded")

    original = yd._daemon_dispatch_with_crumb_retry
    yd._daemon_dispatch_with_crumb_retry = failing_inner
    try:
        errors = []
        def worker():
            try:
                yd._daemon_dispatch("quote", {"symbol": "FAIL"})
            except Exception as e:
                errors.append(str(e))

        threads = [threading.Thread(target=worker) for _ in range(3)]
        start = time.time()
        for t in threads:
            t.start()
        time.sleep(0.3)   # let the followers queue behind the leader
        hold.set()        # now let it fail
        for t in threads:
            t.join(timeout=10)
        elapsed = time.time() - start

        # THE property this whole mechanism exists for. Errors are never
        # cached, so a design that hands followers back to the cache collapses
        # nothing precisely when it matters — each would make its own call,
        # after paying the leader's latency first. One call, shared failure.
        check("a failing leader makes ONE upstream call, not one per caller",
              len(calls) == 1, f"made {len(calls)}")
        check("every caller sees the failure", len(errors) == 3, f"{errors}")
        check("a failing leader releases followers promptly", elapsed < 5.0, f"took {elapsed:.1f}s")
        check("the in-flight entry is cleaned up", not yd._inflight, f"{yd._inflight}")
        check("the failure is not cached",
              yd._cache_get(yd._cache_key("quote", {"symbol": "FAIL"})) is None)
    finally:
        yd._daemon_dispatch_with_crumb_retry = original


def test_uncached_action_is_passthrough():
    reset_state()
    calls = []

    def inner(action, payload):
        calls.append(action)
        return {"ok": True}

    original = yd._daemon_dispatch_with_crumb_retry
    yd._daemon_dispatch_with_crumb_retry = inner
    try:
        # quote_orderbook is deliberately uncached (live bid/ask).
        for _ in range(3):
            yd._daemon_dispatch("quote_orderbook", {"symbol": "AAPL"})
        check("uncached actions are not collapsed or cached", len(calls) == 3, f"{len(calls)}")
    finally:
        yd._daemon_dispatch_with_crumb_retry = original


def test_hung_leader_is_taken_over_not_waited_on_forever():
    reset_state()
    # A leader that never returns (yfinance's socket path has no timeout)
    # must not poison its key: without takeover, EVERY later request for it
    # parks a worker for the full wait, permanently — with six workers the
    # daemon simply stops serving.
    original_wait = yd._INFLIGHT_WAIT_SEC
    yd._INFLIGHT_WAIT_SEC = 0.3  # keep the test quick; the logic is the same

    forever = threading.Event()
    calls = []
    lock = threading.Lock()

    def inner(action, payload):
        with lock:
            n = len(calls)
            calls.append(1)
        if n == 0:
            forever.wait(timeout=10)   # the hung leader
        return {"ok": True, "n": n}

    original = yd._daemon_dispatch_with_crumb_retry
    yd._daemon_dispatch_with_crumb_retry = inner
    try:
        leader = threading.Thread(target=yd._daemon_dispatch, args=("quote", {"symbol": "HUNG"}))
        leader.start()
        time.sleep(0.1)

        start = time.time()
        result = yd._daemon_dispatch("quote", {"symbol": "HUNG"})
        elapsed = time.time() - start

        check("a follower takes over from a hung leader", result is not None, f"{result}")
        check("takeover happens after the wait, not never", elapsed < 3.0, f"took {elapsed:.1f}s")

        forever.set()
        leader.join(timeout=10)
    finally:
        yd._daemon_dispatch_with_crumb_retry = original
        yd._INFLIGHT_WAIT_SEC = original_wait
        forever.set()


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        try:
            t()
        except Exception:
            print(f"ERROR: {t.__name__}")
            traceback.print_exc()
            FAILURES.append(t.__name__)
    print()
    if FAILURES:
        print(f"{len(FAILURES)} failure(s): {', '.join(FAILURES)}")
        return 1
    print("all daemon dispatch tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
