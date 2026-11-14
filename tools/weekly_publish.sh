#!/usr/bin/env bash
# Weekly brief, run on the owner's machine (systemd user timer, see
# tools/install-weekly-timer.sh). Keys never leave this machine: the SEC
# contact and the X keys are read from the local keyring.
#
# Default (the timer): BUILD THIS WEEK'S DRAFT for review — nothing is
# published. Review and correct it in finterm › SOCIAL › BRIEFS, then
# Publish now or Schedule; the finterm-briefs-tick timer publishes what is
# scheduled and posts to X once the page is live.
#
#   tools/weekly_publish.sh           build the draft (an edited one is kept)
#   tools/weekly_publish.sh --auto    unattended, as before review existed:
#                                     build, publish, post to X when live
#
# Drafts and publishing: app/scripts/brief_drafts.py (a private worktree of
# origin/main; your own checkout is untouched). Safe to rerun: one issue per
# week, and nothing is ever posted twice.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}/finterm"
LOG="$DATA/logs/weekly-publish.log"
DRAFTS="$REPO/app/scripts/brief_drafts.py"
mkdir -p "$(dirname "$LOG")"
exec > >(tee -a "$LOG") 2>&1
echo "=== $(date -Is) weekly brief ${1:-draft}"

if [ "${1:-}" != "--auto" ]; then
    python3 "$DRAFTS" build-weekly --if-unedited
    id=$(python3 -c "import sys;sys.path.insert(0,'$REPO/app/scripts');import brief_drafts as b;print(b.weekly_id())")
    if command -v notify-send >/dev/null 2>&1; then
        notify-send "finterm: weekly brief ready for review" \
            "Open SOCIAL › BRIEFS to review, correct and publish ($id)." || true
    fi
    exit 0
fi

# --auto: an incomplete week is retried (the app may be mid-scan), never published.
ok=
id=$(python3 -c "import sys;sys.path.insert(0,'$REPO/app/scripts');import brief_drafts as b;print(b.weekly_id())")
for attempt in 1 2 3; do
    # A failed build is retried like a refused publish; "already published"
    # exits 0, so a rerun after success finishes at once.
    if python3 -c "import sys;sys.path.insert(0,'$REPO/app/scripts');import brief_drafts as b;m=b.load('$id') or {};sys.exit(0 if m.get('state')=='published' else 1)"; then
        echo "$id is already published"; ok=1; break
    fi
    if python3 "$DRAFTS" build-weekly && python3 "$DRAFTS" publish "$id"; then ok=1; break; fi
    if [ "$attempt" -lt 3 ]; then
        echo "attempt $attempt did not publish; retrying in 10 minutes"
        sleep 600
    fi
done
[ -n "$ok" ] || { echo "not published this run; the timer retries next time"; exit 1; }

# Post to X once the page shows this version (tick does nothing until then).
python3 - "$REPO" "$id" <<'PY'
import sys
sys.path.insert(0, sys.argv[1] + "/app/scripts")
import brief_drafts as b
m = b.load(sys.argv[2]); m["post_x"] = True; b.store(m)
PY
for i in $(seq 1 60); do
    python3 "$DRAFTS" tick
    if python3 -c "import sys;sys.path.insert(0,'$REPO/app/scripts');import brief_drafts as b;m=b.load('$id');sys.exit(0 if m.get('x_ref') else 1)"; then
        exit 0
    fi
    sleep 30
done
echo "$id did not go live within 30 minutes; the tick timer posts it once it does"
exit 1
