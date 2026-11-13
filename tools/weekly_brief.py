#!/usr/bin/env python3
"""Weekly brief: insider-buying clusters and the IPO pipeline, as Markdown.

Built from finterm's own data pipelines — the same code the Ownership and
IPO Watch screens run — so the published numbers are the ones the app shows:

  1. Insider buying clusters (sec_form4_market): open-market purchases filed
     on SEC Form 4 in the last 7 days, grouped per issuer, at least two
     distinct insiders, 10%-owners and 10b5-1 plan trades excluded.
  2. IPO pipeline (ipo_calendar): deals projected to price in the next 30
     days, each with the projection's basis (underwriters' date, an
     exchange/SEC step on file, or the comparable-filer estimate).
  3. Lock-up expiries in the next 30 days.

Sources: SEC EDGAR (public domain) and Nasdaq's public IPO calendar. The
output states its method and limits; it is a general publication, not advice.

Usage:
    python3 tools/weekly_brief.py [--out DIR] [--no-scan]

SEC refuses clients that don't identify themselves: the contact email is read
from FINTERM_CONTACT_EMAIL, else from finterm's keyring entry
(Settings › Credentials › Contact email). It is never written to the output.
"""

import argparse
import os
import sys
from datetime import date, timedelta
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent.parent / "app" / "scripts"
sys.path.insert(0, str(SCRIPTS))

CONTACT_KEY = "FINTERM_CONTACT_EMAIL"

# finterm's own 12-month check of this exact filter (filing-date anchor,
# 2025-09 → 2026-09): median 1-month return after the filing +1.8%, 59% of
# clusters positive. A description of the past, not a forecast.
CLUSTER_HISTORY = "+1.8% median one-month return after the filing date, 59% positive (12-month check, 2025-09 to 2026-09)"

DISCLAIMER = (
    "This is a general summary of public filings, published for information only. It is not investment "
    "advice or a recommendation to buy or sell any security, and it does not account for anyone's "
    "circumstances. Filings can be late, amended or wrong; projections are estimates from past patterns "
    "and are often missed. Check the original filings before relying on anything here."
)


def contact_from_keyring():
    """The SEC contact from finterm's keyring entry (current identity, then an
    older build's), or None. Never printed."""
    try:
        import gi

        gi.require_version("Secret", "1")
        from gi.repository import Secret
    except Exception:
        return None
    # Current keyring identity and key name first, then what an older build
    # left before its first launch of this version moved it.
    for name, attr, key in (("finterm", "finterm-key", CONTACT_KEY),
                            ("finterm", "finterm-key", "FINCEPT_CONTACT_EMAIL"),
                            ("com.fincept.terminal", "fincept-key", "FINCEPT_CONTACT_EMAIL")):
        try:
            schema = Secret.Schema.new(name, Secret.SchemaFlags.NONE, {attr: Secret.SchemaAttributeType.STRING})
            value = Secret.password_lookup_sync(schema, {attr: key}, None)
            if value:
                return value
        except Exception:
            continue
    return None


def money(v):
    if v is None:
        return "—"
    for unit, div in (("B", 1e9), ("M", 1e6), ("K", 1e3)):
        if abs(v) >= div:
            return f"${v / div:,.1f}{unit}"
    return f"${v:,.0f}"


def md_escape(s):
    return str(s or "").replace("|", "\\|").replace("\n", " ").strip()


def clusters_section(scan):
    import sec_form4_market as f4

    notes = []
    if scan:
        try:
            f4.scan(days=7)
        except Exception as e:  # stale data is reported, not hidden
            notes.append(f"The SEC scan failed this week ({type(e).__name__}); rows below may be incomplete.")
    res = f4.recent(days=7, min_insiders=2, min_value=50_000.0)
    rows = res["rows"]
    out = ["## Insider buying clusters", ""]
    out.append(
        "Companies where **two or more insiders** bought shares **on the open market** in filings from the last "
        "7 days (SEC Form 4, code P). Grants, option exercises, 10%-owner purchases and pre-scheduled 10b5-1 "
        "plan trades are excluded. Joint filers count once."
    )
    out.append("")
    out.append(f"How this filter has done before: {CLUSTER_HISTORY}.")
    out.append("")
    if res["days_scanned"] < res["days_wanted"]:
        notes.append(f"Only {res['days_scanned']} of {res['days_wanted']} filing days in the window were scanned.")
    if not rows:
        out.append("_No clusters this week._")
    else:
        out.append("| Symbol | Company | Insiders | Bought | Avg price | Trade dates | Roles |")
        out.append("|---|---|---:|---:|---:|---|---|")
        for r in rows[:25]:
            avg = f"${r['avg_price']:,.2f}" if r.get("avg_price") else "—"
            dates = r["first_trade"] if r["first_trade"] == r["last_trade"] else f"{r['first_trade']} – {r['last_trade']}"
            out.append(
                f"| {md_escape(r['symbol']) or '—'} | {md_escape(r['issuer'])} | {r['insiders']} | "
                f"{money(r['value'])} | {avg} | {dates} | {md_escape(', '.join(r['roles'][:3]))} |"
            )
    for n in notes:
        out += ["", f"> {n}"]
    out.append("")
    return out


