#!/usr/bin/env bash
# Weekly brief, run on the owner's machine (systemd user timer, see
# tools/install-weekly-timer.sh). Keys never leave this machine: the SEC
# contact and the X keys are read from the local keyring.
#
#   1. a private git worktree of origin/main (your own checkout is untouched)
#   2. tools/weekly_brief.py --site  — refuses to publish an incomplete week
#   3. commit + push only the issue files → GitHub Pages rebuilds the site
#   4. wait until the issue page is live, then tools/announce.py posts it
#
# Safe to rerun: one issue per ISO week, and announce.py never posts twice.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}/finterm"
WT="$DATA/site-publish"
LOG="$DATA/logs/weekly-publish.log"
mkdir -p "$(dirname "$LOG")"
exec > >(tee -a "$LOG") 2>&1
echo "=== $(date -Is) weekly publish"

git -C "$REPO" fetch --quiet origin main
if [ ! -d "$WT/.git" ] && [ ! -f "$WT/.git" ]; then
    git -C "$REPO" worktree add --detach "$WT" origin/main
fi
# A previous run that died mid-rebase must not wedge every later one.
git -C "$WT" rebase --abort >/dev/null 2>&1 || true
git -C "$WT" checkout --quiet --detach
git -C "$WT" reset --quiet --hard origin/main

# The app may be mid-scan (the store is locked); a short wait and retry beats
# skipping the week. An incomplete week exits 2 and publishes nothing.
ok=
for attempt in 1 2 3; do
    if python3 "$WT/tools/weekly_brief.py" --site "$WT/site"; then ok=1; break; fi
    if [ "$attempt" -lt 3 ]; then
        echo "attempt $attempt did not complete; retrying in 10 minutes"
        sleep 600
    fi
done
[ -n "$ok" ] || { echo "not published this run; the timer retries next time"; exit 1; }

cd "$WT"
git add -A site/_posts site/insider-cluster-buys.md site/_data/announce.json
if ! git diff --cached --quiet; then
    issue=$(python3 -c "import json;print(json.load(open('site/_data/announce.json'))['issue'])")
    git commit --quiet -m "brief: $issue"
    pushed=
    for i in 1 2 3; do
        if git pull --quiet --rebase origin main && git push --quiet origin HEAD:main; then pushed=1; break; fi
        git rebase --abort >/dev/null 2>&1 || true
        sleep 10
    done
    if [ -z "$pushed" ]; then
        echo "error: could not push the issue (credentials? conflict?); nothing announced"
        exit 1
    fi
fi

# Wait for THIS version of the page (its stamp), not an earlier deploy of the
# same URL, before telling anyone about it.
read -r url stamp < <(python3 -c "import json;d=json.load(open('site/_data/announce.json'));print(d['url'], d['stamp'])")
for i in $(seq 1 60); do
    if curl -sf "$url" 2>/dev/null | grep -q "stamp $stamp"; then
        python3 "$WT/tools/announce.py" --site "$WT/site"
        exit $?
    fi
    sleep 30
done
echo "$url did not show this run's version within 30 minutes; not announcing (rerun to announce)"
exit 1
