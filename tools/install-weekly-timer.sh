#!/usr/bin/env bash
# Install (or update) the systemd user timer that runs tools/weekly_publish.sh
# every Saturday morning. Persistent: a Saturday missed while the computer was
# off runs at the next start. Remove with:
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
Description=finterm weekly brief, Saturdays

[Timer]
OnCalendar=Sat *-*-* 09:00:00
Persistent=true
RandomizedDelaySec=15min

[Install]
WantedBy=timers.target
UNIT
systemctl --user daemon-reload
systemctl --user enable --now finterm-weekly.timer
systemctl --user list-timers finterm-weekly.timer --no-pager
