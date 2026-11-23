#!/bin/sh
# tests/updater/test_apply_update.sh — the in-place update helper
# (src/services/updater/scripts/apply-update.sh) on throwaway files:
#   - an AppImage is replaced in one rename, the old one kept as PREVIOUS,
#     and a rollback puts it back;
#   - the helper waits for the app's pid, and changes nothing when it never exits;
#   - a macOS bundle (an unpacked .app stands in for the DMG) is swapped, the
#     old one kept, and rolled back;
#   - every outcome is written to the status file.
# Usage: test_apply_update.sh <path to apply-update.sh>

set -u
HELPER="${1:?usage: test_apply_update.sh apply-update.sh}"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
FAILS=0
check() { # description, command...
    desc="$1"; shift
    if "$@"; then echo "ok   - $desc"; else echo "FAIL - $desc"; FAILS=$((FAILS + 1)); fi
}
status_ok() { grep -q "\"ok\":$1" "$T/status.json"; }

# ── AppImage ────────────────────────────────────────────────────────────────
mkdir -p "$T/apps"
printf 'old' > "$T/apps/finterm.AppImage"; chmod +x "$T/apps/finterm.AppImage"
printf 'new' > "$T/apps/.finterm-update.AppImage"
ino_old=$(ls -i "$T/apps/finterm.AppImage" | awk '{print $1}')
sh "$HELPER" --kind appimage --staged "$T/apps/.finterm-update.AppImage" --target "$T/apps/finterm.AppImage" \
    --previous "$T/apps/.finterm-previous.AppImage" --status "$T/status.json" --from 1.0.0 --to 1.0.1
check "appimage: target is the new version" [ "$(cat "$T/apps/finterm.AppImage")" = new ]
check "appimage: target is executable" [ -x "$T/apps/finterm.AppImage" ]
check "appimage: staged file is gone (renamed, not copied)" [ ! -e "$T/apps/.finterm-update.AppImage" ]
check "appimage: previous version kept" [ "$(cat "$T/apps/.finterm-previous.AppImage")" = old ]
check "appimage: previous is the old file itself (hard link)" \
    [ "$(ls -i "$T/apps/.finterm-previous.AppImage" | awk '{print $1}')" = "$ino_old" ]
check "appimage: status ok" status_ok true
check "appimage: status names the versions" grep -q '"from":"1.0.0","to":"1.0.1"' "$T/status.json"

sh "$HELPER" --kind appimage-rollback --target "$T/apps/finterm.AppImage" \
    --previous "$T/apps/.finterm-previous.AppImage" --status "$T/status.json"
check "appimage rollback: old version back" [ "$(cat "$T/apps/finterm.AppImage")" = old ]
check "appimage rollback: status ok" status_ok true
check "appimage rollback: says restored" grep -q '"message":"restored"' "$T/status.json"

# Missing staged file: nothing changes, status says why.
sh "$HELPER" --kind appimage --staged "$T/apps/.nope" --target "$T/apps/finterm.AppImage" \
    --previous "$T/apps/.finterm-previous.AppImage" --status "$T/status.json"
check "appimage, no staged file: target untouched" [ "$(cat "$T/apps/finterm.AppImage")" = old ]
check "appimage, no staged file: status not ok" status_ok false

# ── Waits for the app to exit ───────────────────────────────────────────────
printf 'newer' > "$T/apps/.finterm-update.AppImage"
sleep 1 &
APP=$!
sh "$HELPER" --pid "$APP" --kind appimage --staged "$T/apps/.finterm-update.AppImage" \
    --target "$T/apps/finterm.AppImage" --previous "$T/apps/.finterm-previous.AppImage" --status "$T/status.json"
check "waits for the pid, then applies" [ "$(cat "$T/apps/finterm.AppImage")" = newer ]
check "the app had exited first" sh -c "! kill -0 $APP 2>/dev/null"

# ── macOS bundle (an unpacked .app stands in for the DMG) ───────────────────
mkdir -p "$T/Applications/finterm.app/Contents/MacOS" "$T/stage/finterm.app/Contents/MacOS"
printf 'old' > "$T/Applications/finterm.app/Contents/MacOS/finterm"
printf 'new' > "$T/stage/finterm.app/Contents/MacOS/finterm"
sh "$HELPER" --kind macapp --staged "$T/stage/finterm.app" --target "$T/Applications/finterm.app" \
    --previous "$T/Applications/.finterm-previous.app" --status "$T/status.json"
check "macapp: bundle is the new version" [ "$(cat "$T/Applications/finterm.app/Contents/MacOS/finterm")" = new ]
check "macapp: previous bundle kept" [ "$(cat "$T/Applications/.finterm-previous.app/Contents/MacOS/finterm")" = old ]
check "macapp: no .new left behind" [ ! -e "$T/Applications/finterm.app.new" ]
check "macapp: status ok" status_ok true
sh "$HELPER" --kind macapp-rollback --target "$T/Applications/finterm.app" \
    --previous "$T/Applications/.finterm-previous.app" --status "$T/status.json"
check "macapp rollback: old bundle back" [ "$(cat "$T/Applications/finterm.app/Contents/MacOS/finterm")" = old ]
check "macapp rollback: status ok" status_ok true

# ── An app that never exits: nothing changes (short wait for the test) ──────
# (The helper waits 120 s; here a pid that stays alive is killed after the
# helper has been given 2 s, and the helper is expected to still be waiting.)
printf 'x' > "$T/apps/.finterm-update.AppImage"
sleep 30 &
STUCK=$!
sh "$HELPER" --pid "$STUCK" --kind appimage --staged "$T/apps/.finterm-update.AppImage" \
    --target "$T/apps/finterm.AppImage" --previous "$T/apps/.finterm-previous.AppImage" --status "$T/status.json" &
H=$!
sleep 2
check "while the app runs, the target is untouched" [ "$(cat "$T/apps/finterm.AppImage")" = newer ]
kill "$H" "$STUCK" 2>/dev/null
wait 2>/dev/null

echo "$FAILS failure(s)"
[ "$FAILS" -eq 0 ]
