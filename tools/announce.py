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
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app", "scripts"))
from social_post import (  # noqa: E402 — the shared posting code
    NETWORKS, announce, credentials, keyring_set, x_connect_finish, x_connect_start,
)


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
    """Sign the POSTING account in to the app (see social_post.x_connect_*)."""
    if pin is None:
        r = x_connect_start()
        if "error" in r:
            print(r["error"] + " Run: announce.py --store-keys x")
            return 1
        print(f"Open this link in a browser signed in to X as the POSTING account:\n\n   {r['url']}\n")
        print("Check the account name shown, click Authorize app, then run:\n\n"
              "   python3 tools/announce.py --authorize-x PIN")
        return 0
    r = x_connect_finish(pin)
    if "error" in r:
        print(r["error"])
        return 1
    print(f"Stored. Posts will go out as @{r['account']}.")
    return 0


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

    with open(os.path.join(args.site, "_data", "announce.json"), encoding="utf-8") as f:
        msg = json.load(f)
    issue = msg.get("issue") or msg["date"]  # one issue per ISO week
    texts = {name: msg[name] for name, _, _ in NETWORKS if name in msg}
    _, failed = announce(issue, texts, state_path=args.state, dry_run=args.dry_run,
                         log=lambda line: print(line, file=sys.stderr if "FAILED" in line or "error:" in line else sys.stdout))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
