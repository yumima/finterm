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
import re
import secrets
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

import finterm_keyring

UA = "finterm-weekly-brief/1.0 (+https://github.com/yumima/finterm)"

# THE switch for which networks the app may publish to or connect: the weekly
# brief's platform list (NEWS › What's On › WEEKLY BRIEF) offers these, greys
# out any other ("coming soon"), and social_app.py refuses their publish /
# connect / mark actions. Enabling one is adding it here. Each still posts
# only once its account is connected (credentials in the keyring). The weekly timer (tools/announce.py) is not gated: it
# posts wherever credentials were stored by hand.
ENABLED_NETWORKS = ("x", "bluesky", "mastodon")
MANUAL_NETWORKS = ("reddit", "hackernews")  # posted by hand; see launch_posts.py


class HttpError(RuntimeError):
    """A non-2xx answer; str() keeps the "<METHOD> <url> -> HTTP n: body" shape."""

    def __init__(self, method, url, code, body):
        self.code, self.body = code, body
        super().__init__(f"{method} {url} -> HTTP {code}: {body}")


def reason(e):
    """One readable line for a failed request — the server's own message when
    it sent one, never a traceback."""
    if isinstance(e, HttpError):
        msg = ""
        try:
            j = json.loads(e.body)
            if isinstance(j, dict):
                msg = j.get("error_description") or j.get("message") or j.get("error") or ""
        except ValueError:
            pass
        return f"HTTP {e.code}" + (f": {str(msg)[:200]}" if msg else "")
    if isinstance(e, urllib.error.URLError):
        return f"could not reach the server ({e.reason})"
    if isinstance(e, TimeoutError):
        return "the server did not answer in time"
    if isinstance(e, ValueError):
        return "the server's answer was not what was expected"
    return str(e) or type(e).__name__


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
        raise HttpError(method, url, e.code, e.read()[:500].decode("utf-8", "replace")) from None


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


BSKY_XRPC = "https://bsky.social/xrpc"


def bsky_post(text, env, issue=None, again=False, **_):
    base = BSKY_XRPC
    sess = _req("POST", f"{base}/com.atproto.server.createSession",
                json_body={"identifier": env["BSKY_HANDLE"], "password": env["BSKY_APP_PASSWORD"]})
    auth = {"Authorization": f"Bearer {sess['accessJwt']}"}
    # Bluesky has no idempotency key: if a post carrying this issue's link is
    # already on the account, the record of it was lost — don't post again.
    facets = _link_facets(text)
    url = next((f["features"][0]["uri"] for f in facets), None)
    if url and not again:
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
def mastodon_post(text, env, issue, again=False, **_):
    base = env["MASTODON_BASE_URL"].rstrip("/")
    out = _req("POST", f"{base}/api/v1/statuses",
               headers={"Authorization": f"Bearer {env['MASTODON_TOKEN']}",
                        # Server-side dedupe too, if a retry lands after a lost response.
                        "Idempotency-Key": f"finterm-brief-{issue}" + (f"-{secrets.token_hex(8)}" if again else "")},
               data={"status": text, "visibility": "public", "language": "en"})
    return out.get("url") or out.get("id")



_configured = {}  # network -> bool, for this process (a drafts listing asks per draft)


def is_configured(network):
    """True / False — or None when the keyring can't be read (locked): unknown
    must not read as "not connected"."""
    if network in _configured:
        return _configured[network]
    keys = next((k for n, k, _ in NETWORKS if n == network), None)
    if keys is None:
        return False
    try:
        _configured[network] = all(k in credentials(keys) for k in keys)
    except finterm_keyring.KeyringUnavailable:
        return None
    return _configured[network]


def keyring_get(name):
    """None if absent; raises finterm_keyring.KeyringUnavailable if it could
    not look — "locked keyring" must not read as "not configured"."""
    return finterm_keyring.get(name)


def keyring_set(name, value):
    _configured.clear()
    finterm_keyring.set(name, value)


def keyring_delete(name):
    _configured.clear()
    finterm_keyring.delete(name)


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
    keyring_delete("X_PENDING_REQUEST")
    return {"account": acc.get("screen_name", "")}


