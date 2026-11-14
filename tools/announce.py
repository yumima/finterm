#!/usr/bin/env python3
"""Post this week's brief to the social networks whose credentials are set.

Runs on the owner's machine (tools/weekly_publish.sh), never in CI: the keys
stay in the local keyring with finterm's other keys and are never uploaded.
Each credential is read from the environment if set, else from the keyring
(service "finterm", attribute "finterm-key", same as the app). A network is
skipped unless all of its credentials are present:

  X        X_API_KEY, X_API_SECRET, X_ACCESS_TOKEN, X_ACCESS_SECRET   (OAuth 1.0a user context)
  Bluesky  BSKY_HANDLE, BSKY_APP_PASSWORD                             (an app password, not the login one)
  Mastodon MASTODON_BASE_URL, MASTODON_TOKEN                          (token with write:statuses)

Store keys once with hidden input (they never appear on screen or in history):

  python3 tools/announce.py --store-keys x      # the app's X_API_KEY / X_API_SECRET
  python3 tools/announce.py --authorize-x       # prints the approval link for the posting account
  python3 tools/announce.py --authorize-x PIN   # finishes: stores that account's token

Idempotent: what was posted is recorded in finterm's data folder
(weekly/announced.json), keyed by issue (ISO week); X rejects a duplicate and
Bluesky's feed is checked, so a lost record does not cause a second post.

Usage: announce.py [--site site] [--dry-run] | --store-keys {x,bluesky,mastodon}
"""
import argparse
import base64
import hashlib
import hmac
import json
import os
import secrets
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

UA = "finterm-weekly-brief/1.0 (+https://github.com/yumima/finterm)"


def _req(method, url, *, headers=None, data=None, json_body=None, timeout=30):
    h = {"User-Agent": UA, **(headers or {})}
    body = None
    if json_body is not None:
        body = json.dumps(json_body).encode()
        h["Content-Type"] = "application/json"
    elif data is not None:
        body = urllib.parse.urlencode(data).encode()
        h["Content-Type"] = "application/x-www-form-urlencoded"
    r = urllib.request.Request(url, data=body, headers=h, method=method)
    try:
        with urllib.request.urlopen(r, timeout=timeout) as resp:
            return json.loads(resp.read() or b"{}")
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"{method} {url} -> HTTP {e.code}: {e.read()[:500].decode('utf-8', 'replace')}") from None


# ---------------------------------------------------------------- X (Twitter)
def _pct(s):
    return urllib.parse.quote(str(s), safe="~-._")


def oauth1_header(method, url, oauth, consumer_secret, token_secret, extra_params=None):
    """RFC 5849 HMAC-SHA1 Authorization header. extra_params: query/form params
    that are signed too (a JSON body is not)."""
    oauth = dict(oauth)
    allp = {**oauth, **(extra_params or {})}
    params = "&".join(f"{k}={v}" for k, v in sorted((_pct(k), _pct(v)) for k, v in allp.items()))
    base = "&".join([method.upper(), _pct(url), _pct(params)])
    key = f"{_pct(consumer_secret)}&{_pct(token_secret)}"
    oauth["oauth_signature"] = base64.b64encode(hmac.new(key.encode(), base.encode(), hashlib.sha1).digest()).decode()
    return "OAuth " + ", ".join(f'{_pct(k)}="{_pct(v)}"' for k, v in sorted(oauth.items()))


def x_post(text, env, issue=None):
    url = "https://api.x.com/2/tweets"
    oauth = {
        "oauth_consumer_key": env["X_API_KEY"],
        "oauth_nonce": secrets.token_hex(16),
        "oauth_signature_method": "HMAC-SHA1",
        "oauth_timestamp": str(int(time.time())),
        "oauth_token": env["X_ACCESS_TOKEN"],
        "oauth_version": "1.0",
    }
    auth = oauth1_header("POST", url, oauth, env["X_API_SECRET"], env["X_ACCESS_SECRET"])
    try:
        out = _req("POST", url, headers={"Authorization": auth}, json_body={"text": text})
    except RuntimeError as e:
        # X refuses an identical repeat. Each issue's text is unique (it names
        # the issue's date and companies), so this means it already went out
        # and only the record of it was lost.
        if "HTTP 403" in str(e) and "duplicate" in str(e).lower():
            return "already-posted (X rejected a duplicate)"
        raise
    return out["data"]["id"]


# ---------------------------------------------------------------- Bluesky
def _link_facets(text):
    """Bluesky doesn't auto-link: each URL needs a facet with UTF-8 byte offsets."""
    facets, raw = [], text.encode("utf-8")
    pos = 0
    while True:
        i = text.find("https://", pos)
        if i < 0:
            return facets
        j = i
        while j < len(text) and not text[j].isspace():
            j += 1
        u = text[i:j].rstrip(".,)")
        start = len(text[:i].encode("utf-8"))
        facets.append({"index": {"byteStart": start, "byteEnd": start + len(u.encode("utf-8"))},
                       "features": [{"$type": "app.bsky.richtext.facet#link", "uri": u}]})
        pos = j


