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
  weekly  tools/weekly_brief.py --site: one page — at a glance, markets and economy,
          the week ahead, stocks (insider buying, fund moves, screen leaders)
          (built in a scratch folder, then copied out)
  theme   theme_create(): The theme (overview — written by the app's AI or
          by hand, always reviewed) · The companies (SEC SIC industry) ·
          Insider buying, last 30 days (Form 4) · Recent SEC filings, last
          30 days · Method and sources. Every fact is from SEC EDGAR.

What may be edited: a page's BODY (between front matter and disclaimer — the
code keeps those and the per-version stamp) and the X text. A weekly draft's
evergreen /insider-cluster-buys/ page stays the generated full list.

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


# ── the owner's brief settings ───────────────────────────────────────────────
#
# What the weekly brief covers, how much of it, where it is posted and whether
# the Sunday run publishes on its own. Read by the build (→ weekly_brief.py
# --topics/--detail), by tick() (where to post) and by tools/weekly_publish.sh
# (auto-mode). Set from finterm › NEWS › What's On › WEEKLY BRIEF.

TOPICS = ("summary", "macro", "earnings", "ipo", "insider", "funds", "screens", "mine")
NEW_TOPICS = (("funds", 2), ("earnings", 4), ("mine", 4))  # the settings version each came with
RENAMED_TOPICS = {"ipo_private": "ipo"}  # the IPO part, when it was the owner's only
DETAILS = ("full", "standard", "short")
AUTO_MODES = ("draft", "publish")
PUBLIC_SECTIONS = ("macro", "earnings", "ipo", "insider", "funds", "screens")  # what a published issue can hold
DEFAULT_SETTINGS = {"topics": list(TOPICS), "detail": "standard", "platforms": ["x"], "auto": "draft",
                    "v": 4}  # 2: the one-page brief (fund moves); 3: IPOs are public; 4: earnings, your holdings


def settings_path():
    return _root() / "social" / "brief_settings.json"


def settings_get():
    s = dict(DEFAULT_SETTINGS)
    try:
        saved = json.loads(settings_path().read_text(encoding="utf-8"))
        # Saved before fund moves existed: they are on, as a new section is by
        # default; what was chosen of the rest stands.
        if isinstance(saved.get("topics"), list):
            saved["topics"] = [*saved["topics"], *(t for t, since in NEW_TOPICS if saved.get("v", 1) < since)]
        s.update(saved, v=DEFAULT_SETTINGS["v"])
    except (OSError, ValueError, AttributeError, TypeError):
        pass
    import social_post as sp
    chosen = [RENAMED_TOPICS.get(t, t) for t in s.get("topics", [])]
    s["topics"] = [t for t in TOPICS if t in chosen]
    if not [t for t in s["topics"] if t in PUBLIC_SECTIONS]:  # a hand-edited file: never an empty issue
        s["topics"] = list(DEFAULT_SETTINGS["topics"])
    s["detail"] = s.get("detail") if s.get("detail") in DETAILS else "standard"
    s["platforms"] = [n for n in s.get("platforms", []) if n in sp.ENABLED_NETWORKS]
    s["auto"] = s.get("auto") if s.get("auto") in AUTO_MODES else "draft"
    return s