# ── Bluesky: handle + APP password ──────────────────────────────────────────
# An app password (Settings › App passwords) can post but cannot change the
# account, and can be revoked on its own. The account's real password is
# refused before anything is sent: its shape is not an app password's.
BSKY_APP_PASSWORDS_URL = "https://bsky.app/settings/app-passwords"
_BSKY_APP_PASSWORD = re.compile(r"^[a-z0-9]{4}(-[a-z0-9]{4}){3}$", re.IGNORECASE)


def _jwt_scope(jwt):
    """The "scope" claim of an (unverified) JWT, or "" if unreadable."""
    try:
        part = jwt.split(".")[1]
        return json.loads(base64.urlsafe_b64decode(part + "=" * (-len(part) % 4))).get("scope", "")
    except Exception:  # noqa: BLE001 — unreadable is "unknown", not an error
        return ""


def bsky_connect(handle, app_password):
    """{account} once Bluesky accepts the handle + app password; only then
    are they stored."""
    h = str(handle or "").strip().lstrip("@")
    pw = str(app_password or "").strip()
    if not h or any(c.isspace() for c in h):
        return {"error": "Enter the account's handle, e.g. name.bsky.social."}
    if not _BSKY_APP_PASSWORD.match(pw):
        return {"error": "That is not an app password (they look like xxxx-xxxx-xxxx-xxxx). Create one at "
                         f"{BSKY_APP_PASSWORDS_URL} — never enter the account's own password here."}
    try:
        sess = _req("POST", f"{BSKY_XRPC}/com.atproto.server.createSession",
                    json_body={"identifier": h, "password": pw})
    except Exception as e:  # noqa: BLE001
        return {"error": f"Bluesky did not accept the handle and app password ({reason(e)})."}
    if not isinstance(sess, dict) or not sess.get("accessJwt"):
        return {"error": "Bluesky's answer had no session; nothing was stored."}
    if _jwt_scope(sess["accessJwt"]) == "com.atproto.access":
        # Full-access session: that was the account password after all.
        return {"error": f"That password has full access to the account — create an app password at "
                         f"{BSKY_APP_PASSWORDS_URL} instead. Nothing was stored."}
    account = sess.get("handle") or h
    keyring_set("BSKY_HANDLE", account)
    keyring_set("BSKY_APP_PASSWORD", pw)
    return {"account": account}


# ── Mastodon: OAuth with an out-of-band code ────────────────────────────────
# finterm registers itself on the reader's instance, the reader approves it in
# a browser, and the instance shows a code to paste back — like X's PIN.
MASTODON_SCOPES = "write:statuses read:accounts"
MASTODON_OOB = "urn:ietf:wg:oauth:2.0:oob"
_HOST = re.compile(r"^(?=.{1,253}$)([a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z][a-z0-9-]{1,62}$")


def mastodon_instance(text):
    """"https://host" from "mastodon.social", "https://mastodon.social/about",
    "@me@mastodon.social"; None if it names no host (or isn't https)."""
    s = str(text or "").strip()
    if "://" in s:
        try:
            p = urllib.parse.urlsplit(s)
            if p.scheme.lower() != "https" or p.port not in (None, 443) or p.username:
                return None
        except ValueError:
            return None
        host = p.hostname or ""
    else:
        s = s.lstrip("@")
        host = s.rsplit("@", 1)[-1].split("/", 1)[0]
    try:
        host = host.strip().rstrip(".").encode("idna").decode("ascii").lower()
    except UnicodeError:
        return None
    return f"https://{host}" if _HOST.match(host) else None


def _host(base):
    return urllib.parse.urlsplit(base).hostname or base