def bsky_post(text, env, issue=None):
    base = "https://bsky.social/xrpc"
    sess = _req("POST", f"{base}/com.atproto.server.createSession",
                json_body={"identifier": env["BSKY_HANDLE"], "password": env["BSKY_APP_PASSWORD"]})
    auth = {"Authorization": f"Bearer {sess['accessJwt']}"}
    # Bluesky has no idempotency key: if a post carrying this issue's link is
    # already on the account, the record of it was lost — don't post again.
    facets = _link_facets(text)
    url = next((f["features"][0]["uri"] for f in facets), None)
    if url:
        feed = _req("GET", f"{base}/app.bsky.feed.getAuthorFeed?" +
                    urllib.parse.urlencode({"actor": sess["did"], "limit": 30}), headers=auth)
        for item in feed.get("feed", []):
            post = item.get("post", {})
            if url in post.get("record", {}).get("text", ""):
                return post.get("uri", "already-posted")
    rec = {"$type": "app.bsky.feed.post", "text": text, "langs": ["en"],
           "createdAt": time.strftime("%Y-%m-%dT%H:%M:%S.000Z", time.gmtime())}
    if facets:
        rec["facets"] = facets
    out = _req("POST", f"{base}/com.atproto.repo.createRecord",
               headers=auth,
               json_body={"repo": sess["did"], "collection": "app.bsky.feed.post", "record": rec})
    return out["uri"]


# ---------------------------------------------------------------- Mastodon
def mastodon_post(text, env, issue):
    base = env["MASTODON_BASE_URL"].rstrip("/")
    out = _req("POST", f"{base}/api/v1/statuses",
               headers={"Authorization": f"Bearer {env['MASTODON_TOKEN']}",
                        # Server-side dedupe too, if a retry lands after a lost response.
                        "Idempotency-Key": f"finterm-brief-{issue}"},
               data={"status": text, "visibility": "public", "language": "en"})
    return out.get("url") or out.get("id")


sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app", "scripts"))
import finterm_keyring  # noqa: E402


def keyring_get(name):
    """None if absent; raises finterm_keyring.KeyringUnavailable if it could
    not look — "locked keyring" must not read as "not configured"."""
    return finterm_keyring.get(name)


def keyring_set(name, value):
    finterm_keyring.set(name, value)


def credentials(keys):
    out = {}
    for k in keys:
        v = os.environ.get(k) or keyring_get(k)
        if v:
            out[k] = v.strip()
    return out


NETWORKS = [
    ("x", ("X_API_KEY", "X_API_SECRET", "X_ACCESS_TOKEN", "X_ACCESS_SECRET"), x_post),
    ("bluesky", ("BSKY_HANDLE", "BSKY_APP_PASSWORD"), bsky_post),
    ("mastodon", ("MASTODON_BASE_URL", "MASTODON_TOKEN"), mastodon_post),
]


def store_keys(network):
    import getpass

    keys = next(k for n, k, _ in NETWORKS if n == network)
    print(f"Paste each {network} credential and press Enter (input is hidden; empty keeps the current one).")
    for k in keys:
        v = getpass.getpass(f"  {k}: ").strip()
        if v:
            keyring_set(k, v)
    have = credentials(keys)
    print(f"{network}: {len(have)} of {len(keys)} credentials stored in the keyring"
          + ("" if len(have) == len(keys) else f" — missing: {', '.join(k for k in keys if k not in have)}"))


def authorize_x(pin=None):
    """OAuth 1.0a PIN flow: lets the POSTING account (not the developer
    account that owns the app) grant the app access. Needs X_API_KEY and
    X_API_SECRET (the app's consumer keys) already in the keyring; stores the
    posting account's X_ACCESS_TOKEN / X_ACCESS_SECRET without showing them."""
    creds = credentials(("X_API_KEY", "X_API_SECRET"))
    if len(creds) < 2:
        print("Store the app's consumer keys first: announce.py --store-keys x "
              "(paste X_API_KEY and X_API_SECRET; leave the access token prompts empty).")
        return 1

    def signed_post(url, token="", token_secret="", extra=None):
        oauth = {"oauth_consumer_key": creds["X_API_KEY"], "oauth_nonce": secrets.token_hex(16),
                 "oauth_signature_method": "HMAC-SHA1", "oauth_timestamp": str(int(time.time())),
                 "oauth_version": "1.0", **(extra or {})}
        if token:
            oauth["oauth_token"] = token
        auth = oauth1_header("POST", url, oauth, creds["X_API_SECRET"], token_secret)
        r = urllib.request.Request(url, data=b"", method="POST",
                                   headers={"Authorization": auth, "User-Agent": UA})
        try:
            with urllib.request.urlopen(r, timeout=30) as resp:
                return dict(urllib.parse.parse_qsl(resp.read().decode()))
        except urllib.error.HTTPError as e:
            raise SystemExit(f"{url} -> HTTP {e.code}: {e.read()[:300].decode('utf-8', 'replace')}")

    if pin is None:
        req = signed_post("https://api.x.com/oauth/request_token", extra={"oauth_callback": "oob"})
        # Kept for the finish step; a request token is short-lived and useless alone.
        keyring_set("X_PENDING_REQUEST", f"{req['oauth_token']}:{req['oauth_token_secret']}")
        print("Open this link in a browser signed in to X as the POSTING account:\n")
        print(f"   https://api.x.com/oauth/authorize?oauth_token={req['oauth_token']}\n")
        print("Check the account name shown, click Authorize app, then run:\n")
        print("   python3 tools/announce.py --authorize-x PIN")
        return 0
    pending = keyring_get("X_PENDING_REQUEST") or ""
    if ":" not in pending:
        print("No sign-in in progress: run announce.py --authorize-x first.")
        return 1
    rtok, rsec = pending.split(":", 1)
    acc = signed_post("https://api.x.com/oauth/access_token", rtok, rsec, {"oauth_verifier": pin.strip()})
    keyring_set("X_ACCESS_TOKEN", acc["oauth_token"])
    keyring_set("X_ACCESS_SECRET", acc["oauth_token_secret"])
    keyring_set("X_POSTING_ACCOUNT", acc.get("screen_name", ""))
    keyring_set("X_PENDING_REQUEST", "-")
    print(f"\nStored. Posts will go out as @{acc.get('screen_name', '?')}.")
    return 0


