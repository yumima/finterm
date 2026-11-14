"""The Social screen's backend: channels, the weekly brief as a draft source,
and publishing — the same code and the same "already posted" record as the
owner's tools (tools/weekly_publish.sh, tools/announce.py), so the button and
the weekly timer can never post the same issue twice.

    social_app.py <action> -     payload (JSON) on stdin → one JSON object on stdout

The payload comes on stdin, never the command line: argv is readable by any
local user (ps, /proc/*/cmdline), and payloads carry PINs, codes and passwords.
A payload in argv is still accepted for the non-secret actions (by hand).

Actions:
  channels                     each network: configured?, posting account
  brief                        the PUBLISHED issue (issue.json beside the
                               page): rows, per-network texts, what was
                               posted, whether its page is live
  publish    {network, issue, text}
                               post one text (the UI confirms first); only
                               for the published, live issue, and never one
                               already posted to that network
  history                      everything posted, newest issue first, and
                               the hand-made launch posts
  x_connect_start / x_connect_finish {pin}
                               PIN sign-in of the posting X account
  bsky_connect {handle, app_password}
  mastodon_connect_start {instance} / mastodon_connect_finish {code}
  disconnect {network}         remove that network's stored sign-in
  launch {issue, rows}         the Reddit / HN launch drafts (launch_posts.py)
                               filled from the issue rows the screen shows,
                               with what was already posted
  launch_mark {target, url, replace}
                               record a launch post the user made by hand

Only social_post.ENABLED_NETWORKS may be published to, connected or marked.

Credentials are read from finterm's keyring and never returned.
"""
import json
import sys
import time
import urllib.request
from datetime import date, datetime, timezone

import finterm_keyring
import launch_posts as lp
import social_post as sp
import weekly_brief_core as wb



def _channels():
    out = []
    enabled = set(sp.ENABLED_NETWORKS)
    for name, keys, _ in sp.NETWORKS:
        acct, err, ok = "", "", False
        try:
            ok = len(sp.credentials(keys)) == len(keys)
            if ok and name == "x":
                acct = sp.keyring_get("X_POSTING_ACCOUNT") or ""
            elif ok and name == "bluesky":
                acct = sp.keyring_get("BSKY_HANDLE") or ""
            elif ok and name == "mastodon":
                acct = sp.keyring_get("MASTODON_POSTING_ACCOUNT") or ""
        except finterm_keyring.KeyringUnavailable as e:
            ok, err = False, str(e)
        limit, url_cost = wb.NETWORK_LIMITS[name]
        out.append({"network": name, "configured": ok, "account": acct, "error": err,
                    "limit": limit, "url_cost": url_cost, "manual": False, "enabled": name in enabled})
    # Communities that are posted to by hand: their APIs need approval and
    # their rules forbid recurring links. The UI copies text + opens submit.
    for name in sp.MANUAL_NETWORKS:
        out.append({"network": name, "configured": True, "manual": True, "account": "", "enabled": name in enabled})
    return {"channels": out}


def _not_enabled(network):
    if network in sp.ENABLED_NETWORKS:
        return None
    return {"error": f"{network} is not enabled in this build (coming soon)."}


def _page_live(url, stamp=""):
    try:
        req = urllib.request.Request(url, headers={"User-Agent": sp.UA})
        with urllib.request.urlopen(req, timeout=10) as r:
            body = r.read(400_000).decode("utf-8", "replace")
        return True if not stamp else (f"stamp {stamp}" in body)
    except Exception:
        return False


def _published_issue():
    """The issue as published (issue.json beside the pages), or None."""
    try:
        req = urllib.request.Request(f"{wb.ISSUE_JSON_URL}?t={int(time.time())}", headers={"User-Agent": sp.UA})
        with urllib.request.urlopen(req, timeout=15) as r:
            return json.loads(r.read(2_000_000))
    except Exception:
        return None


def _brief(_payload):
    issue = _published_issue()
    if not issue:
        return {"error": "No published issue found yet — the weekly run publishes one each Sunday."}
    texts = {n: issue[n] for n, _, _ in sp.NETWORKS if n in issue}
    return {
        "issue": issue["issue"], "url": issue["url"], "date": issue.get("date", ""),
        "count": issue.get("count", len(issue.get("rows", []))), "rows": issue.get("rows", []),
        "texts": texts, "posted": sp.posted(issue["issue"]),
        "page_live": _page_live(issue["url"], issue.get("stamp", "")),
        "current_week": issue["issue"] == wb.issue_key(date.today()),
        "disclaimer": wb.DISCLAIMER,
    }


