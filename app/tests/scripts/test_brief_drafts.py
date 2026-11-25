#!/usr/bin/env python3
"""Brief drafts: review, correct, schedule and publish — and what must never
be published.

Runs against a throwaway data folder and a throwaway git "origin" (a bare
repo with a site/ folder), so publish() really commits and pushes, without
touching the real site or the real worktree. No network.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
from datetime import date, datetime, timedelta, timezone
from pathlib import Path

TMP = tempfile.mkdtemp(prefix="brief-drafts-test-")
os.environ["FINTERM_DATA_DIR"] = TMP
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import brief_drafts as bd  # noqa: E402
import weekly_brief_core as wb  # noqa: E402

FAILURES = []
POST = "site/_posts/2026-11-09-insider-buying.md"
URL = "https://yumima.github.io/finterm/brief/2026-11-09/"


def git(*args, cwd):
    subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True)


def origin():
    """A bare origin with an initial site, cloned as the publishing worktree."""
    bare = Path(TMP) / "origin.git"
    if bare.exists():
        return bare
    git("init", "--quiet", "--bare", "-b", "main", str(bare), cwd=TMP)
    seed = Path(TMP) / "seed"
    (seed / "site/_posts").mkdir(parents=True)
    (seed / "site/index.md").write_text("home\n")
    git("init", "--quiet", "-b", "main", cwd=seed)
    for k, v in (("user.email", "t@example.com"), ("user.name", "t")):
        git("config", k, v, cwd=seed)
    git("add", "-A", cwd=seed)
    git("commit", "--quiet", "-m", "seed", cwd=seed)
    git("remote", "add", "origin", str(bare), cwd=seed)
    git("push", "--quiet", "origin", "main", cwd=seed)
    wt = bd.worktree()
    git("clone", "--quiet", str(bare), str(wt), cwd=TMP)
    for k, v in (("user.email", "t@example.com"), ("user.name", "t")):
        git("config", k, v, cwd=wt)
    return bare


def body(private=True):
    lines = ["## Markets and economy", "", "| 10-year Treasury | 4.92% |", "",
             "## Stocks", "",
             "| Insider buying | Company |", "|---|---|", "| AAA | Alpha |", "",
             "## Week ahead, Nov 16–20", "", "| Wed 18 | 8:30 CPI |", ""]
    if private:
        lines += wb.private_block(["## Private: IPOs ahead (Nasdaq calendar — only you see this; never published)",
                                   "", "| 2026-11-19 | SECRETCO | Secret Co |"])
    return lines


def make_weekly(did="weekly-2026-w46", complete=True):
    stamp = "<!-- issue 2026-W46 stamp 20261114T000000Z -->"
    post = wb.page({"title": "Weekly brief", "date": date(2026, 11, 9)},
                   [stamp, "", "{% raw %}"] + body() + ["{% endraw %}", ""], wb.ISSUE_DISCLAIMER)
    ever = wb.page({"title": "Insider", "permalink": "/insider-cluster-buys/"},
                   ["_Updated November 14, 2026 · links_", "", stamp, "", "{% raw %}",
                    "## Insider buying clusters", "", "| AAA | Alpha |", "{% endraw %}", ""])
    issue = {"issue": "2026-W46", "url": URL, "stamp": "20261114T000000Z", "x": f"Insider buying. {URL}",
             "complete": complete, "notes": [] if complete else ["3 of 5 days read"]}
    m = {"id": did, "kind": "weekly", "title": "Weekly brief 2026-W46", "key": "2026-W46", "url": URL,
         "main": POST, "files": [POST] + list(bd.WEEKLY_FILES), "state": "ready", "edited": False,
         "complete": complete, "notes": issue["notes"], "created": bd._iso(bd._now()),
         "x_text": issue["x"], "x_generated": issue["x"]}
    for which in ("files", "generated"):
        for rel, text in ((POST, post), ("site/insider-cluster-buys.md", ever),
                          ("site/issue.json", json.dumps(issue)), ("site/_data/announce.json", json.dumps(issue))):
            p = bd._file(m, rel, which)
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(text, encoding="utf-8")
    bd.store(m)
    return m


# ── format ───────────────────────────────────────────────────────────────────

def test_split_join_round_trip_keeps_stamp_and_disclaimer():
    m = make_weekly("weekly-rt")
    text = bd._file(m, POST).read_text(encoding="utf-8")
    front, stamp, b, disc = bd.split_page(text)
    assert stamp.startswith("<!-- issue 2026-W46 stamp") and disc == f"_{wb.ISSUE_DISCLAIMER}_"
    assert "{% raw %}" not in "\n".join(b)
    again = bd.split_page(bd.join_page(front, stamp, b, disc))
    assert again == (front, stamp, b, disc)


def test_strip_private_fails_closed():
    text = "a\n<!-- private -->\nsecret\n<!-- /private -->\nb\n<!-- private -->\nunclosed secret\nmore"
    out = wb.strip_private(text)
    assert out == "a\nb", out
    assert "secret" not in out


def test_check_body():
    m = {"kind": "weekly"}
    assert "empty" in bd.check_body(m, "  ")
    assert "endraw" in bd.check_body(m, "## Insider buying clusters\n{% endraw %}")
    assert bd.check_body(m, "## Something else") is None  # no heading is needed: sections are the owner's choice
    assert "private" in bd.check_body(m, "## Insider buying clusters\n<!-- private -->\nx")
    assert bd.check_body({"kind": "theme"}, "## The theme\ntext") is None


# ── edit ─────────────────────────────────────────────────────────────────────

def test_save_marks_edited_restamps_and_evergreen_stays_generated():
    m = make_weekly("weekly-save")
    got = bd.get(m["id"])
    edited = got["body"].replace("| AAA | Alpha |", "| AAA | Alpha Corp (corrected) |")
    r = bd.save(m["id"], body=edited)
    assert not r.get("error") and r["edited"], r
    post = bd._file(m, POST).read_text(encoding="utf-8")
    ever = bd._file(m, "site/insider-cluster-buys.md").read_text(encoding="utf-8")
    assert "Alpha Corp (corrected)" in post and "| AAA | Alpha |" in ever  # the full list is not the issue's table
    assert "_Updated November 14, 2026" in ever and "SECRETCO" not in ever
    stamp = bd.split_page(post)[1]
    assert stamp != "<!-- issue 2026-W46 stamp 20261114T000000Z -->"
    issue = json.loads(bd._file(m, "site/issue.json").read_text())
    assert stamp.endswith(f"stamp {issue['stamp']} -->"), (stamp, issue["stamp"])
    # back to the generated text: not edited any more — the evergreen too, should
    # it carry a correction from before it was separate
    bd._file(m, "site/insider-cluster-buys.md").write_text(ever.replace("Alpha", "Old correction"))
    r = bd.revert(m["id"])
    assert not r.get("error") and not r["edited"], r
    assert "Old correction" not in bd._file(m, "site/insider-cluster-buys.md").read_text()


def test_x_text_must_keep_the_link_and_lands_in_issue_json():
    m = make_weekly("weekly-x")
    assert "link" in bd.save(m["id"], x_text="no link here")["error"]
    r = bd.save(m["id"], x_text=f"Corrected post {URL}", post_x=True)
    assert not r.get("error") and r["post_x"], r
    assert json.loads(bd._file(m, "site/issue.json").read_text())["x"] == f"Corrected post {URL}"


# ── schedule and publish ─────────────────────────────────────────────────────

def test_schedule_rules():
    m = make_weekly("weekly-sched", complete=False)
    past = (datetime.now(timezone.utc) - timedelta(hours=1)).isoformat()
    assert "passed" in bd.schedule(m["id"], past)["error"]
    soon = (datetime.now(timezone.utc) + timedelta(hours=1)).isoformat()
    assert bd.schedule(m["id"], soon).get("needs_confirm")  # an incomplete week asks first
    r = bd.schedule(m["id"], soon, accept_incomplete=True)
    assert r["schedule_at"] and r["accept_incomplete"], r
    assert bd.schedule(m["id"], "")["schedule_at"] == ""


def test_publish_pushes_public_files_only():
    bare = origin()
    m = make_weekly("weekly-pub", complete=False)
    assert bd.publish(m["id"]).get("needs_confirm")  # incomplete: confirm first
    r = bd.publish(m["id"], accept_incomplete=True)
    assert not r.get("error") and r["state"] == "published", r
    shown = subprocess.run(["git", "--git-dir", str(bare), "show", f"main:{POST}"], capture_output=True,
                           text=True).stdout
    assert "| Insider buying |" in shown and "## Week ahead" in shown
    assert "SECRETCO" not in shown and "private" not in shown, "a private section was published"
    # published drafts are frozen
    assert "already published" in bd.save(m["id"], body="## Insider buying clusters\nx")["error"]
    # the worktree is left clean at origin/main
    st = subprocess.run(["git", "-C", str(bd.worktree()), "status", "--porcelain"], capture_output=True, text=True)
    assert st.stdout.strip() == "", st.stdout


def test_tick_publishes_what_is_due():
    origin()
    m = make_weekly("weekly-tick")
    m["schedule_at"] = (datetime.now(timezone.utc) - timedelta(minutes=1)).isoformat()
    bd.store(m)
    m2 = make_weekly("weekly-later")
    m2["schedule_at"] = (datetime.now(timezone.utc) + timedelta(days=1)).isoformat()
    bd.store(m2)
    out = bd.tick(log=lambda *_: None)
    assert "weekly-tick" in out["acted"] and "weekly-later" not in out["acted"], out
    assert bd.load("weekly-tick")["state"] == "published" and bd.load("weekly-later")["state"] == "ready"


def test_settings_validate_and_drive_the_build():
    st = bd.settings_get()
    assert st["detail"] == "standard" and st["auto"] == "draft" and "insider" in st["topics"], st
    assert "error" in bd.settings_set({"topics": ["nope"]})
    assert "error" in bd.settings_set({"topics": ["ipo_private"]})  # nothing public: refused
    assert "error" in bd.settings_set({"platforms": ["myspace"]})
    assert "error" in bd.settings_set({"detail": "huge"})
    out = bd.settings_set({"v": 2, "topics": ["funds", "macro", "summary"], "detail": "short", "platforms": ["mastodon", "x"],
                           "auto": "publish"})
    assert out["topics"] == ["summary", "macro", "funds"] and out["platforms"] == ["x", "mastodon"], out  # canonical order
    assert bd.settings_get() == out
    bd.settings_set(bd.DEFAULT_SETTINGS)


def test_post_to_replaces_post_x_and_older_drafts_still_read():
    m = make_weekly("weekly-platforms")
    assert bd.post_to(m) == []
    m["post_x"] = True  # a draft from before platforms
    bd.store(m)
    assert bd.post_to(bd.load("weekly-platforms")) == ["x"]
    out = bd.save("weekly-platforms", post_to_list=["x", "bluesky"])
    assert out.get("post_to") == ["x", "bluesky"], out
    assert "error" in bd.save("weekly-platforms", post_to_list=["myspace"])
    # The per-network texts: X's is the edited one, the others the issue's own.
    texts = bd._texts(bd.load("weekly-platforms"))
    assert texts["x"].endswith(URL), texts


def test_settings_from_before_the_one_page_brief():
    bd.settings_path().parent.mkdir(parents=True, exist_ok=True)
    bd.settings_path().write_text(json.dumps({"topics": ["summary", "insider", "screens"], "detail": "short"}))
    st = bd.settings_get()
    assert st["topics"] == ["summary", "insider", "funds", "screens"] and st["detail"] == "short" and st["v"] == 2, st
    # A screen without a box for fund moves does not switch them off...
    assert "funds" in bd.settings_set({"topics": ["insider", "screens"], "detail": "full"})["topics"]
    # ...one with a box does, for good.
    bd.settings_set({"v": 2, "topics": ["summary", "insider", "screens"]})
    assert bd.settings_get()["topics"] == ["summary", "insider", "screens"], bd.settings_get()
    assert "error" in bd.settings_set({"topics": ["summary"]})
    assert "error" in bd.settings_set({"v": 2, "topics": ["summary", "ipo_private"]})
    bd.settings_path().write_text("[1]")  # not a settings file: the defaults
    assert bd.settings_get()["topics"] == list(bd.TOPICS)
    bd.settings_set(bd.DEFAULT_SETTINGS)


def test_platforms_chosen_on_a_draft_survive_a_rebuild_settings_drive_the_rest():
    m = make_weekly("weekly-chosen")
    assert not bd.post_to_chosen(m)
    bd.save("weekly-chosen", post_to_list=["x"])
    assert bd.post_to_chosen(bd.load("weekly-chosen"))
    old = {"id": "o", "post_x": False}  # a draft from before platforms: its choice stands
    assert bd.post_to_chosen(old) and bd.post_to(old) == []


def test_a_corrected_text_goes_everywhere():
    m = make_weekly("weekly-corrected")
    t = bd._texts(m)
    assert t["x"] == m["x_text"]
    bd.save("weekly-corrected", x_text="Corrected figure. " + URL)
    t = bd._texts(bd.load("weekly-corrected"))
    assert t["x"] == t["bluesky"] == t["mastodon"] == "Corrected figure. " + URL, t


def test_not_connected_is_skipped_and_old_x_error_kept():
    import social_post as sp
    m = make_weekly("weekly-skip")
    m.update({"post_to": ["x", "bluesky"], "key": "2026-W99"})
    saved = dict(sp._configured)
    sp._configured.update({"x": True, "bluesky": False})
    try:
        assert bd.pending_posts(m) == ["x"], bd.pending_posts(m)
        assert bd._not_connected(["x", "bluesky"]) == ["bluesky"]
    finally:
        sp._configured.clear()
        sp._configured.update(saved)
    assert bd.post_errors({"x_error": "failed: 403"}) == {"x": "failed: 403"}
    assert bd.post_errors({"x_error": "old", "x_ref": "123"}) == {}


def test_post_now_needs_a_published_brief():
    make_weekly("weekly-not-yet")
    assert "Publish the brief first" in bd.post_now("weekly-not-yet", ["x"]).get("error", "")


class FakeNetworks:
    """Posting without a network: what was sent, and with which arguments."""

    def __init__(self):
        self.sent = []

    def post(self, name):
        def go(text, creds, issue, **kw):
            self.sent.append((name, text, kw))
            return f"{name}-{len(self.sent)}"
        return go

    def __enter__(self):
        import social_post as sp
        self.saved = (sp.NETWORKS, sp.credentials, dict(sp._configured), bd.page_live)
        sp.NETWORKS = [("x", (), self.post("x")), ("bluesky", (), self.post("bluesky"))]
        sp.credentials = lambda keys: {}
        sp._configured.update({"x": True, "bluesky": True})
        self.live = True
        bd.page_live = lambda m: self.live
        return self

    def __exit__(self, *a):
        import social_post as sp
        sp.NETWORKS, sp.credentials, conf, bd.page_live = self.saved
        sp._configured.clear()
        sp._configured.update(conf)


def test_post_on_demand_publishes_first_and_again_only_when_asked():
    origin()
    with FakeNetworks() as net:
        m = make_weekly("weekly-demand")
        m["key"] = "2026-W77"
        bd.store(m)
        assert "Tick a platform" in bd.post_now("weekly-demand", [], publish_first=True)["error"]
        net.live = False  # published, the page not up yet: posted by the next tick
        m["post_to"] = ["x", "bluesky"]  # an earlier choice: only what was asked for goes out
        bd.store(m)
        r = bd.post_now("weekly-demand", ["x"], publish_first=True)
        assert r["state"] == "published" and r["waiting"] and "as soon as it is" in r["error"] and not net.sent, r
        assert bd.load("weekly-demand")["post_to"] == ["x"]
        net.live = True
        bd.tick(log=lambda *_: None)
        assert [s[0] for s in net.sent] == ["x"], net.sent
        bd.tick(log=lambda *_: None)
        bd.post_now("weekly-demand", ["x"])  # already there, not asked for again
        assert len(net.sent) == 1, net.sent
        # a repeat is never left for a tick: not live, nothing is kept
        net.live = False
        r = bd.post_now("weekly-demand", ["x"], again=True)
        assert r["waiting"] and "post again once it is" in r["error"], r
        net.live = True
        bd.tick(log=lambda *_: None)
        assert len(net.sent) == 1, net.sent
        # ...nor by a tick that runs while it is being sent
        real = net.post("x")
        import social_post as sp

        def slow(text, creds, issue, **kw):
            # what a tick would send meanwhile: never the repeat
            assert "x" not in bd.pending_posts(bd.load("weekly-demand"))
            return real(text, creds, issue, **kw)
        sp.NETWORKS[0] = ("x", (), slow)
        r = bd.post_now("weekly-demand", ["x", "bluesky"], again=True)
        sp.NETWORKS[0] = ("x", (), real)
        # (Bluesky had not had it: an ordinary post, whoever sends it first)
        assert sorted(s[0] for s in net.sent) == ["bluesky", "x", "x"], net.sent
        again = [s for s in net.sent if s[0] == "x"][1]
        assert again[2] == {"trust_duplicate": False}, net.sent  # X's duplicate refusal is then a failure
        assert r["posted"]["x"] != "x-1", r
        bd.tick(log=lambda *_: None)
        assert len(net.sent) == 3
        bd.post_now("weekly-demand", ["bluesky"], again=True)
        assert net.sent[3][2] == {"again": True}, net.sent
        sp._configured["bluesky"] = False
        assert "Not connected: bluesky" in bd.post_now("weekly-demand", ["bluesky"], again=True)["error"]
        assert bd.post_now("weekly-demand", ["x", "bluesky"])["off"] == ["bluesky"]
        # an earlier failure of another platform is not this call's answer
        sp._configured["bluesky"] = True
        late = bd.load("weekly-demand")
        late["post_errors"] = {"x": "failed: 403"}
        bd.store(late)
        assert bd.post_now("weekly-demand", ["bluesky"], again=True)["post_errors"] == {}
        # a new edition: published, its page waited for, then posted again
        late = bd.load("weekly-demand")
        late.update({"state": "ready"})
        bd.store(late)
        sleep, seen = bd.time.sleep, []
        net.live = False
        bd.time.sleep = lambda s: (seen.append(s), setattr(net, "live", True))
        before = len(net.sent)
        r = bd.post_now("weekly-demand", ["x"], publish_first=True, again=True)
        bd.time.sleep = sleep
        assert seen == [10] and [s[0] for s in net.sent[before:]] == ["x"], (seen, r)
        net.sent = net.sent[:4]
        sp._configured["bluesky"] = True
        # incomplete: refused until confirmed, and nothing is posted
        m = make_weekly("weekly-demand2", complete=False)
        assert bd.post_now("weekly-demand2", ["x"], publish_first=True).get("needs_confirm")
        left = bd.load("weekly-demand2")
        assert left["state"] == "ready" and len(net.sent) == 4 and bd.post_to(left) == [], left  # nothing left armed


def test_a_published_brief_can_be_generated_again():
    origin()
    did = bd.weekly_id()
    m = make_weekly(did)
    m.update({"state": "published", "published_at": bd._iso(bd._now())})
    bd.store(m)
    r = bd.start_weekly_build()
    assert r.get("needs_new_edition") and not r.get("needs_force") and "new edition" in r["error"], r
    assert bd.load(did)["state"] == "published"
    # the build fails: the published brief stands
    started, popen, real = [], subprocess.Popen, bd.repo_root
    bd.subprocess.Popen = lambda *a, **k: started.append(a)
    bd.repo_root = lambda: Path(TMP) / "no-such-repo"
    try:
        bd.start_weekly_build(force=True)
        assert "--again" in started[0][0] and bd.load(did)["state"] == "building"
        # killed: it reads as the published brief, and a second try still knows it
        dead = bd.load(did)
        dead["pid"] = 2 ** 22 + 1
        bd.store(dead)
        assert bd.get(did)["state"] == "published", bd.get(did)
        bd.start_weekly_build(force=True)
        assert "--again" in started[1][0]
        bd.subprocess.Popen = popen
        assert bd.build_weekly(again=True) == 1
    finally:
        bd.subprocess.Popen, bd.repo_root = popen, real
    after = bd.load(did)
    assert after["state"] == "published" and "the published one stands" in after["error"], after
    # a new edition waiting to be published is not deleted under the live page
    after.update({"state": "ready"})
    bd.store(after)
    assert "new edition" in bd.delete(did)["error"]
    shutil.rmtree(bd._dir(did))


# ── theme briefs ─────────────────────────────────────────────────────────────

FACTS = {"days": 30, "unknown": ["ZZZZ"], "insider_note": "",
         "companies": [{"ticker": "NVDA", "cik": 1045810, "name": "NVIDIA CORP", "sic": "Semiconductors",
                        "filings": [{"form": "8-K", "date": "2026-11-05", "url": "https://www.sec.gov/x"}]},
                       {"ticker": "AMD", "cik": 2488, "name": "ADVANCED MICRO_DEVICES", "sic": "", "filings": []}],
         "insiders": [{"symbol": "AMD", "insiders": 1, "value": 120000.0, "first_trade": "2026-11-03",
                       "last_trade": "2026-11-03"}]}


def test_theme_fixed_structure():
    r = bd.theme_create("AI chips", "Overview text.", FACTS)
    assert not r.get("error"), r
    heads = [ln for ln in r["body"].split("\n") if ln.startswith("## ")]
    assert heads == ["## The theme", "## The companies", "## Insider buying, last 30 days",
                     "## Recent SEC filings, last 30 days", "## Method and sources"], heads
    assert "ADVANCED MICRO\\_DEVICES" in r["body"] and "ZZZZ" in r["body"]
    assert r["url"].startswith(wb.SITE_URL + "/themes/ai-chips-") and r["url"] in r["x_text"]
    text = bd._file(bd.load(r["id"]), bd.load(r["id"])["main"]).read_text()
    assert f"permalink: \"/themes/{r['id'][len('theme-'):]}/\"" in text, text[:300]
    assert bd.THEME_DISCLAIMER in text
    # a second brief on the same theme the same day gets its own page
    r2 = bd.theme_create("AI chips", "", FACTS)
    assert r2["id"] != r["id"] and "_Write the overview here._" in r2["body"]
    assert "Give the theme a title" in bd.theme_create(" ", "", FACTS)["error"]


def test_overview_cannot_break_the_page():
    r = bd.theme_create("Safe", "Intro.\n{% endraw %}\n<!-- private -->\nrest", FACTS)
    assert not r.get("error"), r
    assert "endraw" not in r["body"] and "<!-- private -->" not in r["body"] and "rest" in r["body"]


def test_refused_save_changes_nothing():
    m = make_weekly("weekly-refused")
    before = bd._file(m, POST).read_text()
    r = bd.save(m["id"], body=bd.get(m["id"])["body"] + "\nnew line", x_text="no link")
    assert "link" in r["error"]
    assert bd._file(m, POST).read_text() == before and not bd.load(m["id"])["edited"]


def test_rebuild_guard_and_failed_rebuild_keeps_previous():
    m = make_weekly(bd.weekly_id())
    m["schedule_at"] = (datetime.now(timezone.utc) + timedelta(hours=2)).isoformat()
    bd.store(m)
    assert bd.build_weekly(if_unedited=True) == 0  # scheduled = approved: the timer leaves it
    assert bd.load(m["id"])["schedule_at"]
    real = bd.repo_root
    bd.repo_root = lambda: Path(TMP) / "no-such-repo"  # the generator fails
    try:
        assert bd.build_weekly() == 1
    finally:
        bd.repo_root = real
    after = bd.load(m["id"])
    assert after["state"] == "ready" and "previous build" in after["error"], after
    assert bd._file(after, POST).exists()


def test_tick_records_a_failing_publish_and_goes_on():
    origin()
    bad = make_weekly("weekly-bad")
    bad["schedule_at"] = (datetime.now(timezone.utc) - timedelta(minutes=1)).isoformat()
    bad["files"] = bad["files"] + ["site/missing.md"]  # copying it raises
    bd.store(bad)
    good = make_weekly("weekly-good")
    good["schedule_at"] = bad["schedule_at"]
    bd.store(good)
    bd.tick(log=lambda *_: None)
    assert "scheduled publish failed" in bd.load("weekly-bad")["error"]
    assert bd.load("weekly-bad")["schedule_at"]  # retried next tick
    assert bd.load("weekly-good")["state"] == "published"


def test_a_stored_note_is_not_a_call_error():
    m = make_weekly("weekly-note")
    m["error"] = "rebuild failed (x); this is the previous build"
    bd.store(m)
    r = bd.save(m["id"], post_x=True)
    assert "error" not in r and "previous build" in r["note"], r
    # an X-text-only correction counts as corrected (Revert, the timer's guard)
    r = bd.save(m["id"], x_text=f"Changed {URL}")
    assert r["edited"], r


def test_theme_briefs_are_not_weekly_issues():
    r = bd.theme_create("Grid", "x", FACTS)
    assert bd.load(r["id"])["main"].startswith("site/_themes/"), bd.load(r["id"])["main"]


def test_tickers_parsed():
    assert bd.parse_tickers("nvda, $AMD;  brk.b  nvda") == ["NVDA", "AMD", "BRK.B"]
    assert bd.slugify("AI chips: who makes it?") == "ai-chips-who-makes-it"


def main():
    for t in [v for k, v in sorted(globals().items()) if k.startswith("test_") and callable(v)]:
        try:
            t()
            print(f"ok   {t.__name__}")
        except AssertionError as e:
            print(f"FAIL {t.__name__}: {e}")
            FAILURES.append(t.__name__)
        except Exception:
            import traceback
            print(f"ERROR: {t.__name__}")
            traceback.print_exc()
            FAILURES.append(t.__name__)
    print()
    if FAILURES:
        print(f"{len(FAILURES)} failure(s): {', '.join(FAILURES)}")
        return 1
    print("all brief draft tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
