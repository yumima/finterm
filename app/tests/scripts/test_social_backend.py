#!/usr/bin/env python3
"""The Social screen's backend: connecting Bluesky / Mastodon, disconnecting,
the enabled-networks switch, secrets kept off argv, and the hand-made launch
posts' record.

Offline: every HTTP call goes through a fake _req and the keyring is a dict —
nothing here reaches a network or the real keyring.
"""
import io
import json
import os
import sys
import tempfile
import types

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts"))
import finterm_keyring  # noqa: E402
import launch_posts as lp  # noqa: E402
import social_app as sa  # noqa: E402
import social_post as sp  # noqa: E402

fails = []


def check(cond, what):
    if not cond:
        fails.append(what)


# ── fakes ───────────────────────────────────────────────────────────────────
KEYRING = {}
sp.keyring_get = lambda name: KEYRING.get(name)
sp.keyring_set = lambda name, value: KEYRING.__setitem__(name, value)
sp.keyring_delete = lambda name: KEYRING.pop(name, None)
for k in list(os.environ):
    if k.startswith(("X_", "BSKY_", "MASTODON_")):
        os.environ.pop(k)

CALLS = []
ROUTES = {}  # (method, url) -> answer dict, or an Exception to raise


def fake_req(method, url, *, headers=None, data=None, json_body=None, timeout=30):
    CALLS.append({"method": method, "url": url, "headers": headers or {}, "data": data, "json": json_body})
    ans = ROUTES.get((method, url.split("?")[0]))
    if ans is None:
        raise AssertionError(f"unexpected request {method} {url}")
    if isinstance(ans, Exception):
        raise ans
    return ans


sp._req = fake_req
# social_app's own fetches (issue.json, the page) never leave the machine either.
sa._published_issue = lambda: None
sa._page_live = lambda url, stamp="": False
TMP = tempfile.mkdtemp(prefix="social-test-")
STATE = os.path.join(TMP, "announced.json")
sp.default_state_path = lambda: STATE


def reset():
    KEYRING.clear()
    CALLS.clear()
    ROUTES.clear()


def no_secret_in(out, *secrets_):
    blob = json.dumps(out)
    return not any(s and s in blob for s in secrets_)


APP_PW = "abcd-efgh-ijkl-mnop"  # the shape of a Bluesky app password, not a real one

# ── the enabled-networks switch ─────────────────────────────────────────────
check(sp.ENABLED_NETWORKS == ("x", "bluesky", "mastodon"),
      f"X, Bluesky and Mastodon ship enabled (the weekly brief posts where the owner picks): {sp.ENABLED_NETWORKS}")
# The gate itself: with a network switched off, every action for it is refused.
sp.ENABLED_NETWORKS = ("x",)
reset()
for act, payload in [("bsky_connect", {"handle": "a.bsky.social", "app_password": APP_PW}),
                     ("mastodon_connect_start", {"instance": "mastodon.social"}),
                     ("mastodon_connect_finish", {"code": "c"}),
                     ("publish", {"network": "bluesky", "issue": "2026-W46", "text": "t"}),
                     ("publish", {"network": "mastodon", "issue": "2026-W46", "text": "t"}),
                     ("launch_mark", {"target": "hn-show", "url": "https://news.ycombinator.com/item?id=1"})]:
    out = sa.handle_action(act, payload)
    check("not enabled" in out.get("error", ""), f"{act} {payload.get('network', '')} must be refused: {out}")
check(not CALLS and not KEYRING, "a refused action must not call out or store anything")
ch = {c["network"]: c for c in sa.handle_action("channels", {})["channels"]}
check(ch["x"]["enabled"] and not ch["bluesky"]["enabled"] and not ch["mastodon"]["enabled"]
      and not ch["reddit"]["enabled"] and not ch["hackernews"]["enabled"], f"channels carry the switch: {ch}")

# Everything below exercises the gated paths with every network switched on.
sp.ENABLED_NETWORKS = ("x", "bluesky", "mastodon", "reddit", "hackernews")