def default_state_path():
    try:
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app", "scripts"))
        import finterm_paths

        d = finterm_paths.user_root() / "files" / "weekly"
    except Exception:
        d = os.path.expanduser("~/.local/share/finterm/weekly")
    os.makedirs(d, exist_ok=True)
    return os.path.join(str(d), "announced.json")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--site", default="site")
    ap.add_argument("--state", help="where posted issues are recorded (default: finterm's files/weekly/)")
    ap.add_argument("--dry-run", action="store_true", help="print what would be posted; post nothing")
    ap.add_argument("--store-keys", choices=[n for n, _, _ in NETWORKS],
                    help="save a network's credentials to the keyring (hidden prompt), then exit")
    ap.add_argument("--authorize-x", nargs="?", const="", metavar="PIN",
                    help="sign the posting account in to the app: run once to get the approval link, "
                         "then again with the PIN it shows; stores that account's access token")
    args = ap.parse_args()
    if args.authorize_x is not None:
        return authorize_x(args.authorize_x or None)
    if args.store_keys:
        store_keys(args.store_keys)
        return 0

    src = os.path.join(args.site, "_data", "announce.json")
    state_path = args.state or default_state_path()
    msg = json.load(open(src, encoding="utf-8"))
    issue = msg.get("issue") or msg["date"]  # one issue per ISO week
    try:
        with open(state_path, encoding="utf-8") as f:
            state = json.load(f)
    except FileNotFoundError:
        state = {}
    except ValueError:
        # A record cut short (crash, full disk) must not block announcing for
        # good; X's duplicate refusal and the Bluesky feed check still stop a
        # second post of an issue that did go out.
        print(f"note: {state_path} was unreadable; starting a fresh record", file=sys.stderr)
        state = {}
    done = state.setdefault(issue, {})

    failed = False
    configured = 0
    for name, keys, post in NETWORKS:
        if name in done:
            configured += 1
            print(f"{name}: already posted for {issue} ({done[name]})")
            continue
        try:
            creds = credentials(keys)
        except finterm_keyring.KeyringUnavailable as e:
            failed = True
            print(f"{name}: FAILED: could not read the keyring ({e}); is it unlocked?", file=sys.stderr)
            continue
        missing = [k for k in keys if k not in creds]
        if missing:
            print(f"{name}: skipped (not configured: {', '.join(missing)})")
            continue
        configured += 1
        text = msg[name]
        if args.dry_run:
            who = ""
            if name == "x":
                acct = keyring_get("X_POSTING_ACCOUNT")
                who = f" as @{acct}" if acct else ""
            print(f"--- {name}{who} (dry run) ---\n{text}\n")
            continue
        try:
            ref = post(text, creds, issue)
            done[name] = ref
            print(f"{name}: posted {ref}")
            save(state_path, state)  # at once: a later failure must not lose this
        except Exception as e:  # one network failing must not stop the others
            failed = True
            print(f"{name}: FAILED: {e}", file=sys.stderr)

    if not configured and not failed:
        # Nothing to post to is a setup problem, not a quiet success.
        print("error: no network is configured; nothing was announced", file=sys.stderr)
        failed = True

    if not args.dry_run:
        save(state_path, state)
    return 1 if failed else 0


def save(path, state):
    tmp = f"{path}.tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump({k: v for k, v in sorted(state.items()) if v}, f, indent=2)
        f.write("\n")
    os.replace(tmp, path)  # atomic: the record is either old or new, never half


if __name__ == "__main__":
    sys.exit(main())