def settings_set(new):
    """Validate and store; returns the stored settings or {"error"}."""
    import social_post as sp
    cur = settings_get()
    if "topics" in new:
        new = dict(new, topics=[RENAMED_TOPICS.get(t, t) for t in new["topics"]])
        bad = [t for t in new["topics"] if t not in TOPICS]
        if bad:
            return {"error": f"unknown topic(s): {', '.join(bad)}"}
        if not [t for t in new["topics"] if t in PUBLIC_SECTIONS]:
            return {"error": "Pick at least one section: a brief with nothing in it is not published."}
        # A screen from before fund moves (an app still running, or not yet
        # updated) has no box for them and sends none: its list is not a choice.
        # The same for IPOs, whose box it looks up under their old name.
        for t, since in (*NEW_TOPICS, ("ipo", 3)):
            if int(new.get("v") or 1) < since and t in cur["topics"] and t not in new["topics"]:
                new["topics"].append(t)
        cur["topics"] = [t for t in TOPICS if t in new["topics"]]
    if "detail" in new:
        if new["detail"] not in DETAILS:
            return {"error": f"unknown detail level: {new['detail']}"}
        cur["detail"] = new["detail"]
    if "platforms" in new:
        bad = [n for n in new["platforms"] if n not in sp.ENABLED_NETWORKS]
        if bad:
            return {"error": f"not a platform this build posts to: {', '.join(bad)}"}
        cur["platforms"] = [n for n in sp.ENABLED_NETWORKS if n in new["platforms"]]
    if "auto" in new:
        if new["auto"] not in AUTO_MODES:
            return {"error": f"unknown mode: {new['auto']}"}
        cur["auto"] = new["auto"]
    settings_path().parent.mkdir(parents=True, exist_ok=True)
    tmp = settings_path().with_suffix(".tmp")
    tmp.write_text(json.dumps(cur, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, settings_path())
    return cur


def post_to(m):
    """The platforms a draft posts to once live (older drafts: post_x)."""
    if isinstance(m.get("post_to"), list):
        return list(m["post_to"])
    return ["x"] if m.get("post_x") else []


def post_errors(m):
    """Each platform's last posting failure (older drafts kept X's in x_error)."""
    errs = dict(m.get("post_errors") or {})
    if m.get("x_error") and "x" not in errs and not m.get("x_ref"):
        errs["x"] = m["x_error"]
    return errs


def post_to_chosen(m):
    """The owner chose this draft's platforms (Review), so a rebuild keeps
    them; otherwise every build takes the settings'. A draft from before
    post_to carried the owner's post_x choice."""
    return bool(m.get("post_to_chosen")) or ("post_to" not in m and "post_x" in m)


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
        m = json.loads((_dir(did) / "meta.json").read_text(encoding="utf-8"))
    except (FileNotFoundError, ValueError):
        return None
    # A new edition whose build died: its files are still the published ones.
    if m.get("state") == "building" and m.get("prev_published") and not _alive(m.get("pid")):
        m.update({"state": "published", "pid": None})
    return m


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
            "post_x": "x" in post_to(m), "x_ref": m.get("x_ref", ""), "x_error": m.get("x_error", ""),
            "post_to": post_to(m), "post_errors": post_errors(m),
            # Ticked but not connected: skipped, never retried (connect it, then Post).
            "skipped": _not_connected(post_to(m)),
            # What is posted where — the shared record (timer, app, by hand).
            "posted": _posted_record(m),
            "topics": m.get("topics", []), "detail": m.get("detail", ""),
            "progress_path": str(progress_path(m["id"])) if st == "building" else "",
            "published_at": m.get("published_at", ""), "commit": m.get("commit", ""),
            # A note stored on the draft (a failed rebuild, a scheduled publish
            # being retried) — never the top-level "error", which means THIS
            # call failed and would make a successful save read as refused.
            "note": m.get("error", "") or ("the build stopped without finishing — see the log"
                                           if st == "failed" and m.get("state") == "building" else "")}


def _not_connected(networks):
    try:
        import social_post as sp
        return [n for n in networks if sp.is_configured(n) is False]
    except Exception:  # noqa: BLE001 — unknown is not "not connected"
        return []


def _posted_record(m):
    if not m.get("key"):
        return {}
    try:
        import social_post as sp
        return sp.posted(m["key"])
    except Exception:  # noqa: BLE001 — a record that can't be read shows as nothing posted
        return {}


def progress_path(did):
    """Where a build keeps its progress (tools/weekly_brief.py Progress)."""
    return _dir(did) / "progress.json"


def private_path(did):
    """The owner's holdings this week (weekly_brief_core.mine_part): beside the
    draft's files, never one of them, so nothing that publishes can read it."""
    return _dir(did) / "private.md"


def list_drafts():
    out = []
    for d in drafts_dir().iterdir():
        if d.is_dir() and _ID.match(d.name):
            m = load(d.name)
            if m:
                out.append(summary(m))
    out.sort(key=lambda s: s.get("created", ""), reverse=True)
    return {"drafts": out, "worktree": str(worktree()), "health": health()}


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
    try:
        out["private"] = private_path(did).read_text(encoding="utf-8")
    except OSError:
        out["private"] = ""
    if m.get("main") and _file(m, m["main"]).exists():
        try:
            out["body"] = "\n".join(split_page(_file(m, m["main"]).read_text(encoding="utf-8"))[2])
            gen = _file(m, m["main"], "generated")
            if gen.exists():
                out["generated_body"] = "\n".join(split_page(gen.read_text(encoding="utf-8"))[2])
        except (OSError, ValueError) as e:
            out["note"] = f"the draft's page is unreadable ({e})"
    return out


def delete(did):
    m = load(did)
    if not m:
        return {"ok": True}
    if _effective_state(m) == "building":
        return {"error": "That draft is still being built."}
    if m.get("published_at") and m.get("state") != "published":
        return {"error": "A new edition of a published brief is not deleted; publish it or generate it again."}
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


def check_body(m, text):
    if not text.strip():
        return "The page body is empty."
    if "endraw" in text:
        return "The body may not contain \"endraw\" (it would end the page's raw block)."
    opens = sum(ln.strip() == wb.PRIVATE_OPEN for ln in text.split("\n"))
    closes = sum(ln.strip() == wb.PRIVATE_CLOSE for ln in text.split("\n"))
    if opens != closes:
        return "Each <!-- private --> needs its <!-- /private --> (everything after an unclosed one is withheld)."
    return None