# ── payload on stdin; secrets refused on argv ───────────────────────────────
act, payload = sa.read_payload(["social_app.py", "x_connect_finish", "-"], io.StringIO('{"pin": "1234567"}'))
check((act, payload) == ("x_connect_finish", {"pin": "1234567"}), "stdin payload is read")
check(sa.read_payload(["s", "channels", "-"], io.StringIO("")) == ("channels", {}), "empty stdin = {}")
for secret_act in sa.SECRET_ACTIONS:
    try:
        sa.read_payload(["s", secret_act, '{"pin":"1"}'], io.StringIO(""))
        fails.append(f"{secret_act} accepted its payload from argv")
    except ValueError:
        pass
check(sa.read_payload(["s", "launch", '{"a":1}'], io.StringIO("")) == ("launch", {"a": 1}),
      "non-secret actions still take argv (by hand)")
buf = io.StringIO()
sa.main(["s", "bsky_connect", json.dumps({"app_password": APP_PW})], io.StringIO(""), buf)
check("stdin only" in json.loads(buf.getvalue()).get("error", ""), "main() answers {error} for argv secrets")
check(APP_PW not in buf.getvalue(), "the refused secret is not echoed back")

# ── Bluesky ─────────────────────────────────────────────────────────────────
SESSION_URL = f"{sp.BSKY_XRPC}/com.atproto.server.createSession"


def jwt(scope):
    import base64

    body = base64.urlsafe_b64encode(json.dumps({"scope": scope}).encode()).decode().rstrip("=")
    return f"e30.{body}.sig"


reset()
out = sp.bsky_connect("@someone.bsky.social", "my-real-password")
check("not an app password" in out.get("error", "") and not CALLS and not KEYRING,
      f"a non-app password is refused before anything is sent: {out}")
reset()
ROUTES[("POST", SESSION_URL)] = sp.HttpError("POST", SESSION_URL, 401,
                                             '{"error":"AuthenticationRequired","message":"Invalid identifier or password"}')
out = sp.bsky_connect("someone.bsky.social", APP_PW)
check("Invalid identifier or password" in out.get("error", "") and not KEYRING,
      f"a refused session stores nothing: {out}")
check(no_secret_in(out, APP_PW), "the error does not carry the password")
reset()
ROUTES[("POST", SESSION_URL)] = {"accessJwt": jwt("com.atproto.access"), "handle": "someone.bsky.social"}
out = sp.bsky_connect("someone.bsky.social", APP_PW)
check("full access" in out.get("error", "") and not KEYRING, f"a full-access session is refused: {out}")
reset()
ROUTES[("POST", SESSION_URL)] = {"accessJwt": jwt("com.atproto.appPass"), "handle": "someone.bsky.social",
                                 "did": "did:plc:x"}
out = sp.bsky_connect("@Someone.bsky.social ", APP_PW)
check(out == {"account": "someone.bsky.social"}, f"bsky_connect answers the canonical handle: {out}")
check(KEYRING == {"BSKY_HANDLE": "someone.bsky.social", "BSKY_APP_PASSWORD": APP_PW}, f"stored: {KEYRING}")
check(CALLS[0]["json"] == {"identifier": "Someone.bsky.social", "password": APP_PW}, "session request body")

# ── Mastodon ────────────────────────────────────────────────────────────────
for given, want in [("mastodon.social", "https://mastodon.social"),
                    (" https://Mastodon.Social/about ", "https://mastodon.social"),
                    ("@me@fosstodon.org", "https://fosstodon.org"),
                    ("me@hachyderm.io/", "https://hachyderm.io"),
                    ("http://mastodon.social", None), ("https://mastodon.social:8443", None),
                    ("localhost", None), ("", None), ("not a host", None), ("javascript:alert(1)", None),
                    ("https://user@mastodon.social", None)]:
    got = sp.mastodon_instance(given)
    check(got == want, f"mastodon_instance({given!r}) = {got!r}, want {want!r}")

