#!/usr/bin/env python3
"""Briefs as DRAFTS: generated, reviewed and corrected, then published now or
at a scheduled time — the weekly brief and theme briefs alike.

A draft is a folder, data_root()/social/drafts/<id>/:
    meta.json        state, title, schedule, the X text, what was published
    files/<path>     the site files it publishes (paths relative to the repo)
    generated/<path> the same files as the generator wrote them (revert)

Drafts live OUTSIDE the publishing worktree (a private git worktree of
origin/main, data_root()/site-publish — the one tools/weekly_publish.sh has
always used): several drafts can wait at once, and building one never wipes
another. Publishing resets the worktree to origin/main, copies ONE draft's
files in, commits and pushes; GitHub Pages then deploys the page. When the
draft asks for it, the X post goes out once the page shows this version
(tick(), from the timer or the Social screen) — through social_post.announce,
whose record means nothing is ever posted twice.

Kinds and their fixed structure:
  weekly  tools/weekly_brief.py --site: insider clusters, the week ahead,
          the top name per screen (built in the worktree, then copied out)
  theme   theme_create(): The theme (overview — written by the app's AI or
          by hand, always reviewed) · The companies (SEC SIC industry) ·
          Insider buying, last 30 days (Form 4) · Recent SEC filings, last
          30 days · Method and sources. Every fact is from SEC EDGAR.

What may be edited: a page's BODY (between front matter and disclaimer — the
code keeps those and the per-version stamp) and the X text. A weekly draft's
evergreen /insider-cluster-buys/ page follows its edited insider section.

    brief_drafts.py build-weekly [--if-unedited]   (blocking; the timer)
    brief_drafts.py tick                            publish what is due, post what is live
    brief_drafts.py publish ID [--accept-incomplete]
    brief_drafts.py list

Standard library only (plus the SEC helpers): runs under the system Python
with social_app.py, which reads the keyring through python3-gi.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.request
from datetime import date, datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import finterm_paths  # noqa: E402
import weekly_brief_core as wb  # noqa: E402

INSIDER_HEADING = "## Insider buying clusters"
BUILD_TIMEOUT_S = 3 * 3600
X_POST_GIVE_UP_S = 6 * 3600  # a page that never goes live is reported, not retried forever
_RAW = ("{% raw %}", "{% endraw %}")
_STAMP = re.compile(r"^<!-- issue (\S+) stamp (\S+) -->$")
_ID = re.compile(r"^[a-z0-9][a-z0-9-]{0,80}$")

THEME_DISCLAIMER = (
    "This brief is general information compiled from public SEC EDGAR filings (company registrations, Form 4 "
    "insider reports and other filings). The overview was drafted with the help of an AI model and reviewed "
    "before publication. It is not investment advice, a recommendation, or an offer to buy or sell any "
    "security, and it does not consider any reader's circumstances. Inclusion in a theme is not a view on a "
    "company. finterm and its authors are not registered investment advisers or broker-dealers. Data may "
    "contain errors; verify against the filings. finterm receives no compensation from any company listed. "
    "Source: U.S. SEC, EDGAR."
)


# ── paths and locking ────────────────────────────────────────────────────────

def _root():
    return finterm_paths.data_root()


def worktree():
    return _root() / "site-publish"


def drafts_dir():
    d = _root() / "social" / "drafts"
    d.mkdir(parents=True, exist_ok=True)
    return d


def log_path():
    d = _root() / "logs"
    d.mkdir(parents=True, exist_ok=True)
    return d / "brief-drafts.log"


class Lock:
    """One publisher (or draft writer) at a time: the timer, the tick and the
    screen. Held for seconds — never for a build (BuildLock)."""

    NAME = ".lock"

    def __init__(self, wait=False):
        self.wait = wait

    def __enter__(self):
        import fcntl

        self.f = open(drafts_dir() / self.NAME, "a")
        try:
            fcntl.flock(self.f, fcntl.LOCK_EX | (0 if self.wait else fcntl.LOCK_NB))
        except OSError:
            self.f.close()
            raise RuntimeError("another brief is being built or published; try again in a minute") from None
        return self

    def __exit__(self, *exc):
        import fcntl

        fcntl.flock(self.f, fcntl.LOCK_UN)
        self.f.close()


class BuildLock(Lock):
    """One weekly build at a time (minutes to hours). Separate from Lock: a
    build must not hold up a scheduled publish or a save."""

    NAME = ".build.lock"


def _now():
    return datetime.now(timezone.utc)


def _iso(dt):
    return dt.astimezone(timezone.utc).isoformat(timespec="seconds")


def _parse(ts):
    try:
        d = datetime.fromisoformat(str(ts))
        return d if d.tzinfo else d.replace(tzinfo=timezone.utc)
    except (TypeError, ValueError):
        return None


def _alive(pid):
    try:
        os.kill(int(pid), 0)
        return True
    except (OSError, ValueError, TypeError):
        return False


def _git(*args, check=True, cwd=None):
    r = subprocess.run(["git", "-C", str(cwd or worktree()), *args], capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"git {args[0]}: {(r.stderr or r.stdout).strip()[-300:]}")
    return r.stdout.strip()


# ── a draft on disk ──────────────────────────────────────────────────────────

def _dir(did):
    if not _ID.match(did or ""):
        raise ValueError(f"bad draft id: {did!r}")
    return drafts_dir() / did


def load(did):
    try:
        return json.loads((_dir(did) / "meta.json").read_text(encoding="utf-8"))
    except (FileNotFoundError, ValueError):
        return None


def store(m):
    d = _dir(m["id"])
    d.mkdir(parents=True, exist_ok=True)
    tmp = d / "meta.tmp"
    tmp.write_text(json.dumps(m, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    os.replace(tmp, d / "meta.json")


def _file(m, rel, which="files"):
    return _dir(m["id"]) / which / rel


def _effective_state(m):
    if m.get("state") == "building" and not _alive(m.get("pid")):
        return "failed"
    return m.get("state") or "failed"


def summary(m):
    st = _effective_state(m)
    return {"id": m["id"], "kind": m.get("kind", ""), "title": m.get("title", ""), "key": m.get("key", ""),
            "state": st, "url": m.get("url", ""), "created": m.get("created", ""),
            "built_at": m.get("built_at", ""), "saved_at": m.get("saved_at", ""), "edited": bool(m.get("edited")),
            "complete": m.get("complete", True), "notes": m.get("notes", []),
            "schedule_at": m.get("schedule_at", ""), "accept_incomplete": bool(m.get("accept_incomplete")),
            "post_x": bool(m.get("post_x")), "x_ref": m.get("x_ref", ""), "x_error": m.get("x_error", ""),
            "published_at": m.get("published_at", ""), "commit": m.get("commit", ""),
            "error": m.get("error", "") or ("the build stopped without finishing — see the log"
                                            if st == "failed" and m.get("state") == "building" else "")}


def list_drafts():
    out = []
    for d in drafts_dir().iterdir():
        if d.is_dir() and _ID.match(d.name):
            m = load(d.name)
            if m:
                out.append(summary(m))
    out.sort(key=lambda s: s.get("created", ""), reverse=True)
    return {"drafts": out, "worktree": str(worktree())}


def get(did):
    m = load(did)
    if not m:
        return {"error": "That draft no longer exists."}
    out = summary(m)
    out["x_text"] = m.get("x_text", "")
    out["x_generated"] = m.get("x_generated", "")
    if out["state"] in ("building", "failed"):
        try:
            out["log"] = "\n".join(log_path().read_text(encoding="utf-8", errors="replace").splitlines()[-14:])
        except FileNotFoundError:
            out["log"] = ""
    if m.get("main") and _file(m, m["main"]).exists():
        try:
            out["body"] = "\n".join(split_page(_file(m, m["main"]).read_text(encoding="utf-8"))[2])
            gen = _file(m, m["main"], "generated")
            if gen.exists():
                out["generated_body"] = "\n".join(split_page(gen.read_text(encoding="utf-8"))[2])
        except (OSError, ValueError) as e:
            out["error"] = f"the draft's page is unreadable ({e})"
    return out


def delete(did):
    m = load(did)
    if not m:
        return {"ok": True}
    if _effective_state(m) == "building":
        return {"error": "That draft is still being built."}
    shutil.rmtree(_dir(did), ignore_errors=True)
    return {"ok": True}


# ── the page format (weekly_brief_core.page) ─────────────────────────────────

def split_page(text):
    """(front_matter_lines, stamp_line, body_lines, disclaimer_line). The body
    is what a reader may correct; raw-block markers are not part of it."""
    lines = text.split("\n")
    if not lines or lines[0] != "---" or "---" not in lines[1:]:
        raise ValueError("no front matter")
    end = lines.index("---", 1)
    front, rest = lines[:end + 1], lines[end + 1:]
    while rest and not rest[-1].strip():
        rest.pop()
    if not rest or not (rest[-1].startswith("_") and rest[-1].endswith("_")):
        raise ValueError("no disclaimer line")
    disclaimer = rest.pop()
    stamp, body = "", []
    for ln in rest:
        if _STAMP.match(ln.strip()):
            stamp = ln.strip()
        elif ln.strip() not in _RAW:
            body.append(ln)
    while body and not body[0].strip():
        body.pop(0)
    while body and not body[-1].strip():
        body.pop()
    return front, stamp, body, disclaimer


def join_page(front, stamp, body, disclaimer, lead=()):
    """The page: front matter, lead lines (the evergreen page's "Updated …"),
    the stamp, the body in ONE raw block — vendor and reader text is data,
    never Liquid — and the disclaimer."""
    out = list(front) + [""] + list(lead) + ([stamp, ""] if stamp else [])
    out += [_RAW[0]] + list(body) + ["", _RAW[1], "", disclaimer, ""]
    return "\n".join(out)


def section(body_lines, heading):
    try:
        i = next(k for k, ln in enumerate(body_lines) if ln.strip() == heading)
    except StopIteration:
        return None
    j = next((k for k in range(i + 1, len(body_lines)) if body_lines[k].startswith("## ")), len(body_lines))
    sec = body_lines[i:j]
    while sec and not sec[-1].strip():
        sec.pop()
    return sec


def check_body(m, text):
    if not text.strip():
        return "The page body is empty."
    if "endraw" in text:
        return "The body may not contain \"endraw\" (it would end the page's raw block)."
    opens = sum(ln.strip() == wb.PRIVATE_OPEN for ln in text.split("\n"))
    closes = sum(ln.strip() == wb.PRIVATE_CLOSE for ln in text.split("\n"))
    if opens != closes:
        return "Each <!-- private --> needs its <!-- /private --> (everything after an unclosed one is withheld)."
    if m.get("kind") == "weekly" and not any(ln.strip() == INSIDER_HEADING for ln in text.split("\n")):
        return f"Keep the \"{INSIDER_HEADING}\" heading: the always-current insider page is cut from it."
    return None


def _stamp_now():
    return _now().strftime("%Y%m%dT%H%M%SZ")


# ── weekly ───────────────────────────────────────────────────────────────────

WEEKLY_FILES = ("site/insider-cluster-buys.md", "site/_data/announce.json", "site/issue.json")


def weekly_id(today=None):
    return f"weekly-{wb.issue_key(today or date.today()).lower()}"


def start_weekly_build(force=False):
    """Start the build in the background (minutes); the screen polls get()."""
    did = weekly_id()
    m = load(did)
    if m and _effective_state(m) == "building":
        return {"error": "This week's brief is already being built."}
    if m and m.get("state") == "published":
        return {"error": f"{m.get('key')} is already published; it can't be rebuilt."}
    if m and m.get("edited") and not force:
        return {"error": "This week's draft has corrections; rebuilding replaces them.", "needs_force": True}
    with open(log_path(), "a", encoding="utf-8") as log:
        log.write(f"\n=== {_iso(_now())} weekly build (Social screen)\n")
        log.flush()
        p = subprocess.Popen([sys.executable, os.path.abspath(__file__), "build-weekly"], stdout=log,
                             stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True,
                             cwd=os.path.dirname(os.path.abspath(__file__)))
    base = m or {"id": did, "kind": "weekly", "created": _iso(_now())}
    base.update({"state": "building", "pid": p.pid, "error": "", "title": base.get("title") or
                 f"Weekly brief {wb.issue_key(date.today())}"})
    store(base)
    return get(did)


def repo_root():
    """The source checkout these scripts run from (the app runs them from
    <repo>/app/build/…/scripts, inside it). The generator is taken from
    HERE, so it is always the same version as this module."""
    top = subprocess.run(["git", "-C", os.path.dirname(os.path.abspath(__file__)), "rev-parse", "--show-toplevel"],
                         capture_output=True, text=True)
    if top.returncode != 0:
        raise RuntimeError("publishing needs finterm's source checkout (a git clone with the site/ folder)")
    return Path(top.stdout.strip())


def _ensure_worktree():
    wt = worktree()
    if (wt / ".git").exists():
        return
    repo = str(repo_root())
    _git("fetch", "--quiet", "origin", "main", cwd=repo)
    _git("worktree", "add", "--detach", str(wt), "origin/main", cwd=repo)


def _reset_worktree():
    _ensure_worktree()
    _git("fetch", "--quiet", "origin", "main")
    _git("rebase", "--abort", check=False)
    _git("checkout", "--quiet", "--detach")
    _git("reset", "--quiet", "--hard", "origin/main")
    _git("clean", "-fdq", "site")


def build_weekly(if_unedited=False):
    """Build this week's draft (blocking). The generator writes into a
    scratch folder of the draft's own — never the publishing worktree, so a
    publish can run meanwhile — and only a successful build replaces the
    draft's files: a failed rebuild leaves the previous version in place."""
    did = weekly_id()
    with BuildLock(wait=True):
        m = load(did) or {"id": did, "kind": "weekly", "created": _iso(_now())}
        if m.get("state") == "published":
            print(f"{did} is already published")
            return 0
        # The timer must not replace text the owner has looked at: corrected,
        # or scheduled (scheduling is approval of THIS text).
        if if_unedited and m.get("state") == "ready" and (m.get("edited") or m.get("schedule_at")):
            print(f"{did} is corrected or scheduled; not rebuilt (rebuild it from the Social screen)")
            return 0
        had = m.get("state") == "ready" and bool(m.get("main"))
        m.update({"state": "building", "pid": os.getpid(), "error": "",
                  "title": f"Weekly brief {wb.issue_key(date.today())}"})
        with Lock(wait=True):
            store(m)
        scratch = _dir(did) / "build"
        shutil.rmtree(scratch, ignore_errors=True)
        (scratch / "site").mkdir(parents=True)
        try:
            r = subprocess.run([sys.executable, str(repo_root() / "tools/weekly_brief.py"), "--site",
                                str(scratch / "site"), "--allow-incomplete", "--draft"],
                               capture_output=True, text=True, timeout=BUILD_TIMEOUT_S)
            sys.stdout.write(r.stdout + r.stderr)
            if r.returncode != 0:
                tail = (r.stderr or r.stdout).strip().splitlines()[-1:] or [f"exit {r.returncode}"]
                raise RuntimeError(f"the brief failed: {tail[0][:300]}")
            post = Path(r.stdout.strip().splitlines()[-1]).resolve()
            main = str(post.relative_to(scratch.resolve()))
            issue = json.loads((scratch / "site/issue.json").read_text(encoding="utf-8"))
            with Lock(wait=True):
                m = load(did) or m
                for which in ("files", "generated"):
                    shutil.rmtree(_dir(did) / which, ignore_errors=True)
                    for rel in (main,) + WEEKLY_FILES:
                        dst = _file(m, rel, which)
                        dst.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(scratch / rel, dst)
                # New text has not been reviewed: any schedule is cleared.
                m.update({"state": "ready", "key": issue["issue"], "url": issue.get("url", ""), "main": main,
                          "files": [main, *WEEKLY_FILES], "built_at": _iso(_now()), "edited": False,
                          "complete": bool(issue.get("complete", True)), "notes": issue.get("notes", []),
                          "x_text": issue.get("x", ""), "x_generated": issue.get("x", ""), "pid": None,
                          "schedule_at": "", "accept_incomplete": False, "error": ""})
                store(m)
            print(f"{did} ready ({main})")
            return 0
        except Exception as e:  # noqa: BLE001 — recorded for the screen
            with Lock(wait=True):
                m = load(did) or m
                if had:
                    m.update({"state": "ready", "pid": None,
                              "error": f"rebuild failed ({str(e)[:300]}); this is the previous build"})
                else:
                    m.update({"state": "failed", "error": str(e)[:500], "pid": None})
                store(m)
            print(f"error: {e}", file=sys.stderr)
            return 1
        finally:
            shutil.rmtree(scratch, ignore_errors=True)


# ── theme briefs ─────────────────────────────────────────────────────────────

def slugify(text):
    s = re.sub(r"[^a-z0-9]+", "-", (text or "").lower()).strip("-")
    return s[:48].strip("-") or "theme"


def parse_tickers(text):
    out = []
    for t in re.split(r"[\s,;]+", str(text or "").upper()):
        t = t.strip().lstrip("$")
        if t and re.fullmatch(r"[A-Z][A-Z0-9.\-]{0,9}", t) and t not in out:
            out.append(t)
    return out[:25]


def _sec_get(url):
    from finterm_identity import user_agent

    req = urllib.request.Request(url, headers={"User-Agent": user_agent(), "Accept-Encoding": "identity"})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.loads(r.read().decode("utf-8"))


FILING_FORMS = ("8-K", "10-K", "10-Q", "S-1", "S-1/A", "DEF 14A", "6-K", "20-F", "424B4", "SC 13D", "SC 13G")


def theme_facts(tickers, days=30, today=None):
    """Public facts for a theme's companies: EDGAR name + SIC industry,
    filings in the window, and open-market insider buying from the local
    Form 4 store. Never a price or a vendor figure."""
    tickers = parse_tickers(tickers if isinstance(tickers, str) else " ".join(tickers or []))
    if not tickers:
        return {"error": "Enter at least one ticker."}
    if not os.environ.get(wb.CONTACT_KEY):  # the app injects it; a shell or the timer reads the keyring
        c = wb.contact_from_keyring()
        if c:
            os.environ[wb.CONTACT_KEY] = c
    today = today or date.today()
    cutoff = today - timedelta(days=days)
    try:
        directory = _sec_get("https://www.sec.gov/files/company_tickers.json")
    except Exception as e:  # noqa: BLE001
        return {"error": f"SEC refused the company list ({type(e).__name__}); is the contact email set in "
                         "Settings › Credentials?"}
    by_ticker = {str(v.get("ticker", "")).upper(): v for v in directory.values()}
    companies, unknown = [], []
    for t in tickers:
        row = by_ticker.get(t) or by_ticker.get(t.replace(".", "-"))
        if not row:
            unknown.append(t)
            continue
        cik = int(row["cik_str"])
        c = {"ticker": t, "cik": cik, "name": row.get("title", ""), "sic": "", "filings": []}
        try:
            sub = _sec_get(f"https://data.sec.gov/submissions/CIK{cik:010d}.json")
            c["name"] = sub.get("name") or c["name"]
            c["sic"] = sub.get("sicDescription") or ""
            rec = (sub.get("filings") or {}).get("recent") or {}
            for form, fd, acc, doc in zip(rec.get("form", []), rec.get("filingDate", []),
                                          rec.get("accessionNumber", []), rec.get("primaryDocument", [])):
                if form in FILING_FORMS and fd >= cutoff.isoformat():
                    c["filings"].append({"form": form, "date": fd, "url": (
                        f"https://www.sec.gov/Archives/edgar/data/{cik}/{acc.replace('-', '')}/{doc}")})
            c["filings"] = c["filings"][:6]
        except Exception as e:  # noqa: BLE001 — the company stays; its filings are said to be missing
            c["filings_error"] = type(e).__name__
        companies.append(c)
        time.sleep(0.15)  # SEC: well under 10 requests a second
    insiders, insider_note = [], ""
    try:
        import sec_form4_market as f4

        res = f4.recent(days=days, min_insiders=1, min_value=0.0, symbols=[c["ticker"] for c in companies])
        insiders = res.get("rows", [])
        read, expected, missing = f4.coverage(days)
        if missing:
            insider_note = f"Form 4 filings from {read} of {expected} business days were read."
    except Exception as e:  # noqa: BLE001
        insider_note = f"The local Form 4 store could not be read ({type(e).__name__})."
    lines = [f"{c['ticker']}: {c['name']} — SEC industry: {c['sic'] or 'n/a'}" for c in companies]
    return {"tickers": tickers, "companies": companies, "unknown": unknown, "insiders": insiders,
            "insider_note": insider_note, "days": days, "facts_text": "\n".join(lines)}


def clean_overview(text):
    """Model (or pasted) prose made safe for the page: no Liquid tags (they
    would end the raw block) and no private markers (strip_private would
    withhold everything after one)."""
    out = []
    for ln in str(text or "").replace("\r\n", "\n").split("\n"):
        if ln.strip() in (wb.PRIVATE_OPEN, wb.PRIVATE_CLOSE):
            continue
        ln = re.sub(r"\{%.*?%\}", "", ln)  # a whole tag goes; a stray half is defused
        out.append(ln.replace("{%", "{ ").replace("%}", " }").replace("endraw", "end-raw"))
    return "\n".join(out).strip()


def theme_body(title, overview, facts):
    """The fixed structure. Only the overview is prose; the rest is EDGAR."""
    days = facts.get("days", 30)
    overview = clean_overview(overview)
    out = ["## The theme", "", (overview or "").strip() or "_Write the overview here._", ""]
    out += ["## The companies", "", "| Ticker | Company | Industry (SEC) |", "|---|---|---|"]
    for c in facts.get("companies", []):
        out.append(f"| {wb.md_escape(c['ticker'])} | {wb.md_escape(c['name'])} | {wb.md_escape(c['sic']) or '—'} |")
    if facts.get("unknown"):
        out += ["", f"_Not found in SEC's company list: {', '.join(wb.md_escape(t) for t in facts['unknown'])}._"]
    out += ["", f"## Insider buying, last {days} days", ""]
    rows = facts.get("insiders") or []
    if rows:
        out += ["Open-market purchases (Form 4, code P) by officers and directors; 10% owners and 10b5-1 plan "
                "trades excluded.", "", "| Ticker | Insiders | Reported | Trade dates |", "|---|---:|---:|---|"]
        for r in rows:
            span = r["first_trade"] if r["first_trade"] == r["last_trade"] else f"{r['first_trade']} – {r['last_trade']}"
            out.append(f"| {wb.md_escape(r['symbol'])} | {r['insiders']} | {wb.money(r['value'])} | {span} |")
    else:
        out.append("_No open-market insider purchases were reported for these companies in the window._")
    if facts.get("insider_note"):
        out += ["", f"> {facts['insider_note']}"]
    out += ["", f"## Recent SEC filings, last {days} days", ""]
    any_filing = False
    for c in facts.get("companies", []):
        for f in c.get("filings", []):
            any_filing = True
            out.append(f"- **{wb.md_escape(c['ticker'])}** [{wb.md_escape(f['form'])}]({f['url']}), {f['date']}")
        if c.get("filings_error"):
            out.append(f"- **{wb.md_escape(c['ticker'])}** filings could not be read this time")
    if not any_filing:
        out.append("_No 8-K, 10-K, 10-Q, proxy or registration filings in the window._")
    out += ["", "## Method and sources", "",
            "Companies were chosen by the editor for the theme; inclusion is not a view on any of them. Company "
            "names and industries are from SEC registrations, insider purchases from Form 4 filings, filings "
            "from EDGAR. No prices, ratings or forecasts are given.", ""]
    return out


def theme_create(title, tickers, overview, facts):
    title = " ".join(str(title or "").split())[:120]
    if not title:
        return {"error": "Give the theme a title."}
    if not facts or not facts.get("companies"):
        return {"error": "None of those tickers is in SEC's company list."}
    today = date.today()
    slug = slugify(title)
    did = f"theme-{slug}-{today:%Y%m%d}"
    n = 2
    while load(did):
        did, n = f"theme-{slug}-{today:%Y%m%d}-{n}", n + 1
    url_slug = did[len("theme-"):]
    rel = f"site/_posts/{today.isoformat()}-{did}.md"
    url = f"{wb.SITE_URL}/themes/{url_slug}/"
    names = [c["ticker"] for c in facts["companies"]]
    front = {"title": title, "description": f"A theme brief: {', '.join(names[:8])} — companies, insider buying and "
                                            f"recent SEC filings.", "date": today, "updated": today,
             "permalink": f"/themes/{url_slug}/"}
    text = wb.page(front, [f"<!-- issue {did} stamp {_stamp_now()} -->", "", _RAW[0]] +
                   theme_body(title, overview, facts) + [_RAW[1], ""], THEME_DISCLAIMER)
    m = {"id": did, "kind": "theme", "title": title, "key": did, "url": url, "main": rel, "files": [rel],
         "created": _iso(_now()), "built_at": _iso(_now()), "state": "ready", "edited": False, "complete": True,
         "notes": [], "tickers": names}
    x = f"Theme brief: {title}. {len(names)} companies — insider buying and recent SEC filings. {url}"
    m.update({"x_text": x, "x_generated": x})
    for which in ("files", "generated"):
        p = _file(m, rel, which)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding="utf-8")
    store(m)
    return get(did)


