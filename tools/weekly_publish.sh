#!/usr/bin/env bash
# Weekly brief, run on the owner's machine (systemd user timer, see
# tools/install-weekly-timer.sh). Keys never leave this machine: the SEC
# contact and the X keys are read from the local keyring.
#
# Default (the timer): BUILD THIS WEEK'S DRAFT for review — nothing is
# published. Review and correct it in finterm › NEWS › What's On › WEEKLY
# BRIEF, then Publish now or Schedule; the finterm-briefs-tick timer publishes
# what is scheduled and posts to the chosen platforms once the page is live.
# With "publish automatically" chosen there, the timer runs --auto itself.
#
#   tools/weekly_publish.sh           build the draft (an edited one is kept)
#   tools/weekly_publish.sh --auto    unattended: build, publish, post to the
#                                     chosen platforms once live
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

# The owner's choice (NEWS › What's On › WEEKLY BRIEF): "publish" runs the
# unattended path every week; "draft" (default) builds for review only.
mode="${1:-}"
if [ -z "$mode" ] && [ "$(python3 "$DRAFTS" auto-mode)" = publish ]; then
    mode=--auto
fi

alarm() { # a failure nobody sees is a missing week
    command -v notify-send >/dev/null 2>&1 && notify-send --urgency=critical "finterm: the weekly brief failed" \
        "See $LOG, or generate it in NEWS › What's On › WEEKLY BRIEF." || true
}
trap alarm ERR

if [ "$mode" != "--auto" ]; then
    python3 "$DRAFTS" build-weekly --if-unedited
    id=$(python3 "$DRAFTS" weekly-id)
    if command -v notify-send >/dev/null 2>&1; then
        notify-send "finterm: weekly brief ready for review" \
            "Open NEWS › What's On › WEEKLY BRIEF to review, correct and publish ($id)." || true
    fi
    exit 0
fi

# --auto: an incomplete week is retried (the app may be mid-scan), never published.
ok=
id=$(python3 "$DRAFTS" weekly-id)
for attempt in 1 2 3; do
    # A failed build is retried like a refused publish; "already published"
    # exits 0, so a rerun after success finishes at once.
    if [ "$(python3 "$DRAFTS" state "$id")" = published ]; then
        echo "$id is already published"; ok=1; break
    fi
    if python3 "$DRAFTS" build-weekly && python3 "$DRAFTS" publish "$id"; then ok=1; break; fi
    if [ "$attempt" -lt 3 ]; then
        echo "attempt $attempt did not publish; retrying in 10 minutes"
        sleep 600
    fi
done
[ -n "$ok" ] || { echo "not published this run; the timer retries next time"; alarm; exit 1; }

# Post to the chosen platforms once the page shows this version (tick does
# nothing until then; each platform is posted once, ever).
python3 "$DRAFTS" post-to "$id"
for i in $(seq 1 60); do
    python3 "$DRAFTS" tick
    if python3 "$DRAFTS" posted-all "$id"; then
        exit 0
    fi
    sleep 30
done
echo "$id did not go live within 30 minutes; the tick timer posts it once it does"
exit 1