BASE = "https://mastodon.example"
reset()
out = sp.mastodon_connect_start("http://mastodon.example")
check("https only" in out.get("error", "") and not CALLS, f"plain http is refused: {out}")
reset()
ROUTES[("POST", f"{BASE}/api/v1/apps")] = {"client_id": "CID123", "client_secret": "CSECRET456"}
out = sp.mastodon_connect_start("mastodon.example")
check(out.get("url", "").startswith(f"{BASE}/oauth/authorize?"), f"start returns the authorize url: {out}")
check("response_type=code" in out["url"] and "redirect_uri=urn%3Aietf%3Awg%3Aoauth%3A2.0%3Aoob" in out["url"]
      and "scope=write%3Astatuses+read%3Aaccounts" in out["url"], f"authorize url params: {out['url']}")
check(no_secret_in(out, "CSECRET456"), "the client secret is never returned")
check(CALLS[0]["data"]["redirect_uris"] == sp.MASTODON_OOB and CALLS[0]["data"]["client_name"] == "finterm"
      and CALLS[0]["data"]["scopes"] == sp.MASTODON_SCOPES, f"app registration: {CALLS[0]['data']}")
check(json.loads(KEYRING["MASTODON_PENDING_APP"])["client_secret"] == "CSECRET456", "pending app kept")
check("MASTODON_TOKEN" not in KEYRING, "nothing connected yet")

ROUTES[("POST", f"{BASE}/oauth/token")] = sp.HttpError("POST", f"{BASE}/oauth/token", 400,
                                                         '{"error":"invalid_grant","error_description":"bad code"}')
out = sp.mastodon_connect_finish("WRONG")
check("bad code" in out.get("error", "") and "MASTODON_TOKEN" not in KEYRING, f"a refused code: {out}")
check(no_secret_in(out, "CSECRET456"), "the token error does not carry the client secret")

ROUTES[("POST", f"{BASE}/oauth/token")] = {"access_token": "TOKEN789", "scope": "read:accounts"}
out = sp.mastodon_connect_finish("CODE")
check("permission to post" in out.get("error", "") and "MASTODON_TOKEN" not in KEYRING,
      f"a token that cannot post is refused: {out}")

ROUTES[("POST", f"{BASE}/oauth/token")] = {"access_token": "TOKEN789", "scope": "write:statuses read:accounts"}
ROUTES[("GET", f"{BASE}/api/v1/accounts/verify_credentials")] = OSError("connection reset")
out = sp.mastodon_connect_finish("CODE")
check("nothing was stored" in out.get("error", "") and "MASTODON_TOKEN" not in KEYRING,
      f"an unverified token is not stored: {out}")
check(no_secret_in(out, "TOKEN789"), "the error does not carry the token")

ROUTES[("GET", f"{BASE}/api/v1/accounts/verify_credentials")] = {"username": "brief"}
CALLS.clear()
out = sp.mastodon_connect_finish(" CODE ")
check(out == {"account": "@brief@mastodon.example"}, f"finish answers @user@host: {out}")
check(KEYRING.get("MASTODON_BASE_URL") == BASE and KEYRING.get("MASTODON_TOKEN") == "TOKEN789"
      and KEYRING.get("MASTODON_POSTING_ACCOUNT") == "@brief@mastodon.example"
      and "MASTODON_PENDING_APP" not in KEYRING, f"stored after finish: {sorted(KEYRING)}")
check(CALLS[0]["data"]["code"] == "CODE" and CALLS[0]["data"]["grant_type"] == "authorization_code",
      "token exchange body")
check(CALLS[1]["headers"].get("Authorization") == "Bearer TOKEN789", "verify uses the new token")
out = sp.mastodon_connect_finish("CODE")
check("No Mastodon sign-in in progress" in out.get("error", ""), "a second finish has no pending app")