def _stamp_now():
    return _now().strftime("%Y%m%dT%H%M%SZ")


# ── weekly ───────────────────────────────────────────────────────────────────

WEEKLY_FILES = ("site/insider-cluster-buys.md", "site/_data/announce.json", "site/issue.json")


def weekly_id(today=None):
    return f"weekly-{wb.issue_key(today or date.today()).lower()}"


def symbols_path():
    """The owner's portfolio and watchlist symbols, as the app last sent them:
    for the private block, the Sunday run's too. Local, readable by the owner."""
    return _root() / "social" / "my_symbols.json"


def keep_symbols(symbols):
    syms = sorted({s.strip().upper() for s in symbols if isinstance(s, str) and s.strip()})[:2000]
    p = symbols_path()
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = p.with_suffix(".tmp")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        json.dump(syms, fh)
    os.replace(tmp, p)


def health(now=None):
    """What is wrong with this week's brief, or "". Due from Sunday noon (it is
    built at nine) until the week is over, published or not by Monday noon."""
    now = now or datetime.now()
    if now.weekday() not in (6, 0, 1, 2) or (now.weekday() == 6 and now.hour < 12):
        return ""
    m = load(weekly_id(now.date()))
    if not m:
        # Only where there is a weekly brief at all.
        ever = any(x.is_dir() and x.name.startswith("weekly-") for x in drafts_dir().iterdir())
        return "This week's brief was not built." if ever else ""
    st = _effective_state(m)
    if st == "failed" and "cancelled" in (m.get("error") or ""):
        return ""
    if st == "failed":
        return f"This week's brief failed: {m.get('error') or 'see the log'}"
    if st == "ready" and not m.get("complete", True):
        return "This week's brief is incomplete: some filings could not be read."
    late = now.weekday() in (0, 1, 2) and (now.weekday() != 0 or now.hour >= 12)
    if st == "ready" and late and not m.get("published_at"):
        return "This week's brief is built but not published."
    return ""


def notify_health():
    """A desktop notice of health(), once per week and message (the tick timer)."""
    msg = health()
    if not msg:
        return
    mark = _root() / "social" / ".health"
    said = f"{weekly_id()} {msg}"
    try:
        if mark.read_text(encoding="utf-8") == said:
            return
    except OSError:
        pass
    try:
        subprocess.run(["notify-send", "--urgency=critical", "finterm", f"{msg} NEWS › What's On › WEEKLY BRIEF."],
                       timeout=10, check=True)
    except (OSError, subprocess.SubprocessError):
        print(msg)  # not shown: the next tick tries again
        return
    mark.parent.mkdir(parents=True, exist_ok=True)
    mark.write_text(said, encoding="utf-8")


def start_weekly_build(force=False, symbols=None):
    """Start the build in the background (minutes); the screen polls get()."""
    if symbols is not None:
        keep_symbols(symbols)
    did = weekly_id()
    m = load(did)
    if m and _effective_state(m) == "building":
        return {"error": "This week's brief is already being built."}
    if m and m.get("state") == "published" and not force:
        waiting = pending_posts(m)
        return {"error": "This week's brief is published. Generate a new edition? The page changes when it is "
                         "published again."
                         + (f" Not posted yet to {', '.join(waiting)}: that waits for the new edition." if waiting else ""),
                # Not needs_force: an older screen answers that with its own
                # question, about corrections.
                "needs_new_edition": True}
    if m and m.get("edited") and not force:
        return {"error": "This week's draft has corrections; generating replaces them.", "needs_force": True}
    # "building" is stored BEFORE the child starts (under the lock, from a
    # fresh load), so the child's own result can never be overwritten by it.
    with Lock(wait=True):
        base = load(did) or {"id": did, "kind": "weekly", "created": _iso(_now())}
        if base.get("state") != "building":  # a build that died keeps what was there before it
            base["prev_ready"] = base.get("state") == "ready" and bool(base.get("main"))
            base["prev_published"] = base.get("state") == "published"
        again = bool(base.get("prev_published"))
        base.update({"state": "building", "pid": os.getpid(), "error": "", "title": base.get("title") or
                     f"Weekly brief {wb.issue_key(date.today())}"})
        store(base)
    with open(log_path(), "a", encoding="utf-8") as log:
        log.write(f"\n=== {_iso(_now())} weekly build (Social screen)\n")
        log.flush()
        subprocess.Popen([sys.executable, os.path.abspath(__file__), "build-weekly", *(["--again"] if again else [])], stdout=log,
                         stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True,
                         cwd=os.path.dirname(os.path.abspath(__file__)))
    time.sleep(0.5)  # the child records its own pid at once; this one exits
    return get(did)


