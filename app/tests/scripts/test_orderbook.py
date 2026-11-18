"""get_orderbook: Nasdaq's real-time book, zeros when there is no book to show."""
import os
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))

import yfinance_data as y  # noqa: E402


class _Resp:
    def __init__(self, body):
        self._body = body

    def raise_for_status(self):
        pass

    def json(self):
        return self._body


def _book(bid, ask, bs="10", as_="20"):
    return {"data": {"primaryData": {"bidPrice": bid, "askPrice": ask, "bidSize": bs, "askSize": as_}}}


def _run(symbol, answers):
    calls = []

    def get(url, headers=None, timeout=None):
        calls.append(url)
        a = answers.pop(0)
        if isinstance(a, Exception):
            raise a
        return _Resp(a)

    sys.modules["requests"] = types.SimpleNamespace(get=get)
    y._book_asset.clear()
    return y.get_orderbook(symbol), calls


def main():
    r, calls = _run("AAPL", [_book("$352.85", "$352.88")])
    assert (r["bid"], r["ask"], r["bid_size"], r["ask_size"]) == (352.85, 352.88, 10, 20), r
    assert "/AAPL/info?assetclass=stocks" in calls[0], calls

    # An ETF: the stocks lookup has no book, the etf one does; remembered after.
    r, calls = _run("SPY", [_book("N/A", "N/A"), _book("729.68", "729.69")])
    assert (r["bid"], r["ask"]) == (729.68, 729.69) and len(calls) == 2, (r, calls)
    assert y._book_asset == {"SPY": "etf"}

    # A class share is asked for as Nasdaq writes it.
    r, calls = _run("BRK-B", [_book("$575.98", "$576.08")])
    assert "/BRK.B/" in calls[0] and r["bid"] == 575.98, (r, calls)

    # A crossed book is not a quote.
    r, _ = _run("AAPL", [_book("$353.00", "$352.00")])
    assert (r["bid"], r["ask"]) == (0, 0), r

    # One side empty: that side is 0 (a dash), the other shown.
    r, _ = _run("AAPL", [_book("$352.85", "N/A")])
    assert (r["bid"], r["ask"]) == (352.85, 0), r

    # A refusal on both classes: no book, not an exception.
    r, _ = _run("AAPL", [RuntimeError("403"), RuntimeError("403")])
    assert (r["bid"], r["ask"]) == (0, 0), r

    # Not a US listing: Nasdaq is not asked at all.
    for sym in ("VOD.L", "ES=F", "BTC-USD", "^GSPC", "EURUSD=X", ""):
        r, calls = _run(sym, [])
        assert (r["bid"], r["ask"]) == (0, 0) and not calls, (sym, r, calls)
    print("ok")


if __name__ == "__main__":
    main()