# ── edit, schedule, publish ──────────────────────────────────────────────────

def _editable(m):
    if not m:
        return "That draft no longer exists."
    st = _effective_state(m)
    if st == "published":
        return "That brief is already published."
    if st != "ready":
        return "That draft is not ready."
    return None


def save(did, body=None, x_text=None, post_x=None):
    m = load(did)
    why = _editable(m)
    if why:
        return {"error": why}
    # Everything is checked before anything is written: a refused save must
    # leave the files and the "edited" flag as they were.
    if body is not None:
        body = body.replace("\r\n", "\n").strip("\n")
        why = check_body(m, body)
        if why:
            return {"error": why}
    if x_text is not None:
        x_text = x_text.strip()
        if m.get("url") and m["url"] not in x_text:
            return {"error": "The X text must keep the link to the page."}
    with Lock():
        m = load(did)
        why = _editable(m)
        if why:
            return {"error": why}
        if body is not None:
            lines = body.split("\n")
            stamp = f"<!-- issue {m['key']} stamp {_stamp_now()} -->"
            main = _file(m, m["main"])
            front, _, _, disc = split_page(main.read_text(encoding="utf-8"))
            main.write_text(join_page(front, stamp, lines, disc), encoding="utf-8")
            if m["kind"] == "weekly":
                ev = _file(m, "site/insider-cluster-buys.md")
                e_front, _, e_body, e_disc = split_page(ev.read_text(encoding="utf-8"))
                cut = next((k for k, ln in enumerate(e_body) if ln.strip() == INSIDER_HEADING), 0)
                lead = [ln for ln in e_body[:cut] if ln.strip()]
                ev.write_text(join_page(e_front, stamp, section(lines, INSIDER_HEADING), e_disc,
                                        lead=lead + [""] if lead else ()), encoding="utf-8")
                for rel in ("site/issue.json", "site/_data/announce.json"):
                    p = _file(m, rel)
                    d = json.loads(p.read_text(encoding="utf-8"))
                    d["stamp"] = stamp.split(" stamp ")[1].rstrip(" ->")
                    p.write_text(json.dumps(d, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            gen = _file(m, m["main"], "generated")
            m["edited"] = not gen.exists() or "\n".join(split_page(gen.read_text(encoding="utf-8"))[2]) != body
        if x_text is not None:
            m["x_text"] = x_text
            if m["kind"] == "weekly":
                for rel in ("site/issue.json", "site/_data/announce.json"):
                    p = _file(m, rel)
                    d = json.loads(p.read_text(encoding="utf-8"))
                    d["x"] = x_text
                    p.write_text(json.dumps(d, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        if post_x is not None:
            m["post_x"] = bool(post_x)
        m["saved_at"] = _iso(_now())
        store(m)
    return get(did)


def revert(did):
    m = load(did)
    why = _editable(m)
    if why:
        return {"error": why}
    gen = _file(m, m["main"], "generated")
    if not gen.exists():
        return {"error": "The generated copy is missing; rebuild the draft."}
    return save(did, body="\n".join(split_page(gen.read_text(encoding="utf-8"))[2]),
                x_text=m.get("x_generated") or None)


def schedule(did, at, accept_incomplete=False):
    """Publish at @at (ISO; empty = unschedule). The timer's tick does it."""
    m = load(did)
    why = _editable(m)
    if why:
        return {"error": why}
    if not at:
        m["schedule_at"] = ""
        store(m)
        return get(did)
    when = _parse(at)
    if not when:
        return {"error": "That is not a date and time."}
    if when < _now() - timedelta(minutes=1):
        return {"error": "That time has passed; use Publish now."}
    if not m.get("complete", True) and not accept_incomplete:
        return {"error": "Some days of filings could not be read; confirm scheduling an incomplete week.",
                "needs_confirm": True}
    m.update({"schedule_at": _iso(when), "accept_incomplete": bool(accept_incomplete)})
    store(m)
    return get(did)


def publish(did, accept_incomplete=False):
    """Commit and push one draft's files. GitHub Pages deploys in a minute
    or two; tick() posts to X once the page shows this version."""
    m = load(did)
    why = _editable(m)
    if why:
        return {"error": why}
    if not m.get("complete", True) and not accept_incomplete:
        return {"error": "Some days of filings could not be read; confirm publishing an incomplete week.",
                "needs_confirm": True}
    with Lock(wait=True):
        # Again under the lock: another publisher (the timer's tick, the
        # screen's) may have published it while this one waited.
        m = load(did)
        why = _editable(m)
        if why:
            return {"error": why, "already": bool(m and m.get("state") == "published")}
        _reset_worktree()
        wt = worktree()
        for rel in m["files"]:
            dst = wt / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            text = _file(m, rel).read_text(encoding="utf-8")
            # Private blocks (the owner's Nasdaq IPO section) never leave
            # this machine; a weekly rerun keeps its file name, so it
            # replaces the week's issue rather than adding one.
            public = wb.strip_private(text)
            if wb.PRIVATE_OPEN in public or "Nasdaq calendar — only you see this" in public:
                raise RuntimeError(f"{rel}: a private section survived stripping; not published")
            dst.write_text(public, encoding="utf-8")
        _git("add", "-A", *m["files"])
        if subprocess.run(["git", "-C", str(wt), "diff", "--cached", "--quiet"]).returncode == 0:
            m.update({"state": "published", "published_at": _iso(_now()), "schedule_at": "",
                      "commit": _git("rev-parse", "HEAD")})
            store(m)
            return get(did)  # identical to what is live: nothing to push
        _git("commit", "--quiet", "-m", f"brief: {m['key']}")
        err = ""
        for _ in range(3):
            pulled = subprocess.run(["git", "-C", str(wt), "pull", "--quiet", "--rebase", "origin", "main"],
                                    capture_output=True, text=True)
            if pulled.returncode == 0:
                pushed = subprocess.run(["git", "-C", str(wt), "push", "--quiet", "origin", "HEAD:main"],
                                        capture_output=True, text=True)
                if pushed.returncode == 0:
                    m.update({"state": "published", "published_at": _iso(_now()), "schedule_at": "",
                              "commit": _git("rev-parse", "HEAD"), "error": ""})
                    store(m)
                    _reset_worktree()
                    return get(did)
                err = pushed.stderr
            else:
                _git("rebase", "--abort", check=False)
                err = pulled.stderr
            time.sleep(5)
        _reset_worktree()
        m["error"] = f"could not push: {err.strip()[-300:]}"
        store(m)
        return {"error": f"Could not push the brief: {err.strip()[-300:]}"}


def page_live(m):
    """The page shows THIS version (its stamp)."""
    try:
        stamp = split_page(_file(m, m["main"]).read_text(encoding="utf-8"))[1]
        req = urllib.request.Request(m["url"], headers={"User-Agent": "finterm-brief/1.0"})
        with urllib.request.urlopen(req, timeout=10) as r:
            body = r.read(600_000).decode("utf-8", "replace")
        return bool(stamp) and stamp in body
    except Exception:  # noqa: BLE001
        return False


def tick(log=print):
    """Publish every draft that is due; post to X every published draft that
    asked for it once its page is live. Safe to run from several places."""
    done = []
    for s in list_drafts()["drafts"]:
        m = load(s["id"])
        if not m:
            continue
        due = _parse(m.get("schedule_at"))
        if m.get("state") == "ready" and due and due <= _now():
            try:
                r = publish(m["id"], accept_incomplete=bool(m.get("accept_incomplete")))
            except Exception as e:  # noqa: BLE001 — one draft failing must not stop the others
                r = {"error": f"{type(e).__name__}: {e}"}
            if r.get("error") and not r.get("already"):
                # Said on the draft; the schedule stays, so the next tick retries.
                with Lock(wait=True):
                    m = load(m["id"]) or m
                    m["error"] = f"scheduled publish failed: {r['error'][:300]} — retrying"
                    store(m)
            log(f"{m['id']}: {'published' if not r.get('error') else 'NOT published: ' + r['error']}")
            done.append(m["id"])
            m = load(m["id"]) or m
        if m.get("state") == "published" and m.get("post_x") and not m.get("x_ref"):
            pub = _parse(m.get("published_at"))
            if pub and (_now() - pub).total_seconds() > X_POST_GIVE_UP_S:
                if not m.get("x_error"):
                    m["x_error"] = "the page never showed this version; not posted"
                    store(m)
                continue
            if not page_live(m):
                continue
            import social_post as sp

            if "x" not in sp.ENABLED_NETWORKS:
                continue
            try:
                results, failed = sp.announce(m["key"], {"x": m.get("x_text", "")}, only={"x"}, log=log,
                                              generated=(m.get("x_text") == m.get("x_generated")))
                status = results.get("x", "")
            except Exception as e:  # noqa: BLE001
                status = f"failed: {type(e).__name__}: {e}"
            m = load(m["id"]) or m
            with Lock(wait=True):
                m = load(m["id"]) or m
                if status.startswith(("posted", "already")):
                    m["x_ref"] = sp.posted(m["key"]).get("x", "") or status
                    m["x_error"] = ""
                else:
                    m["x_error"] = status or "not posted"
                store(m)
            done.append(m["id"])
    return {"acted": done}


def main(argv):
    cmd = argv[1] if len(argv) > 1 else ""
    if cmd == "build-weekly":
        return build_weekly(if_unedited="--if-unedited" in argv)
    if cmd == "tick":
        try:
            print(json.dumps(tick()))
        except RuntimeError as e:  # another run holds the lock: the next tick does it
            print(f"skipped: {e}")
        return 0
    if cmd == "publish" and len(argv) > 2:
        m = load(argv[2])
        if m and m.get("state") == "published":
            print(f"{argv[2]} is already published")
            return 0  # a rerun of --auto is safe
        out = publish(argv[2], accept_incomplete="--accept-incomplete" in argv)
        print(json.dumps(out, indent=2, ensure_ascii=False))
        return 1 if out.get("error") else 0
    if cmd == "list":
        print(json.dumps(list_drafts(), indent=2, ensure_ascii=False))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