def cancel_build(did):
    """Stop a build started from the app: the brief is what it was before."""
    import signal

    m = load(did)
    if not m or _effective_state(m) != "building":
        return {"error": "Nothing is being built."}
    pid = int(m.get("pid") or 0)
    # Its own session (start_weekly_build), so its children end with it. The
    # Sunday run is not stopped from here: systemd owns it.
    try:
        mine = pid > 1 and os.getpgid(pid) == pid and "build-weekly" in subprocess.run(
            ["ps", "-o", "command=", "-p", str(pid)], capture_output=True, text=True).stdout
    except OSError:
        mine = False
    if not mine:
        return {"error": "The Sunday run is building it; it finishes by itself."}
    # Under the lock the finished draft is put in place with, and only while the
    # draft is still this build's: never in the middle of that, never another's.
    with Lock(wait=True):
        m = load(did)
        if not m or m.get("state") != "building" or m.get("pid") != pid:
            return {"error": "It had finished.", **(get(did) if m else {})}
        os.killpg(pid, signal.SIGTERM)
        for _ in range(50):
            if not _alive(pid):
                break
            time.sleep(0.1)
        else:
            os.killpg(pid, signal.SIGKILL)
            time.sleep(0.2)
        # Not load(): a died new edition reads as published there already.
        back = "published" if m.get("prev_published") else "ready" if m.get("prev_ready") else "failed"
        m.update({"state": back, "pid": None, "error": "the build was cancelled" if back == "failed" else ""})
        store(m)
        shutil.rmtree(_dir(did) / "build", ignore_errors=True)
        progress_path(did).unlink(missing_ok=True)
    return get(did)


PREFETCH_EVERY_S = 3 * 3600
PREFETCH_FOR_S = 8 * 60  # the tick that runs it holds up the next ones


def prefetch_filings():
    """Read the Form 4 filings published since, a day at a time (the tick timer):
    a build then has minutes of reading done for it. Never when a publish is
    near, and nothing depends on it."""
    mark = _root() / "social" / ".prefetched"
    try:
        if time.time() - mark.stat().st_mtime < PREFETCH_EVERY_S:
            return
    except FileNotFoundError:
        pass
    soon = _now() + timedelta(minutes=20)
    for s in list_drafts()["drafts"]:
        due = _parse(s.get("schedule_at"))
        if s["state"] == "building" or (due and due < soon):
            return
        m = load(s["id"])
        if m and m.get("state") == "published" and pending_posts(m):
            return
    if not os.environ.get(wb.CONTACT_KEY):
        c = wb.contact_from_keyring()
        if not c:
            return  # SEC refuses a reader that does not say who it is
        os.environ[wb.CONTACT_KEY] = c
    mark.parent.mkdir(parents=True, exist_ok=True)
    mark.touch()
    import sec_form4_market as f4

    f4.scan(days=wb.WINDOW_DAYS, max_new_days=1, deadline_s=PREFETCH_FOR_S)  # gives way to another scan


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


