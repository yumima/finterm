#!/bin/sh
# apply-update.sh — finterm's in-place updater for Linux (AppImage) and macOS
# (.app bundle). The app writes this script out (it is embedded in the binary)
# and starts it detached when it quits with a verified update staged:
#
#   apply-update.sh --pid PID --kind appimage|macapp --staged PATH --target PATH
#                   --previous PATH --status FILE --from VER --to VER [--relaunch]
#                   [--profile NAME]   (relaunched as finterm --profile NAME)
#                   [--lock FILE]      (the app's record of this helper; removed
#                                       when done, before any relaunch)
#
#   appimage  STAGED is the verified new AppImage, in TARGET's directory (same
#             filesystem): TARGET is hard-linked to PREVIOUS, then STAGED is
#             renamed over TARGET — one atomic step; a running copy keeps its
#             old inode.
#   macapp    STAGED is the verified DMG (or, for tests, an unpacked .app):
#             its bundle is copied next to TARGET, TARGET moves to PREVIOUS and
#             the new bundle takes its place; quarantine is cleared.
#   rollback  (--kind appimage-rollback | macapp-rollback) puts PREVIOUS back.
#
# It waits for PID to exit (up to 120 s; past that it changes nothing), writes
# a one-line JSON STATUS the next launch reports, and with --relaunch starts
# TARGET again. Nothing here touches the user's data, which lives elsewhere.

set -u
PID="" KIND="" STAGED="" TARGET="" PREVIOUS="" STATUS="" FROM="" TO="" RELAUNCH=0 PROFILE="" LOCK=""
while [ $# -gt 0 ]; do
    case "$1" in
        --pid) PID="$2"; shift 2 ;;
        --kind) KIND="$2"; shift 2 ;;
        --staged) STAGED="$2"; shift 2 ;;
        --target) TARGET="$2"; shift 2 ;;
        --previous) PREVIOUS="$2"; shift 2 ;;
        --status) STATUS="$2"; shift 2 ;;
        --from) FROM="$2"; shift 2 ;;
        --to) TO="$2"; shift 2 ;;
        --relaunch) RELAUNCH=1; shift ;;
        --profile) PROFILE="$2"; shift 2 ;;
        --lock) LOCK="$2"; shift 2 ;;
        *) shift ;;
    esac
done

json_escape() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'; }

status() { # ok(true|false) message
    [ -n "$LOCK" ] && rm -f "$LOCK"
    [ -n "$STATUS" ] || return 0
    mkdir -p "$(dirname "$STATUS")" 2>/dev/null
    printf '{"ok":%s,"kind":"%s","from":"%s","to":"%s","profile":"%s","message":"%s","at":"%s"}\n' \
        "$1" "$(json_escape "$KIND")" "$(json_escape "$FROM")" "$(json_escape "$TO")" "$(json_escape "$PROFILE")" \
        "$(json_escape "$2")" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$STATUS.tmp" && mv -f "$STATUS.tmp" "$STATUS"
}

fail() { status false "$1"; relaunch; exit 1; }

relaunch() {
    [ "$RELAUNCH" -eq 1 ] || return 0
    case "$KIND" in
        appimage*)
            [ -x "$TARGET" ] || return 0
            if [ -n "$PROFILE" ]; then (nohup "$TARGET" --profile "$PROFILE" >/dev/null 2>&1 &)
            else (nohup "$TARGET" >/dev/null 2>&1 &); fi ;;
        macapp*)
            [ -d "$TARGET" ] || return 0
            if [ -n "$PROFILE" ]; then open -n "$TARGET" --args --profile "$PROFILE"
            else open -n "$TARGET"; fi ;;
    esac
}

[ -n "$KIND" ] && [ -n "$TARGET" ] && [ -n "$PREVIOUS" ] || { status false "missing arguments"; exit 2; }