# ── disconnect ──────────────────────────────────────────────────────────────
ROUTES[("POST", f"{BASE}/oauth/revoke")] = {}
out = sa.handle_action("disconnect", {"network": "mastodon"})
check("revoked" in out.get("note", "") and not any(k.startswith("MASTODON_") for k in KEYRING),
      f"mastodon disconnect revokes and removes everything: {out} {sorted(KEYRING)}")
check(CALLS[-1]["data"] == {"client_id": "CID123", "client_secret": "CSECRET456", "token": "TOKEN789"},
      "revoke request body")

reset()
KEYRING.update({"MASTODON_BASE_URL": BASE, "MASTODON_TOKEN": "T"})
ROUTES[("POST", f"{BASE}/oauth/revoke")] = OSError("down")
out = sp.disconnect("mastodon")
check("Authorized apps" in out.get("note", "") and not KEYRING, f"a failed revoke still removes locally: {out}")

reset()
KEYRING.update({"X_API_KEY": "k", "X_API_SECRET": "s", "X_ACCESS_TOKEN": "t", "X_ACCESS_SECRET": "ts",
                "X_POSTING_ACCOUNT": "acct", "X_PENDING_REQUEST": "a:b"})
out = sa.handle_action("disconnect", {"network": "x"})
check(KEYRING == {"X_API_KEY": "k", "X_API_SECRET": "s"},
      f"X disconnect keeps the developer app's consumer keys only: {sorted(KEYRING)}")
check("error" not in out and "environment" not in out.get("note", ""), f"x disconnect: {out}")
os.environ["X_API_KEY"] = "k"  # app keys alone can't post: no "still applies" note
check("environment" not in sp.disconnect("x").get("note", ""), "X's app keys in the env are not a sign-in")
os.environ["X_ACCESS_TOKEN"] = "t"
check("environment" in sp.disconnect("x").get("note", ""), "an env access token still signs in: say so")
os.environ.pop("X_API_KEY")
os.environ.pop("X_ACCESS_TOKEN")
reset()
KEYRING.update({"BSKY_HANDLE": "h", "BSKY_APP_PASSWORD": "p"})
sp.disconnect("bluesky")
check(not KEYRING, "bluesky disconnect")
check("unknown network" in sa.handle_action("disconnect", {"network": "reddit"}).get("error", ""),
      "manual networks have nothing to disconnect")


def unavailable(name):
    raise finterm_keyring.KeyringUnavailable("locked")


saved_delete, sp.keyring_delete = sp.keyring_delete, unavailable
out = sp.disconnect("bluesky")
check("unlocked" in out.get("error", ""), f"a locked keyring is an error, not a success: {out}")
sp.keyring_delete = saved_delete

# X's PIN finish removes the pending request instead of writing a placeholder.
reset()
KEYRING.update({"X_API_KEY": "k", "X_API_SECRET": "s", "X_PENDING_REQUEST": "rt:rs"})
saved_x = sp._x_signed_post
sp._x_signed_post = lambda *a, **k: {"oauth_token": "AT", "oauth_token_secret": "AS", "screen_name": "acct"}
out = sp.x_connect_finish("1234")
sp._x_signed_post = saved_x
check(out == {"account": "acct"} and "X_PENDING_REQUEST" not in KEYRING, f"x finish clears pending: {KEYRING}")

# finterm_keyring.delete clears every identity (get() falls back to the legacy one).
cleared = []
fake = types.SimpleNamespace(
    Schema=types.SimpleNamespace(new=lambda name, flags, attrs: name),
    SchemaFlags=types.SimpleNamespace(NONE=0),
    SchemaAttributeType=types.SimpleNamespace(STRING=0),
    password_clear_sync=lambda schema, attrs, cancel: cleared.append((schema, attrs)) or True)
saved_secret, finterm_keyring._secret = finterm_keyring._secret, lambda: fake
finterm_keyring.delete("BSKY_APP_PASSWORD")
check(cleared == [("finterm", {"finterm-key": "BSKY_APP_PASSWORD"}),
                  ("com.fincept.terminal", {"fincept-key": "BSKY_APP_PASSWORD"})],
      f"delete clears current and legacy: {cleared}")