def build_weekly(if_unedited=False, again=False):
    """Build this week's draft (blocking). The generator writes into a
    scratch folder of the draft's own — never the publishing worktree, so a
    publish can run meanwhile — and only a successful build replaces the
    draft's files: a failed rebuild leaves the previous version in place."""
    did = weekly_id()
    with BuildLock(wait=True):
        m = load(did) or {"id": did, "kind": "weekly", "created": _iso(_now())}
        if m.get("state") == "published" and not again:
            print(f"{did} is already published")
            return 0
        # The timer must not replace text the owner has looked at: corrected,
        # or scheduled (scheduling is approval of THIS text).
        if if_unedited and m.get("state") == "ready" and (m.get("edited") or m.get("schedule_at")):
            print(f"{did} is corrected or scheduled; not rebuilt (rebuild it from the Social screen)")
            return 0
        had = (m.get("state") == "ready" or (m.get("state") == "building" and m.get("prev_ready"))) \
            and bool(m.get("main"))
        # A new edition of a published brief: if it cannot be built, the brief
        # is the published one again.
        was_published = again or (m.get("state") == "building" and bool(m.get("prev_published")))
        m.update({"state": "building", "pid": os.getpid(), "error": "",
                  "title": f"Weekly brief {wb.issue_key(date.today())}"})
        with Lock(wait=True):
            store(m)
        progress_path(did).unlink(missing_ok=True)
        scratch = _dir(did) / "build"
        shutil.rmtree(scratch, ignore_errors=True)
        (scratch / "site").mkdir(parents=True)
        try:
            # Output goes straight to this process's stdout — the log file (or
            # the journal) — so the screen's "recent log" moves during a long
            # build and a killed build keeps what it said.
            sys.stdout.flush()
            st = settings_get()
            r = subprocess.run([sys.executable, str(repo_root() / "tools/weekly_brief.py"), "--site",
                                str(scratch / "site"), "--allow-incomplete", "--draft",
                                "--topics", ",".join(st["topics"]), "--detail", st["detail"],
                                "--progress", str(progress_path(did)), "--symbols", str(symbols_path())],
                               stdin=subprocess.DEVNULL, timeout=BUILD_TIMEOUT_S)
            if r.returncode != 0:
                raise RuntimeError(f"the brief failed (exit {r.returncode}) — see the log")
            posts = sorted((scratch / "site/_posts").glob("*.md"))
            if len(posts) != 1:
                raise RuntimeError(f"the brief wrote {len(posts)} issue pages, expected 1")
            post = posts[0].resolve()
            main = str(post.relative_to(scratch.resolve()))
            issue = json.loads((scratch / "site/issue.json").read_text(encoding="utf-8"))
            with Lock(wait=True):
                m = load(did) or m
                for which in ("files", "generated"):
                    shutil.rmtree(_dir(did) / which, ignore_errors=True)
                    # With the picture of the issue, when one was drawn.
                    files = [main, *WEEKLY_FILES] + [str(p.relative_to(scratch)) for p in
                                                     sorted((scratch / "site/assets/brief").glob("*.png"))]
                    for rel in files:
                        dst = _file(m, rel, which)
                        dst.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(scratch / rel, dst)
                private_path(did).unlink(missing_ok=True)
                if (scratch / "private.md").exists():
                    shutil.copy2(scratch / "private.md", private_path(did))
                    os.chmod(private_path(did), 0o600)
                # New text has not been reviewed: any schedule is cleared.
                m.update({"state": "ready", "key": issue["issue"], "url": issue.get("url", ""), "main": main,
                          "files": files, "built_at": _iso(_now()), "edited": False,
                          "complete": bool(issue.get("complete", True)), "notes": issue.get("notes", []),
                          "x_text": issue.get("x", ""), "x_generated": issue.get("x", ""), "pid": None,
                          "schedule_at": "", "accept_incomplete": False, "error": "",
                          "prev_ready": False, "prev_published": False,
                          # Where it posts once live: the settings' platforms,
                          # unless the owner already chose for this draft.
                          "post_to": post_to(m) if post_to_chosen(m) else st["platforms"],
                          "post_to_chosen": post_to_chosen(m),
                          "topics": st["topics"], "detail": st["detail"]})
                store(m)
            print(f"{did} ready ({main})")
            return 0
        except Exception as e:  # noqa: BLE001 — recorded for the screen
            with Lock(wait=True):
                m = load(did) or m
                if was_published:
                    m.update({"state": "published", "pid": None,
                              "error": f"the new edition was not built ({str(e)[:300]}); the published one stands"})
                elif had:
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
    import sec_directory  # the app's one copy of SEC's ticker directory
    import sec_http  # the one SEC client: contact identity, shared pacing
    try:
        sec_directory.rows()
    except Exception as e:  # noqa: BLE001
        return {"error": f"SEC refused the company list ({type(e).__name__}); is the contact email set in "
                         "Settings › Credentials?"}
    companies, unknown = [], []
    for t in tickers:
        row = sec_directory.lookup(t)
        if not row:
            unknown.append(t)
            continue
        cik = row["cik"]
        c = {"ticker": t, "cik": cik, "name": row["title"], "sic": "", "filings": []}
        try:
            r, status = sec_http.get(f"https://data.sec.gov/submissions/CIK{cik:010d}.json", timeout=20)
            if r is None:  # refused or offline: the filings are said to be missing, not empty
                c["filings_error"] = f"HTTP {status}" if status else "no answer"
                companies.append(c)
                continue
            sub = r.json()
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
        out += ["Open-market purchases (Form 4, code P) reported by insiders; purchases by 10% owners and "
                "pre-scheduled 10b5-1 plan trades are excluded.", "", "| Ticker | Insiders | Reported | Trade dates |", "|---|---:|---:|---|"]
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


def theme_create(title, overview, facts):
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
    # A collection of its own (site/_config.yml "themes"), not _posts: the
    # weekly archive, the home page's latest issue and the feed are weekly
    # issues only.
    rel = f"site/_themes/{today.isoformat()}-{did}.md"
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