# ── Wait for the app to be gone (it holds the files we replace) ─────────────
if [ -n "$PID" ]; then
    waited=0
    while kill -0 "$PID" 2>/dev/null; do
        if [ "$waited" -ge 600 ]; then # 600 × 0.2 s = 120 s
            status false "finterm (pid $PID) did not exit; nothing was changed"
            exit 1
        fi
        sleep 0.2
        waited=$((waited + 1))
    done
fi

case "$KIND" in
appimage)
    [ -f "$STAGED" ] || fail "the staged update is missing: $STAGED"
    [ -f "$TARGET" ] || fail "the AppImage to replace is missing: $TARGET"
    [ "$TARGET" != "$PREVIOUS" ] || fail "the kept previous version cannot replace itself"
    chmod 0755 "$STAGED" || fail "cannot make the update executable"
    # Keep the running version for a rollback: a second name for the same
    # file (no copy), else a copy — complete before the older one goes.
    rm -f "$PREVIOUS.tmp"
    ln "$TARGET" "$PREVIOUS.tmp" 2>/dev/null || cp -p "$TARGET" "$PREVIOUS.tmp" || {
        rm -f "$PREVIOUS.tmp"
        fail "cannot keep the previous version"
    }
    rm -f "$PREVIOUS"
    mv -f "$PREVIOUS.tmp" "$PREVIOUS" || fail "cannot keep the previous version"
    mv -f "$STAGED" "$TARGET" || fail "cannot replace $TARGET"
    ;;
appimage-rollback)
    [ -f "$PREVIOUS" ] || fail "no previous version to restore"
    mv -f "$PREVIOUS" "$TARGET" || fail "cannot restore $TARGET"
    ;;
macapp)
    [ -e "$STAGED" ] || fail "the staged update is missing: $STAGED"
    NEW="$TARGET.new"
    rm -rf "$NEW"
    if [ -d "$STAGED" ]; then
        SRC="$STAGED" # an unpacked bundle (tests)
        cp -R "$SRC" "$NEW" || fail "cannot copy the new bundle"
    else
        MNT="$(mktemp -d /tmp/finterm-update.XXXXXX)" || fail "cannot create a mount point"
        hdiutil attach -nobrowse -readonly -mountpoint "$MNT" "$STAGED" >/dev/null || fail "cannot open the disk image"
        SRC="$(find "$MNT" -maxdepth 1 -name '*.app' -type d | head -1)"
        if [ -z "$SRC" ]; then
            hdiutil detach "$MNT" -quiet
            fail "the disk image holds no application"
        fi
        ditto "$SRC" "$NEW"
        rc=$?
        hdiutil detach "$MNT" -quiet
        rmdir "$MNT" 2>/dev/null
        [ "$rc" -eq 0 ] || fail "cannot copy the new bundle"
    fi
    command -v xattr >/dev/null 2>&1 && xattr -dr com.apple.quarantine "$NEW" 2>/dev/null
    rm -rf "$PREVIOUS"
    mv "$TARGET" "$PREVIOUS" || { rm -rf "$NEW"; fail "cannot move the current bundle aside"; }
    if ! mv "$NEW" "$TARGET"; then
        mv "$PREVIOUS" "$TARGET"
        fail "cannot put the new bundle in place"
    fi
    ;;
macapp-rollback)
    [ -d "$PREVIOUS" ] || fail "no previous version to restore"
    rm -rf "$TARGET.failed"
    mv "$TARGET" "$TARGET.failed" 2>/dev/null
    mv "$PREVIOUS" "$TARGET" || { mv "$TARGET.failed" "$TARGET" 2>/dev/null; fail "cannot restore $TARGET"; }
    rm -rf "$TARGET.failed"
    ;;
*)
    status false "unknown kind: $KIND"
    exit 2
    ;;
esac

[ -n "$STAGED" ] && [ -f "$STAGED" ] && case "$KIND" in macapp) rm -f "$STAGED" ;; esac
case "$KIND" in *-rollback) status true "restored" ;; *) status true "applied" ;; esac
relaunch
exit 0
