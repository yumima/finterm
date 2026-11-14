#!/usr/bin/env bash
# Install (or update) the systemd user timer that runs tools/weekly_publish.sh
# every Sunday morning. Persistent: a Sunday missed while the computer was
# off runs at the next start. Sunday, not Saturday: the week-ahead macro list
# comes from the calendar feed's Sunday–Saturday "this week" file — its "next
# week" file is a 404 on Saturdays — and the insider window (7 days back) holds
# the same Monday–Friday filings either day, in the same issue. A catch-up on
# Monday–Wednesday files under the week just ended (ISSUE_LAG_DAYS in
# app/scripts/weekly_brief_core.py), so it never takes next Sunday's issue.
# Remove with:
#   systemctl --user disable --now finterm-weekly.timer
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UNIT="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
mkdir -p "$UNIT"
cat > "$UNIT/finterm-weekly.service" <<UNIT
[Unit]
Description=finterm weekly brief: publish the issue and announce it
Wants=network-online.target
After=network-online.target

[Service]
Type=oneshot
ExecStart=$REPO/tools/weekly_publish.sh
TimeoutStartSec=3h
UNIT
cat > "$UNIT/finterm-weekly.timer" <<UNIT
[Unit]
Description=finterm weekly brief, Sundays

[Timer]
OnCalendar=Sun *-*-* 09:00:00
Persistent=true
RandomizedDelaySec=15min

[Install]
WantedBy=timers.target
UNIT
systemctl --user daemon-reload
systemctl --user enable --now finterm-weekly.timer
systemctl --user list-timers finterm-weekly.timer --no-pager