def save(did, body=None, x_text=None, post_x=None, post_to_list=None):
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
    if post_to_list is not None:
        import social_post as sp
        bad = [n for n in post_to_list if n not in sp.ENABLED_NETWORKS]
        if bad:
            return {"error": f"not a platform this build posts to: {', '.join(bad)}"}
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
                # The evergreen insider page is the generated list: a
                # correction of the issue does not touch it.
                for rel in ("site/issue.json", "site/_data/announce.json"):
                    p = _file(m, rel)
                    d = json.loads(p.read_text(encoding="utf-8"))
                    d["stamp"] = stamp.split(" stamp ")[1].rstrip(" ->")
                    p.write_text(json.dumps(d, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        if x_text is not None:
            m["x_text"] = x_text
            if m["kind"] == "weekly":
                for rel in ("site/issue.json", "site/_data/announce.json"):
                    p = _file(m, rel)
                    d = json.loads(p.read_text(encoding="utf-8"))
                    d["x"] = x_text
                    p.write_text(json.dumps(d, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        if post_x is not None:
            m["post_to"] = sorted(set(post_to(m)) | {"x"}) if post_x else [n for n in post_to(m) if n != "x"]
            m["post_to_chosen"] = True
        if post_to_list is not None:
            m["post_to"] = list(dict.fromkeys(post_to_list))
            m["post_to_chosen"] = True
        # Corrected = the page OR the X text differs from what was generated
        # (the timer's rebuild and Revert both key on this).
        gen = _file(m, m["main"], "generated")
        page_now = "\n".join(split_page(_file(m, m["main"]).read_text(encoding="utf-8"))[2])
        page_gen = "\n".join(split_page(gen.read_text(encoding="utf-8"))[2]) if gen.exists() else None
        m["edited"] = page_now != page_gen or m.get("x_text", "") != m.get("x_generated", "")
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
    # The evergreen page too: a draft corrected before it was separate from the
    # issue carries that correction.
    ever = "site/insider-cluster-buys.md"
    if m.get("kind") == "weekly" and _file(m, ever, "generated").exists():
        shutil.copy2(_file(m, ever, "generated"), _file(m, ever))
    return save(did, body="\n".join(split_page(gen.read_text(encoding="utf-8"))[2]),
                x_text=m.get("x_generated") or None)


def schedule(did, at, accept_incomplete=False):
    """Publish at @at (ISO; empty = unschedule). The timer's tick does it."""
    m = load(did)
    why = _editable(m)
    if why:
        return {"error": why}
    if not at:
        with Lock(wait=True):
            m = load(did)
            why = _editable(m)
            if why:
                return {"error": why}
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
    with Lock(wait=True):
        m = load(did)  # a tick may have published it meanwhile
        why = _editable(m)
        if why:
            return {"error": why}
        m.update({"schedule_at": _iso(when), "accept_incomplete": bool(accept_incomplete)})
        store(m)
    return get(did)


def _index_closes():
    """The S&P 500's closes since the record began (FRED), for its outcomes."""
    import fred_data

    r = fred_data.get_series_keyless("SP500", (date.today() - timedelta(days=3650)).isoformat())
    if r.get("error"):
        raise RuntimeError(r["error"])
    return [(date.fromisoformat(o["date"]), float(o["value"])) for o in r.get("observations") or []]


def _state_kept(m, public_main):
    """The issue's call when the page still states it; None when the owner's
    correction took it out — then it is not kept, and the picture, which shows
    it, is not published."""
    try:
        issue = json.loads(_file(m, "site/issue.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    call = issue.get("call")
    return issue if call and call.get("line", "") in public_main else {k: v for k, v in issue.items() if k != "call"}


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
    closes = []
    if m["kind"] == "weekly":  # before the lock: a slow answer holds up no one else
        try:
            closes = _index_closes()
        except Exception as e:  # noqa: BLE001 — an unread index never holds a publish back
            print(f"track record: outcomes not updated ({type(e).__name__})")
    with Lock(wait=True):
        # Again under the lock: another publisher (the timer's tick, the
        # screen's) may have published it while this one waited.
        m = load(did)
        why = _editable(m)
        if why:
            return {"error": why, "already": bool(m and m.get("state") == "published")}
        _reset_worktree()
        wt = worktree()
        issue = _state_kept(m, _file(m, m["main"]).read_text(encoding="utf-8")) if m["kind"] == "weekly" else {}
        for rel in m["files"]:
            dst = wt / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            if rel.endswith(".png"):
                if issue.get("call"):
                    shutil.copy2(_file(m, rel), dst)
                else:
                    dst.unlink(missing_ok=True)
                continue
            text = _file(m, rel).read_text(encoding="utf-8")
            # Private blocks (the owner's Nasdaq IPO section) never leave
            # this machine; a weekly rerun keeps its file name, so it
            # replaces the week's issue rather than adding one.
            public = wb.strip_private(text)
            if wb.PRIVATE_OPEN in public or "only you see this" in public:
                raise RuntimeError(f"{rel}: a private section survived stripping; not published")
            if not issue.get("call") and m["kind"] == "weekly":
                if rel == m["main"]:
                    public = re.sub(r"(?m)^image: .*\n", "", public, count=1)
                elif rel.endswith(".json"):
                    public = json.dumps({k: v for k, v in json.loads(public).items() if k != "call"}, indent=2,
                                        ensure_ascii=False) + "\n"
            dst.write_text(public, encoding="utf-8")
        record = []
        if m["kind"] == "weekly":  # its stance, kept for good
            import brief_track

            record = ["site/_data/calls.json"]
            brief_track.update(wt / record[0], issue, _now(), closes)
        # (A picture that was neither kept nor ever published is no path to git.)
        paths = [p for p in (*m["files"], *record) if (wt / p).exists() or _git("ls-files", "--", p)]
        _git("add", "-A", *paths)
        if subprocess.run(["git", "-C", str(wt), "diff", "--cached", "--quiet"]).returncode == 0:
            m.update({"state": "published", "published_at": _iso(_now()), "schedule_at": "",
                      "commit": _git("rev-parse", "HEAD"), "error": ""})
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


def _texts(m):
    """The text each network posts. Generated: each network's own (built for
    its limit, from the same figures). Corrected by the owner: the corrected
    text, everywhere — a generated text beside it could carry the figure that
    was corrected."""
    texts = {"x": m.get("x_text", "")}
    if m.get("x_text", "") != m.get("x_generated", ""):
        return {n: m.get("x_text", "") for n in ("x", "bluesky", "mastodon")}
    try:
        issue = json.loads(_file(m, "site/issue.json").read_text(encoding="utf-8"))
        for n in ("bluesky", "mastodon"):
            if issue.get(n):
                texts[n] = issue[n]
    except (OSError, ValueError, KeyError):
        pass
    if m.get("kind") != "weekly":  # a theme brief: one text, everywhere
        for n in ("bluesky", "mastodon"):
            texts.setdefault(n, m.get("x_text", ""))
    return texts


def pending_posts(m):
    """Platforms this published draft still has to be posted to."""
    import social_post as sp
    done = sp.posted(m["key"]) if m.get("key") else {}
    # A platform not connected is skipped, not retried (the UI says so);
    # connecting it and Post sends it.
    return [n for n in post_to(m) if n not in done and n in sp.ENABLED_NETWORKS and sp.is_configured(n) is not False]


def post_live(m, log=print, again=()):
    """Post a published draft to every platform still pending — once its page
    shows this version. Records each platform's reference or error on the
    draft. True when it tried."""
    import social_post as sp
    pending = list(dict.fromkeys([*pending_posts(m), *again]))
    if not pending:
        return False
    pub = _parse(m.get("published_at"))
    if pub and (_now() - pub).total_seconds() > X_POST_GIVE_UP_S and not m.get("posted_late_ok"):
        quiet = [n for n in pending if n not in post_errors(m)]
        if quiet:
            with Lock(wait=True):
                m = load(m["id"]) or m
                errs = post_errors(m)
                for n in quiet:
                    errs.setdefault(n, "the page never showed this version; not posted")
                m["post_errors"] = errs
                store(m)
        return False
    if not page_live(m):
        return False
    texts = _texts(m)
    # A corrected text is X's (a link costs 23 there); Bluesky counts the
    # link in full — refused here, with the reason, rather than cut.
    too_long = {}
    if len(texts.get("bluesky", "")) > 300 and "bluesky" in pending:
        too_long["bluesky"] = (f"the corrected text is {len(texts['bluesky'])} characters with its link; "
                               "Bluesky takes 300 — shorten it")
    send = [n for n in pending if n not in too_long]
    try:
        results, _failed = (sp.announce(m["key"], {n: texts.get(n, "") for n in send}, only=set(send),
                                        log=log, generated=(m.get("x_text") == m.get("x_generated")),
                                        again=again)
                            if send else ({}, False))
    except Exception as e:  # noqa: BLE001
        results = {n: f"failed: {type(e).__name__}: {e}" for n in send}
    results.update(too_long)
    with Lock(wait=True):
        m = load(m["id"]) or m
        refs, errs = dict(m.get("posted") or {}), post_errors(m)
        record = sp.posted(m["key"])
        for n in pending:
            status = results.get(n, "")
            if status.startswith(("posted", "already")):
                refs[n] = record.get(n, "") or status
                errs.pop(n, None)
            else:
                errs[n] = status or "not posted"
        m["posted"], m["post_errors"] = refs, errs
        if "x" in refs:  # older readers
            m["x_ref"], m["x_error"] = refs["x"], ""
        elif "x" in errs:
            m["x_error"] = errs["x"]
        store(m)
    return True


PAGE_WAIT_S = 240  # for a page just pushed, when the post cannot be left to a tick


def post_now(did, networks, publish_first=False, again=False, accept_incomplete=False):
    """A brief posted on demand, to @networks and no other. A draft is published
    first when @publish_first (the screen confirms it); what is not yet posted
    then goes out once its page is live, from here or by tick().

    @again: also to platforms that already have this week's brief. Only ever
    from here and only once: it is not stored, so no timer, poll or rerun can
    send it, and a run that dies sends nothing twice."""
    import social_post as sp
    m = load(did)
    if not m:
        return {"error": "no such brief"}
    bad = [n for n in networks if n not in sp.ENABLED_NETWORKS]
    if bad:
        return {"error": f"not a platform this build posts to: {', '.join(bad)}"}
    off = _not_connected(networks)
    networks = [n for n in networks if n not in off]
    if not networks:
        return {"error": f"Not connected: {', '.join(off)}." if off else "Tick a platform."}
    pushed = m.get("state") != "published"
    if pushed:
        if not publish_first:
            return {"error": "Publish the brief first — a post links to its page."}
        r = publish(did, accept_incomplete=accept_incomplete)
        if r.get("error"):
            return r
    with Lock(wait=True):
        m = load(did)
        done = sp.posted(m["key"])
        # These, and where it already is: not what an earlier choice had listed.
        m["post_to"] = list(dict.fromkeys([*(n for n in post_to(m) if n in done), *networks]))
        m["post_to_chosen"] = True
        m["posted_late_ok"] = True  # asked for now: the 6-hour give-up does not apply
        store(m)
    repeat = {n for n in networks if n in done} if again else set()
    live, until = page_live(m), time.time() + (PAGE_WAIT_S if pushed and repeat else 0)
    while not live and time.time() < until:
        time.sleep(10)
        live = page_live(m)
    if not live:
        return {**get(did), "waiting": True,
                "error": "Published. The page is not live yet: "
                         + ("post again once it is." if repeat else "posted as soon as it is.")}
    post_live(m, again=repeat)
    out = get(did)
    # What THIS call could not post: an error of another platform, or of an
    # earlier try, is on the draft and not this call's answer.
    return {**out, "off": off, "post_errors": {n: e for n, e in (out.get("post_errors") or {}).items() if n in networks}}


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
        if m.get("state") == "published" and pending_posts(m):
            if post_live(m, log):
                done.append(m["id"])
    return {"acted": done}


def main(argv):
    cmd = argv[1] if len(argv) > 1 else ""
    if cmd == "build-weekly":
        return build_weekly(if_unedited="--if-unedited" in argv, again="--again" in argv)
    if cmd == "tick":
        try:
            print(json.dumps(tick()))
        except RuntimeError as e:  # another run holds the lock: the next tick does it
            print(f"skipped: {e}")
        try:
            notify_health()
        except Exception as e:  # noqa: BLE001
            print(f"health: {type(e).__name__}: {e}")
        try:
            prefetch_filings()
        except Exception as e:  # noqa: BLE001 — a convenience: the build reads what is missing
            print(f"filings not read ahead: {type(e).__name__}: {e}")
        return 0
    if cmd == "publish" and len(argv) > 2:
        m = load(argv[2])
        if m and m.get("state") == "published":
            print(f"{argv[2]} is already published")
            return 0  # a rerun of --auto is safe
        out = publish(argv[2], accept_incomplete="--accept-incomplete" in argv)
        print(json.dumps(out, indent=2, ensure_ascii=False))
        return 1 if out.get("error") else 0
    # For tools/weekly_publish.sh — no Python embedded in shell.
    if cmd == "weekly-id":
        print(weekly_id())
        return 0
    if cmd == "state" and len(argv) > 2:
        m = load(argv[2]) or {}
        print(_effective_state(m) if m else "none")
        return 0
    if cmd == "x-ref" and len(argv) > 2:
        print((load(argv[2]) or {}).get("x_ref", ""))
        return 0
    if cmd == "post-x" and len(argv) > 2:  # older timer scripts
        argv = [argv[0], "post-to", argv[2]]
        cmd = "post-to"
    if cmd == "post-to" and len(argv) > 2:
        # The settings' platforms (the unattended run posts where the owner chose).
        with Lock(wait=True):
            m = load(argv[2])
            if not m:
                return 1
            m["post_to"] = list(dict.fromkeys(post_to(m) + settings_get()["platforms"]))
            store(m)
        return 0
    if cmd == "posted-all" and len(argv) > 2:  # exit 0 when nothing is left to post
        m = load(argv[2]) or {}
        # (A new edition not yet published: there is nothing live to wait for.)
        return 0 if m and (m.get("state") != "published" or not pending_posts(m)) else 1
    if cmd == "auto-mode":
        print(settings_get()["auto"])
        return 0
    if cmd == "list":
        print(json.dumps(list_drafts(), indent=2, ensure_ascii=False))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
