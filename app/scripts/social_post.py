"""Posting to X (OAuth 1.0a), Bluesky and Mastodon, with a durable record of
what was posted so an issue never goes out twice.

Credentials come from the environment, else finterm's keyring (never
uploaded anywhere). Shared by the app's Social screen (social_app.py) and
tools/announce.py. The record of what was posted is one profile-independent
file, locked while it is read, posted against and written.
"""
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

import finterm_keyring

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


def x_post(text, env, issue=None, trust_duplicate=True):
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
        # X refuses an identical repeat. A generated issue text is unique (it
        # names the issue's date and companies), so for it this means it went
        # out and only the record was lost. An edited text proves nothing.
        if trust_duplicate and "HTTP 403" in str(e) and "duplicate" in str(e).lower():
            return "already-posted (X rejected a duplicate)"
        raise
    return out["data"]["id"]


# ---------------------------------------------------------------- Bluesky
def _link_facets(text):
    """Bluesky doesn't auto-link: each URL needs a facet with UTF-8 byte offsets."""
    facets = []
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


def bsky_post(text, env, issue=None, **_):
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
def mastodon_post(text, env, issue, **_):
    base = env["MASTODON_BASE_URL"].rstrip("/")
    out = _req("POST", f"{base}/api/v1/statuses",
               headers={"Authorization": f"Bearer {env['MASTODON_TOKEN']}",
                        # Server-side dedupe too, if a retry lands after a lost response.
                        "Idempotency-Key": f"finterm-brief-{issue}"},
               data={"status": text, "visibility": "public", "language": "en"})
    return out.get("url") or out.get("id")



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


# ── X sign-in of the POSTING account (OAuth 1.0a PIN flow) ───────────────
# The app's consumer keys belong to the developer account; the posting
# account approves the app in a browser and X shows it a PIN. Only that
# account's token is stored; nothing secret is ever returned.

def _x_signed_post(creds, url, token="", token_secret="", extra=None):
    oauth = {"oauth_consumer_key": creds["X_API_KEY"], "oauth_nonce": secrets.token_hex(16),
             "oauth_signature_method": "HMAC-SHA1", "oauth_timestamp": str(int(time.time())),
             "oauth_version": "1.0", **(extra or {})}
    if token:
        oauth["oauth_token"] = token
    auth = oauth1_header("POST", url, oauth, creds["X_API_SECRET"], token_secret)
    r = urllib.request.Request(url, data=b"", method="POST", headers={"Authorization": auth, "User-Agent": UA})
    try:
        with urllib.request.urlopen(r, timeout=30) as resp:
            return dict(urllib.parse.parse_qsl(resp.read().decode()))
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"HTTP {e.code}: {e.read()[:300].decode('utf-8', 'replace')}") from None


def x_connect_start():
    """{url} to open as the posting account, or {error}."""
    creds = credentials(("X_API_KEY", "X_API_SECRET"))
    if len(creds) < 2:
        return {"error": "The X app's consumer keys (X_API_KEY, X_API_SECRET) are not in the keyring yet."}
    try:
        req = _x_signed_post(creds, "https://api.x.com/oauth/request_token", extra={"oauth_callback": "oob"})
    except RuntimeError as e:
        return {"error": f"X refused to start the sign-in ({e}); check the app's keys and permissions."}
    if "oauth_token" not in req:
        return {"error": "X's answer had no sign-in token."}
    # Kept for the finish step; a request token is short-lived and useless alone.
    keyring_set("X_PENDING_REQUEST", f"{req['oauth_token']}:{req['oauth_token_secret']}")
    return {"url": f"https://api.x.com/oauth/authorize?oauth_token={req['oauth_token']}"}


def x_connect_finish(pin):
    """{account} once the PIN is exchanged for the posting account's token."""
    creds = credentials(("X_API_KEY", "X_API_SECRET"))
    pending = keyring_get("X_PENDING_REQUEST") or ""
    if len(creds) < 2 or ":" not in pending:
        return {"error": "No X sign-in in progress; start it again."}
    rtok, rsec = pending.split(":", 1)
    try:
        acc = _x_signed_post(creds, "https://api.x.com/oauth/access_token", rtok, rsec,
                             {"oauth_verifier": str(pin).strip()})
    except RuntimeError as e:
        return {"error": f"X did not accept the PIN ({e}); it may be wrong or expired — start again."}
    if "oauth_token" not in acc:
        return {"error": "X's answer had no access token; start again."}
    keyring_set("X_ACCESS_TOKEN", acc["oauth_token"])
    keyring_set("X_ACCESS_SECRET", acc["oauth_token_secret"])
    keyring_set("X_POSTING_ACCOUNT", acc.get("screen_name", ""))
    keyring_set("X_PENDING_REQUEST", "-")
    return {"account": acc.get("screen_name", "")}