def mastodon_connect_start(instance):
    """{url} to approve finterm on the instance, or {error}."""
    base = mastodon_instance(instance)
    if not base:
        return {"error": "That is not a Mastodon server address — enter its domain, e.g. mastodon.social "
                         "(https only)."}
    import weekly_brief_core as wb

    try:
        app = _req("POST", f"{base}/api/v1/apps",
                   data={"client_name": "finterm", "redirect_uris": MASTODON_OOB,
                         "scopes": MASTODON_SCOPES, "website": wb.SITE_URL})
    except Exception as e:  # noqa: BLE001
        return {"error": f"{_host(base)} did not register finterm ({reason(e)}). Is it a Mastodon server?"}
    cid = app.get("client_id") if isinstance(app, dict) else None
    csec = app.get("client_secret") if isinstance(app, dict) else None
    if not cid or not csec:
        return {"error": f"{_host(base)}'s answer had no app credentials. Is it a Mastodon server?"}
    # Kept for the finish step (a new start replaces it); never returned.
    keyring_set("MASTODON_PENDING_APP", json.dumps({"base": base, "client_id": cid, "client_secret": csec}))
    query = urllib.parse.urlencode({"client_id": cid, "response_type": "code",
                                    "scope": MASTODON_SCOPES, "redirect_uri": MASTODON_OOB})
    return {"url": f"{base}/oauth/authorize?{query}", "instance": base}


def mastodon_connect_finish(code):
    """{account} ("@user@host") once the code is exchanged and the token reads
    the account back; only then is anything stored."""
    code = str(code or "").strip()
    if not code:
        return {"error": "Paste the authorization code the instance showed."}
    try:
        pending = json.loads(keyring_get("MASTODON_PENDING_APP") or "")
    except ValueError:
        pending = None
    if not isinstance(pending, dict) or not all(pending.get(k) for k in ("base", "client_id", "client_secret")):
        return {"error": "No Mastodon sign-in in progress; start it again."}
    base, host = pending["base"], _host(pending["base"])
    try:
        tok = _req("POST", f"{base}/oauth/token",
                   data={"grant_type": "authorization_code", "code": code, "client_id": pending["client_id"],
                         "client_secret": pending["client_secret"], "redirect_uri": MASTODON_OOB,
                         "scope": MASTODON_SCOPES})
    except Exception as e:  # noqa: BLE001
        return {"error": f"{host} did not accept the code ({reason(e)}); it may be wrong or already used — "
                         "start again."}
    token = tok.get("access_token") if isinstance(tok, dict) else None
    if not token:
        return {"error": f"{host}'s answer had no access token; start again."}
    granted = set(str(tok.get("scope") or MASTODON_SCOPES).split())
    if not granted & {"write:statuses", "write"}:
        return {"error": f"{host} did not grant permission to post; start again and approve it."}
    try:
        me = _req("GET", f"{base}/api/v1/accounts/verify_credentials", headers={"Authorization": f"Bearer {token}"})
    except Exception as e:  # noqa: BLE001
        return {"error": f"Signed in, but {host} would not show the account ({reason(e)}); nothing was stored."}
    user = me.get("username") if isinstance(me, dict) else None
    if not user:
        return {"error": f"{host}'s account answer had no username; nothing was stored."}
    account = f"@{user}@{host}"
    keyring_set("MASTODON_BASE_URL", base)
    keyring_set("MASTODON_TOKEN", token)
    keyring_set("MASTODON_POSTING_ACCOUNT", account)
    # The app's own credentials, so Disconnect can revoke the token.
    keyring_set("MASTODON_CLIENT", json.dumps({"client_id": pending["client_id"],
                                               "client_secret": pending["client_secret"]}))
    keyring_delete("MASTODON_PENDING_APP")
    return {"account": account}


# ── Disconnect ──────────────────────────────────────────────────────────────
# The posting account's sign-in only: X's consumer keys (X_API_KEY/SECRET)
# belong to the developer app and stay.
DISCONNECT_KEYS = {
    "x": ("X_ACCESS_TOKEN", "X_ACCESS_SECRET", "X_POSTING_ACCOUNT", "X_PENDING_REQUEST"),
    "bluesky": ("BSKY_HANDLE", "BSKY_APP_PASSWORD"),
    "mastodon": ("MASTODON_TOKEN", "MASTODON_BASE_URL", "MASTODON_POSTING_ACCOUNT", "MASTODON_CLIENT",
                 "MASTODON_PENDING_APP"),
}
REVOKE_HINT = {
    "x": "To revoke it at X too: x.com › Settings › Security and account access › Connected apps.",
    "bluesky": f"To revoke it at Bluesky too, delete the app password at {BSKY_APP_PASSWORDS_URL}.",
    "mastodon": "To revoke it on the instance: Preferences › Account › Authorized apps.",
}