def _publish(payload):
    network, issue_key, text = payload.get("network"), payload.get("issue"), (payload.get("text") or "").strip()
    if network not in {n for n, _, _ in sp.NETWORKS}:
        return {"error": f"unknown network: {network}"}
    refused = _not_enabled(network)
    if refused:
        return refused
    if not text:
        return {"error": "nothing to post"}
    issue = _published_issue()
    # Only the published issue, and only once its page shows that version:
    # a post must never link to a missing or different page.
    if not issue or issue.get("issue") != issue_key:
        return {"error": "That issue is not the one currently published; refresh and try again."}
    if not _page_live(issue["url"], issue.get("stamp", "")):
        return {"error": "The issue's page is not live yet; try again in a few minutes."}
    if issue["url"] not in text:
        return {"error": "The post must keep the link to the issue page."}
    lines = []
    results, failed = sp.announce(issue_key, {network: text}, only={network}, log=lines.append,
                                  generated=(text == issue.get(network)))
    status = results.get(network, "")
    if failed or not status.startswith("posted"):
        return {"error": status or "; ".join(lines), "already": status.startswith("already")}
    return {"ok": True, "network": network, "issue": issue_key, "ref": sp.posted(issue_key).get(network, "")}


def _history():
    state = sp.load_state(sp.default_state_path())
    launch = state.pop(sp.LAUNCH_KEY, {})
    return {"history": [{"issue": k, "posts": v} for k, v in sorted(state.items(), reverse=True)],
            "launch": launch}


def _launch(payload):
    """The drafts, filled from the issue rows the screen already loaded
    (payload {issue, rows}) — no second fetch of issue.json, and the table
    always matches the issue shown beside it."""
    rows = payload.get("rows")
    issue = {"issue": str(payload.get("issue") or ""), "rows": rows} if isinstance(rows, list) and rows else None
    return {"targets": [lp.render(t, issue) for t in lp.TARGETS], "rules": lp.RULES,
            "skip": [{"community": c, "why": w} for c, w in lp.SKIP],
            "posted": sp.launch_records(), "issue_loaded": issue is not None}


def _launch_mark(payload):
    t = lp.target(payload.get("target"))
    if not t:
        return {"error": f"unknown launch target: {payload.get('target')}"}
    refused = _not_enabled(t["network"])
    if refused:
        return refused
    url = str(payload.get("url") or "").strip()
    why = lp.validate_post_url(t, url)
    if why:
        return {"error": why}
    record = {"url": url, "at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
              "network": t["network"], "community": t["community"]}
    try:
        existing = sp.record_launch(t["id"], record, replace=bool(payload.get("replace")))
    except RuntimeError as e:
        return {"error": str(e)}
    if existing:
        return {"error": f"{t['community']} already has a launch post recorded ({existing.get('url', '')}).",
                "already": True}
    return {"ok": True, "target": t["id"], "record": record}


# Connect actions, by the network they connect (gated by ENABLED_NETWORKS).
CONNECT_ACTIONS = {"x_connect_start": "x", "x_connect_finish": "x", "bsky_connect": "bluesky",
                   "mastodon_connect_start": "mastodon", "mastodon_connect_finish": "mastodon"}

# Actions whose payload carries a secret: refused unless it came on stdin.
SECRET_ACTIONS = {"x_connect_finish", "bsky_connect", "mastodon_connect_finish"}


def handle_action(act, payload):
    if act == "channels":
        return _channels()
    if act == "brief":
        return _brief(payload)
    if act == "publish":
        return _publish(payload)
    if act == "history":
        return _history()
    if act == "launch":
        return _launch(payload)
    if act == "launch_mark":
        return _launch_mark(payload)
    if act == "disconnect":
        if payload.get("network") not in sp.DISCONNECT_KEYS:
            return {"error": f"unknown network: {payload.get('network')}"}
        # Not gated: removing a stored sign-in is always allowed.
        return sp.disconnect(payload["network"])
    refused = _not_enabled(CONNECT_ACTIONS[act]) if act in CONNECT_ACTIONS else None
    if refused:
        return refused
    if act == "x_connect_start":
        return sp.x_connect_start()
    if act == "x_connect_finish":
        return sp.x_connect_finish(payload.get("pin", ""))
    if act == "bsky_connect":
        return sp.bsky_connect(payload.get("handle", ""), payload.get("app_password", ""))
    if act == "mastodon_connect_start":
        return sp.mastodon_connect_start(payload.get("instance", ""))
    if act == "mastodon_connect_finish":
        return sp.mastodon_connect_finish(payload.get("code", ""))
    return {"error": f"unknown action: {act}"}


def read_payload(argv, stdin):
    """(action, payload) from the command line; the payload from @stdin when
    argv says "-". A secret-bearing action whose payload came in argv is
    refused (ValueError) — it was already visible to other users."""
    act = argv[1] if len(argv) > 1 else "channels"
    src = argv[2] if len(argv) > 2 else None
    if src == "-":
        text = stdin.read()
        return act, (json.loads(text) if text.strip() else {})
    if src is not None and act in SECRET_ACTIONS:
        raise ValueError(f"{act} reads its payload from stdin only (\"-\"); argv is visible to other users")
    return act, (json.loads(src) if src else {})


def main(argv, stdin, stdout):
    try:
        act, payload = read_payload(argv, stdin)
        if not isinstance(payload, dict):
            raise ValueError("the payload must be a JSON object")
        out = handle_action(act, payload)
    except Exception as e:  # the app shows {error}; never a bare traceback
        out = {"error": f"{type(e).__name__}: {e}"}
    print(json.dumps(out, indent=2, default=str, ensure_ascii=False), file=stdout)


if __name__ == "__main__":
    main(sys.argv, sys.stdin, sys.stdout)