def ipo_sections(today):
    import ipo_calendar as ipo

    data = ipo.build(today=today, enrich=True)
    horizon = today + timedelta(days=30)
    upcoming = []
    for d in data["deals"]:
        p = d.get("projection") or {}
        if p.get("kind") not in ("scheduled", "imminent", "estimate") or not p.get("date"):
            continue
        when = date.fromisoformat(p["date"])
        # A window (imminent / estimate) is in range when any of it is; its
        # start is never shown in the past.
        lo = max(date.fromisoformat(p.get("lo") or p["date"]), today)
        hi = date.fromisoformat(p.get("hi") or p["date"])
        if lo > horizon or hi < today:
            continue
        p = dict(p, lo=lo.isoformat(), hi=max(hi, lo).isoformat())
        upcoming.append((lo, d, p))
    upcoming.sort(key=lambda t: (t[0], t[2].get("hi") or ""))  # by window start, then end

    out = ["## IPO pipeline — next 30 days", ""]
    out.append(
        "Deals projected to price within 30 days. **Basis** says what each date rests on: *scheduled* is the "
        "underwriters' date from Nasdaq's calendar; *imminent* means an exchange or SEC step (8-A, CERT, EFFECT) "
        "is on file; *estimate* is the pricing lag of comparable filers still unpriced at the same age."
    )
    out.append("")
    if not upcoming:
        out.append("_Nothing projected in the window._")
    else:
        out.append("| Projected | Ticker | Company | Range | Basis |")
        out.append("|---|---|---|---|---|")
        for when, d, p in upcoming[:30]:
            window = when.isoformat()
            if p.get("lo") and p.get("hi") and p["lo"] != p["hi"]:
                window = f"{p['lo']} – {p['hi']}"
            rng = d.get("price_range") or (f"${d['final_price']}" if d.get("final_price") else "—")
            out.append(
                f"| {window} | {md_escape(d.get('ticker')) or '—'} | {md_escape(d.get('company'))} | "
                f"{md_escape(rng)} | {p['kind']}: {md_escape(p.get('basis'))} |"
            )
    out.append("")

    lockups = [l for l in data.get("lockups", []) if today <= date.fromisoformat(l["expiry"]) <= horizon]
    out += ["## Lock-up expiries — next 30 days", ""]
    out.append(
        "When insiders' and early holders' post-IPO lock-up ends. From each deal's own prospectus term where "
        "Nasdaq has it; *assumed* rows use the common 180 days. Early-release clauses and waivers are not modelled."
    )
    out.append("")
    if not lockups:
        out.append("_None in the window._")
    else:
        out.append("| Expiry | Ticker | Company | Priced | Term |")
        out.append("|---|---|---|---|---|")
        for l in lockups:
            term = f"{l['lockup_days']}d" + (" (assumed)" if l["basis"] == "assumed" else "")
            out.append(f"| {l['expiry']} | {md_escape(l['ticker'])} | {md_escape(l['company'])} | "
                       f"{l['priced_date']} | {term} |")
    for e in data.get("errors", []):
        out += ["", f"> {e}"]
    out.append("")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", help="output folder (default: finterm's files folder, weekly/)")
    ap.add_argument("--no-scan", action="store_true", help="use the Form 4 data already scanned; don't fetch")
    args = ap.parse_args()

    if not os.environ.get(CONTACT_KEY):
        c = contact_from_keyring()
        if c:
            os.environ[CONTACT_KEY] = c
    if not os.environ.get(CONTACT_KEY):
        print("note: no SEC contact email found (Settings › Credentials › Contact email); "
              "SEC may refuse the requests.", file=sys.stderr)

    import finterm_paths

    out_dir = Path(args.out) if args.out else finterm_paths.user_root() / "files" / "weekly"
    out_dir.mkdir(parents=True, exist_ok=True)
    today = date.today()
    week_of = today - timedelta(days=today.weekday())

    lines = [f"# Weekly brief — week of {week_of:%B %-d, %Y}", ""]
    lines.append(f"_Generated {today.isoformat()} from SEC EDGAR filings and Nasdaq's public IPO calendar._")
    lines.append("")
    lines += clusters_section(scan=not args.no_scan)
    lines += ipo_sections(today)
    lines += ["---", "", f"_{DISCLAIMER}_", ""]

    path = out_dir / f"weekly-{week_of.isoformat()}.md"
    path.write_text("\n".join(lines), encoding="utf-8")
    print(path)


if __name__ == "__main__":
    main()
