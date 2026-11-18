#!/usr/bin/env python3
"""FRED, keyless (scripts/fred_data.py — the app's one FRED reader).

No network: fredgraph.csv answers are shaped from live downloads. The guards
that matter: an error page or another series' file is a FAILURE, never an
empty series; FRED's "." is absent, never 0; bad ids are refused before any
request; the keyless answer has the keyed answer's shape, so What's On and
the Economics panels read one format.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
os.environ.pop("FRED_API_KEY", None)
import fred_data as f  # noqa: E402

FAILURES = []
GOOD = "observation_date,CPIAUCSL\n2026-08-01,.\n2026-09-01,336.298\n2026-10-01,337.476\n"


class _Resp:
    def __init__(self, text, status=200):
        self.text, self.status_code = text, status

    def raise_for_status(self):
        if self.status_code >= 400:
            raise f.requests.exceptions.HTTPError(f"HTTP {self.status_code}")


def _with_get(answer, fn):
    calls = []

    def fake_get(url, params=None, timeout=None):
        calls.append((url, dict(params or {})))
        return answer(params or {})

    real = f.session.get
    f.session.get = fake_get
    try:
        return fn(), calls
    finally:
        f.session.get = real


def test_fred_csv_recognised_by_header_only():
    assert f.is_fred_csv(GOOD, "CPIAUCSL")
    assert not f.is_fred_csv(GOOD, "PAYEMS")  # another series' file
    assert not f.is_fred_csv("<!DOCTYPE html><title>Error</title>", "CPIAUCSL")
    assert not f.is_fred_csv("", "CPIAUCSL")


def test_missing_value_is_absent_not_zero():
    obs = f.parse_fred_csv(GOOD)
    assert obs == [{"date": "2026-09-01", "value": 336.298}, {"date": "2026-10-01", "value": 337.476}], obs


def test_bad_ids_refused_before_any_request():
    out, calls = _with_get(lambda p: _Resp(GOOD), lambda: f.get_multiple_series(["../etc", "a b"], "2024-09-01"))
    assert calls == [], calls
    assert [r.get("error_code") for r in out] == ["BAD_SERIES_ID", "BAD_SERIES_ID"], out


def test_keyless_series_has_the_keyed_shape():
    out, calls = _with_get(lambda p: _Resp(GOOD), lambda: f.get_series("CPIAUCSL", "2024-09-01"))
    assert calls[0][0] == f.FRED_CSV and calls[0][1] == {"id": "CPIAUCSL", "cosd": "2024-09-01"}, calls
    assert out["series_id"] == "CPIAUCSL" and "error" not in out, out
    assert out["observations"][-1] == {"date": "2026-10-01", "value": 337.476}
    assert out["observation_count"] == 2


def test_error_page_is_a_failure_not_an_empty_series():
    out, _ = _with_get(lambda p: _Resp("<html>throttled</html>"), lambda: f.get_series("CPIAUCSL"))
    assert out.get("error") and out["series_id"] == "CPIAUCSL", out
    out, _ = _with_get(lambda p: _Resp("", 503), lambda: f.get_series("CPIAUCSL"))
    assert out.get("error_code") == "REQUEST_FAILED", out


def test_one_refused_series_does_not_lose_the_others():
    def answer(p):
        sid = p["id"]
        return _Resp(GOOD.replace("CPIAUCSL", sid)) if sid != "PAYEMS" else _Resp("", 429)

    out, _ = _with_get(answer, lambda: f.get_multiple_series(["CPIAUCSL", "PAYEMS", "UNRATE"], "2024-09-01"))
    assert [r["series_id"] for r in out] == ["CPIAUCSL", "PAYEMS", "UNRATE"]
    assert "error" not in out[0] and "error" not in out[2], out
    assert out[1].get("error_code") == "RATE_LIMITED", out[1]


def test_transform_still_needs_the_key():
    out, calls = _with_get(lambda p: _Resp(GOOD), lambda: f.get_series("CPIAUCSL", transform="pch"))
    assert calls == [] and out.get("error_code") == "MISSING_API_KEY", out


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("PASS", name)
            except Exception as e:  # noqa: BLE001
                FAILURES.append(name)
                print("FAIL", name, repr(e))
    sys.exit(1 if FAILURES else 0)