def default_state_path():
    """One record per machine, NOT per profile: the app (any profile) and the
    weekly timer must read and write the same file."""
    try:
        import finterm_paths

        d = finterm_paths.data_root() / "social"
    except Exception:
        d = os.path.join(os.path.expanduser("~/.local/share/finterm"), "social")
    os.makedirs(d, exist_ok=True)
    return os.path.join(str(d), "announced.json")


class _Locked:
    """Exclusive lock on the record for a whole load → post → save, so the
    timer and the app's button can't both post or overwrite each other."""

    def __init__(self, path):
        self.path = f"{path}.lock"

    def __enter__(self):
        import fcntl

        self.f = open(self.path, "a")
        fcntl.flock(self.f, fcntl.LOCK_EX)
        return self

    def __exit__(self, *exc):
        import fcntl

        fcntl.flock(self.f, fcntl.LOCK_UN)
        self.f.close()


def load_state(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return {}
    except ValueError:
        # A record cut short (crash, full disk) must not block announcing for
        # good; X's duplicate refusal and the Bluesky feed check still stop a
        # second post of an issue that did go out.
        print(f"note: {path} was unreadable; starting a fresh record", file=sys.stderr)
        return {}


def save(path, state):
    tmp = f"{path}.tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump({k: v for k, v in sorted(state.items()) if v}, f, indent=2)
        f.write("\n")
    os.replace(tmp, path)  # atomic: the record is either old or new, never half


def posted(issue, state_path=None):
    """{network: reference} already posted for @issue."""
    return dict(load_state(state_path or default_state_path()).get(issue, {}))


def announce(issue, texts, *, only=None, state_path=None, dry_run=False, log=print, generated=True):
    """Post @texts ({network: text}) for @issue to each configured network
    (or just those in @only). @generated: the texts are the issue's own
    (so X's duplicate refusal means "already posted"). Returns
    (results {network: status}, failed)."""
    state_path = state_path or default_state_path()
    with _Locked(state_path):
        return _announce_locked(issue, texts, only, state_path, dry_run, log, generated)


def _announce_locked(issue, texts, only, state_path, dry_run, log, generated):
    state = load_state(state_path)
    done = state.setdefault(issue, {})
    results, failed, configured = {}, False, 0
    for name, keys, post in NETWORKS:
        if only and name not in only:
            continue
        if name in done:
            configured += 1
            results[name] = f"already posted ({done[name]})"
            log(f"{name}: already posted for {issue} ({done[name]})")
            continue
        try:
            creds = credentials(keys)
        except finterm_keyring.KeyringUnavailable as e:
            failed = True
            results[name] = f"keyring unavailable: {e}"
            log(f"{name}: FAILED: could not read the keyring ({e}); is it unlocked?")
            continue
        missing = [k for k in keys if k not in creds]
        if missing:
            results[name] = "not configured"
            log(f"{name}: skipped (not configured: {', '.join(missing)})")
            continue
        configured += 1
        text = texts[name]
        if dry_run:
            who = ""
            if name == "x":
                try:
                    acct = keyring_get("X_POSTING_ACCOUNT")
                except finterm_keyring.KeyringUnavailable:
                    acct = None
                who = f" as @{acct}" if acct else ""
            results[name] = "dry run"
            log(f"--- {name}{who} (dry run) ---\n{text}\n")
            continue
        try:
            ref = post(text, creds, issue, **({"trust_duplicate": generated} if name == "x" else {}))
            done[name] = ref
            results[name] = f"posted {ref}"
            log(f"{name}: posted {ref}")
            save(state_path, state)  # at once: a later failure must not lose this
        except Exception as e:  # one network failing must not stop the others
            failed = True
            results[name] = f"failed: {e}"
            log(f"{name}: FAILED: {e}")
    if not configured and not failed:
        log("error: no network is configured; nothing was announced")
        failed = True
    return results, failed
