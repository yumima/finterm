"""Re-run the walk-forward validation. `python -m factor_rating` from scripts/.

Universe defaults to the largest US primary listings plus whatever is held in
the local portfolios, so the numbers move as holdings change. Pass --symbols to
pin an explicit list, or --years / --horizons to change the window.
"""

import argparse
import json
import os
import sqlite3
import sys

DEFAULT_DB = os.path.join(os.path.expanduser("~"), ".local", "share",
                          "com.fincept.terminal", "data", "users")

# Priced once daily at NAV with no high/low/volume — every indicator here is
# meaningless on them.
MUTUAL_FUNDS = {"EKWAX", "FNILX", "FSELX", "FTQGX", "GRHAX",
                "SWVXX", "VBIPX", "VMRXX", "VUSXX"}


def portfolio_symbols():
    if not os.path.isdir(DEFAULT_DB):
        return []
    out = set()
    for fn in os.listdir(DEFAULT_DB):
        if not fn.endswith(".db"):
            continue
        try:
            c = sqlite3.connect(os.path.join(DEFAULT_DB, fn))
            out |= {r[0] for r in c.execute("SELECT DISTINCT symbol FROM portfolio_assets")}
        except Exception:
            continue
    return sorted(out - MUTUAL_FUNDS)


def large_caps(limit=160):
    """Top US primary listings by market cap, via the public TradingView scanner."""
    import urllib.request
    payload = {
        "filter": [
            {"left": "market_cap_basic", "operation": "nempty"},
            {"left": "type", "operation": "equal", "right": "stock"},
            {"left": "is_primary", "operation": "equal", "right": True},
            {"left": "exchange", "operation": "in_range", "right": ["NASDAQ", "NYSE"]},
        ],
        "columns": ["name"],
        "sort": {"sortBy": "market_cap_basic", "sortOrder": "desc"},
        "range": [0, limit],
    }
    req = urllib.request.Request(
        "https://scanner.tradingview.com/america/scan",
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json", "User-Agent": "Mozilla/5.0"})
    data = json.loads(urllib.request.urlopen(req, timeout=45).read())["data"]
    return [r["d"][0].replace(".", "-") for r in data if "/" not in r["d"][0]]


def main(argv=None):
    ap = argparse.ArgumentParser(prog="factor_rating")
    ap.add_argument("--symbols", help="comma-separated list, overrides the default universe")
    ap.add_argument("--years", type=int, default=12)
    ap.add_argument("--horizons", default="21,63", help="forward windows in trading days")
    ap.add_argument("--refresh", action="store_true", help="ignore the cached panel")
    args = ap.parse_args(argv)

    if args.symbols:
        syms = [s.strip().upper() for s in args.symbols.split(",") if s.strip()]
    else:
        syms = sorted(set(large_caps()) | set(portfolio_symbols()))
    print(f"universe: {len(syms)} symbols, {args.years}y")

    from .panel import download_panel, clean
    from . import factors as F, validate as V

    p = clean(download_panel(syms, years=args.years, force=args.refresh))
    print(f"panel: {p['close'].shape[1]} usable symbols x {p['close'].shape[0]} days "
          f"({p['close'].index[0].date()} -> {p['close'].index[-1].date()})")

    raw = F.compute_factors(p)
    signals = {k: F.zscore_rows(v) for k, v in raw.items()}
    signals.update(V.baselines(p))
    signals["COMPOSITE (equal weight)"] = F.composite(raw)

    for h in [int(x) for x in args.horizons.split(",")]:
        fwd = V.forward_excess_return(p["close"], horizon=h)
        print(f"\n=== forward excess return, {h}d, non-overlapping ===")
        print(f"{'signal':<44}{'IC':>8}{'t':>7}{'mono':>7}{'n':>5}")
        print("-" * 71)
        for name, df in signals.items():
            r = V.summarise(name, df, fwd, step=h)
            if not r.get("n"):
                print(f"{name:<44}  (no observations)")
                continue
            flag = "  <- significant" if abs(r["ic_t"]) >= 2 else ""
            print(f"{name:<44}{r['ic']:>8.4f}{r['ic_t']:>7.2f}"
                  f"{r['mono']:>7.2f}{r['n']:>5}{flag}")
    print("\nIC = mean Spearman rank correlation with forward excess return.")
    print("|t| >= 2 is the bar. See __init__.py for what the last run found and "
          "why none of it licenses shipping this as a rating.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