finterm_keyring._secret = saved_secret

# ── launch drafts ───────────────────────────────────────────────────────────
ids = [t["id"] for t in lp.TARGETS]
check(len(ids) == len(set(ids)) == 7, f"seven unique targets: {ids}")
check({t["community"] for t in lp.TARGETS} == {"r/algotrading", "r/ValueInvesting", "r/StockMarket",
                                               "r/smallstreetbets", "r/SideProject", "r/coolgithubprojects",
                                               "Show HN"}, "the communities of the plan")
for t in lp.TARGETS:
    check(t["kind"] in ("text", "link") and t["network"] in sp.MANUAL_NETWORKS, f"{t['id']}: kind/network")
    check(bool(t.get("body")) == (t["kind"] == "text") and bool(t.get("url")) == (t["kind"] == "link"),
          f"{t['id']}: a text post has a body, a link post a URL")
    check(len(t["title"]) <= (80 if t["network"] == "hackernews" else 300), f"{t['id']}: title too long")
    check("AI" in (t.get("body", "") + t.get("first_comment", "")), f"{t['id']}: discloses AI assistance")

rows = [{"symbol": "FUN", "issuer": "Six Flags | NEW", "insiders": 3, "value": 15_100_000,
         "first_trade": "2026-11-06", "last_trade": "2026-11-10"},
        {"symbol": "", "issuer": "No Ticker Co", "insiders": 2, "value": None,
         "first_trade": "2026-11-05", "last_trade": "2026-11-05"}] + \
       [{"symbol": f"S{i}", "issuer": f"Co {i}", "insiders": 2, "value": 1e5} for i in range(20)]
sm = lp.render(lp.target("reddit-stockmarket"), {"issue": "2026-W46", "rows": rows})
table = [ln for ln in sm["body"].splitlines() if ln.startswith("|")]
check(len(table) == 2 + 10, f"r/StockMarket gets the top 10 rows: {len(table) - 2}")
check("| $FUN | Six Flags \\| NEW | 3 | $15.1M | 2026-11-06 – 2026-11-10 |" == table[2], f"row: {table[2]}")
check("| — | No Ticker Co | 2 | — | 2026-11-05 |" == table[3], f"absent symbol/value stay absent: {table[3]}")
check(sm["needs_issue"] and not sm["data_missing"] and "{table}" not in sm["body"], "filled")
check(lp.LIST_URL in sm["body"] and "[link]" not in sm["body"], "the doc's [link] is the real list URL")
ssb = lp.render(lp.target("reddit-smallstreetbets"), {"rows": rows})
check(len([ln for ln in ssb["body"].splitlines() if ln.startswith("|")]) == 2 + 22,
      "r/smallstreetbets gets every published row to cut from")
missing = lp.render(lp.target("reddit-stockmarket"), None)
check(missing["data_missing"] and "{table}" not in missing["body"], "no issue: marked, no raw placeholder")
check(not lp.render(lp.target("hn-show"), None)["needs_issue"], "static drafts need no issue")

algo = lp.target("reddit-algotrading")
hn = lp.target("hn-show")
for t, url, ok in [(algo, "https://www.reddit.com/r/algotrading/comments/abc/x/", True),
                   (algo, "https://old.reddit.com/r/AlgoTrading/comments/abc/", True),
                   (algo, "https://www.reddit.com/r/stocks/comments/abc/", False),
                   (algo, "https://evil.example/r/algotrading/comments/abc/", False),
                   (algo, "https://reddit.com.evil.example/r/algotrading/", False),
                   (algo, "https://evilreddit.com/r/algotrading/comments/abc/", False),
                   (algo, "http://www.reddit.com/r/algotrading/comments/abc/", False),
                   (algo, "www.reddit.com/r/algotrading/comments/abc/", False),
                   (hn, "https://news.ycombinator.com/item?id=4242", True),
                   (hn, "https://news.ycombinator.com/item?id=abc", False),
                   (hn, "https://news.ycombinator.com/submitted?id=x", False),
                   (hn, "https://www.reddit.com/r/algotrading/comments/abc/", False)]:
    why = lp.validate_post_url(t, url)
    check((why == "") == ok, f"validate_post_url({t['id']}, {url}) -> {why!r}")