def _mastodon_revoke():
    """Best effort: True if the instance revoked the stored token."""
    try:
        base, token = keyring_get("MASTODON_BASE_URL"), keyring_get("MASTODON_TOKEN")
        client = json.loads(keyring_get("MASTODON_CLIENT") or "")
        if not base or not token or not isinstance(client, dict):
            return False
        _req("POST", f"{base.rstrip('/')}/oauth/revoke", timeout=15,
             data={"client_id": client["client_id"], "client_secret": client["client_secret"], "token": token})
        return True
    except Exception:  # noqa: BLE001 — the local removal goes ahead regardless
        return False


def disconnect(network):
    """Remove @network's stored sign-in from the keyring. {network, note}."""
    keys = DISCONNECT_KEYS.get(network)
    if not keys:
        return {"error": f"unknown network: {network}"}
    try:
        revoked = network == "mastodon" and _mastodon_revoke()
        for k in keys:
            keyring_delete(k)
    except finterm_keyring.KeyringUnavailable as e:
        return {"error": f"Could not reach the keyring ({e}); is it unlocked? Nothing may have been removed."}
    note = "The instance revoked finterm's token." if revoked else REVOKE_HINT[network]
    # Only the sign-in itself counts: X's app keys in the environment can't post alone.
    post_keys = next(k for n, k, _ in NETWORKS if n == network)
    if any(os.environ.get(k) for k in post_keys if k in keys):
        note += " Its credentials are also set in the environment, which still applies."
    return {"network": network, "note": note}


# ── Launch posts (manual Reddit / HN) ───────────────────────────────────────
LAUNCH_KEY = "launch"  # in the same record as the issues: {target_id: {url, at, network, community}}


def launch_records(state_path=None):
    return dict(load_state(state_path or default_state_path()).get(LAUNCH_KEY, {}))


def record_launch(target_id, record, *, replace=False, state_path=None):
    """Record a hand-made launch post. Returns the record already there (and
    leaves it) unless @replace; None once written."""
    state_path = state_path or default_state_path()
    with _Locked(state_path):
        # Strict, unlike load_state(): rewriting an unreadable record with
        # only this entry would erase what was posted for every issue, and
        # the timer would then post them again.
        try:
            with open(state_path, encoding="utf-8") as f:
                state = json.load(f)
        except FileNotFoundError:
            state = {}
        except ValueError:
            raise RuntimeError(f"{state_path} is unreadable; not overwriting it — repair or remove it first") from None
        launch = state.setdefault(LAUNCH_KEY, {})
        if target_id in launch and not replace:
            return launch[target_id]
        launch[target_id] = record
        save(state_path, state)
    return None


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
        import file_lock

        self.f = file_lock.open_lock_file(self.path)
        file_lock.lock(self.f)
        return self

    def __exit__(self, *exc):
        import file_lock

        file_lock.unlock(self.f)
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


def announce(issue, texts, *, only=None, state_path=None, dry_run=False, log=print, generated=True, again=()):
    """Post @texts ({network: text}) for @issue to each configured network
    (or just those in @only). @generated: the texts are the issue's own
    (so X's duplicate refusal means "already posted"). @again: networks the
    owner asked to post to once more (the record then holds the new post;
    there a duplicate refusal is a failure). Returns
    (results {network: status}, failed)."""
    state_path = state_path or default_state_path()
    with _Locked(state_path):
        return _announce_locked(issue, texts, only, state_path, dry_run, log, generated, again)


def _announce_locked(issue, texts, only, state_path, dry_run, log, generated, again=()):
    state = load_state(state_path)
    done = state.setdefault(issue, {})
    results, failed, configured = {}, False, 0
    for name, keys, post in NETWORKS:
        if only and name not in only:
            continue
        if name in done and name not in again:
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
            once_more = name in again
            ref = post(text, creds, issue, **({"trust_duplicate": generated and not once_more} if name == "x"
                                              else {"again": True} if once_more else {}))
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
