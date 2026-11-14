"""The Social screen's backend: channels, the weekly brief as a draft source,
and publishing — the same code and the same "already posted" record as the
owner's tools (tools/weekly_publish.sh, tools/announce.py), so the button and
the weekly timer can never post the same issue twice.

    social_app.py <action> '<json payload>'   → one JSON object on stdout

Actions:
  channels                     each network: configured?, posting account
  brief                        the PUBLISHED issue (issue.json beside the
                               page): rows, per-network texts, what was
                               posted, whether its page is live
  publish    {network, issue, text}
                               post one text (the UI confirms first); only
                               for the published, live issue, and never one
                               already posted to that network
  history                      everything posted, newest issue first
  x_connect_start / x_connect_finish {pin}
                               PIN sign-in of the posting X account

Credentials are read from finterm's keyring and never returned.
"""
import json
import sys
import time
import urllib.request
from datetime import date

import finterm_keyring
import social_post as sp
import weekly_brief_core as wb



def _channels():
    out = []
    for name, keys, _ in sp.NETWORKS:
        acct, err, ok = "", "", False
        try:
            ok = len(sp.credentials(keys)) == len(keys)
            if ok and name == "x":
                acct = sp.keyring_get("X_POSTING_ACCOUNT") or ""
            elif ok and name == "bluesky":
                acct = sp.keyring_get("BSKY_HANDLE") or ""
        except finterm_keyring.KeyringUnavailable as e:
            ok, err = False, str(e)
        limit, url_cost = wb.NETWORK_LIMITS[name]
        out.append({"network": name, "configured": ok, "account": acct, "error": err,
                    "limit": limit, "url_cost": url_cost, "manual": False})
    # Communities that are posted to by hand: their APIs need approval and
    # their rules forbid recurring links. The UI copies text + opens submit.
    out.append({"network": "reddit", "configured": True, "manual": True, "account": ""})
    out.append({"network": "hackernews", "configured": True, "manual": True, "account": ""})
    return {"channels": out}


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
        return {"error": "No published issue found yet — the weekly run publishes one each Saturday."}
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
    return {"history": [{"issue": k, "posts": v} for k, v in sorted(state.items(), reverse=True)]}


def handle_action(act, payload):
    if act == "channels":
        return _channels()
    if act == "brief":
        return _brief(payload)
    if act == "publish":
        return _publish(payload)
    if act == "history":
        return _history()
    if act == "x_connect_start":
        return sp.x_connect_start()
    if act == "x_connect_finish":
        return sp.x_connect_finish(payload.get("pin", ""))
    return {"error": f"unknown action: {act}"}


if __name__ == "__main__":
    act = sys.argv[1] if len(sys.argv) > 1 else "channels"
    payload = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    try:
        out = handle_action(act, payload)
    except Exception as e:  # the app shows {error}; never a bare traceback
        out = {"error": f"{type(e).__name__}: {e}"}
    print(json.dumps(out, indent=2, default=str, ensure_ascii=False))