# ── launch record: same file and lock as the issues, kept apart ─────────────
sp.save(STATE, {"2026-W46": {"x": "123"}})
out = sa.handle_action("launch_mark", {"target": "reddit-algotrading", "url": "https://example.com/x"})
check("reddit.com" in out.get("error", ""), f"a non-reddit url is refused by the backend: {out}")
out = sa.handle_action("launch_mark", {"target": "reddit-algotrading",
                                       "url": "https://www.reddit.com/r/algotrading/comments/abc/x/"})
check(out.get("ok") and out["record"]["community"] == "r/algotrading", f"marked: {out}")
out = sa.handle_action("launch_mark", {"target": "reddit-algotrading",
                                       "url": "https://www.reddit.com/r/algotrading/comments/def/y/"})
check(out.get("already") and "abc" in out.get("error", ""), f"a second mark is refused without replace: {out}")
out = sa.handle_action("launch_mark", {"target": "reddit-algotrading", "replace": True,
                                       "url": "https://www.reddit.com/r/algotrading/comments/def/y/"})
check(out.get("ok"), f"replace: {out}")
state = sp.load_state(STATE)
check(state["2026-W46"] == {"x": "123"} and "def" in state["launch"]["reddit-algotrading"]["url"],
      f"the issue record is untouched: {state}")
h = sa.handle_action("history", {})
check([r["issue"] for r in h["history"]] == ["2026-W46"] and "reddit-algotrading" in h["launch"],
      f"history keeps launch apart from issues: {h}")
check(sp.posted("2026-W46", STATE) == {"x": "123"}, "posted() unaffected")
# The automated announce path still works with a launch key in the file.
results, failed = sp.announce("2026-W46", {"x": "t"}, only={"x"}, state_path=STATE, log=lambda _l: None)
check(results.get("x", "").startswith("already posted") and "launch" in sp.load_state(STATE),
      f"announce keeps the launch record: {results}")
check(sa.handle_action("launch_mark", {"target": "nope", "url": "x"}).get("error", "").startswith("unknown"),
      "unknown target")

# An unreadable record is never rewritten with only the launch entry: that
# would erase every issue's "already posted" and let the timer post again.
with open(STATE, "w", encoding="utf-8") as f:
    f.write('{"2026-W46": {"x": "1')
out = sa.handle_action("launch_mark", {"target": "hn-show", "url": "https://news.ycombinator.com/item?id=7"})
check("unreadable" in out.get("error", ""), f"an unreadable record refuses the mark: {out}")
check(open(STATE, encoding="utf-8").read() == '{"2026-W46": {"x": "1', "the unreadable record is left as it was")

# The launch action fills the table from the rows the screen sends — no fetch.
out = sa.handle_action("launch", {"issue": "2026-W46", "rows": rows})
sm = next(t for t in out["targets"] if t["id"] == "reddit-stockmarket")
check(out["issue_loaded"] and "$FUN" in sm["body"] and sm["issue"] == "2026-W46", "launch fills from the payload")
out = sa.handle_action("launch", {})
check(not out["issue_loaded"] and next(t for t in out["targets"] if t["id"] == "reddit-stockmarket")["data_missing"],
      "no rows: the table is marked missing")

import shutil  # noqa: E402

shutil.rmtree(TMP, ignore_errors=True)
for f in fails:
    print("FAIL:", f)
print("social backend: all checks passed" if not fails else f"{len(fails)} failure(s)")
sys.exit(1 if fails else 0)
