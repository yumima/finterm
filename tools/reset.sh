#!/usr/bin/env bash
# Reset FinceptTerminal persistent state to factory defaults.
#
# Use this when the app's Qt config has gotten into a bad state — duplicate
# floating windows, orphaned toolbars, broken dock layouts, stuck PIN screen,
# etc. Almost always fixable by clearing ~/.config/Fincept alone (keeps your
# Python venv intact, so first relaunch is fast).
#
# Backup directories are kept with a timestamp suffix so you can roll back
# any reset with `--restore <timestamp>`.
#
# Usage:
#   reset.sh                Qt config only (~1s; preserves Python venv)
#   reset.sh --window-only  Strip ONLY window/toolbar/dock-layout state from
#                           Fincept.conf. Preserves portfolio data, workspaces,
#                           theme, component usage stats. Safe to run on every
#                           launch — start.sh does this by default.
#   reset.sh --full         Qt config + Python venv + caches
#                           (next launch re-provisions ~120 pip packages, 5–15 min)
#   reset.sh --list         show available backups
#   reset.sh --restore N    restore the backup tagged with timestamp N
#   reset.sh --help

set -uo pipefail

CONFIG_DIR="$HOME/.config/Fincept"
SHARE_DIR="$HOME/.local/share/com.fincept.terminal"
TS=$(date +%s)

usage() {
    sed -n 's/^# \{0,1\}//p' "$0" | sed -n '/^Reset FinceptTerminal/,/^Usage:/p; /^[[:space:]]*reset\.sh/p; /^$/q'
}

kill_app() {
    pkill -9 -f FinceptTerminal 2>/dev/null || true
    pkill -9 -f "yfinance_data.py --daemon" 2>/dev/null || true
    sleep 1
    if pgrep -f FinceptTerminal >/dev/null 2>&1; then
        echo "WARN: FinceptTerminal still running after kill — proceeding anyway." >&2
    fi
}

backup_one() {
    local src="$1" label="$2"
    if [[ -d "$src" ]]; then
        mv "$src" "${src}.bak.${TS}"
        echo "  $label backed up → ${src}.bak.${TS}"
    else
        echo "  $label not present, skipping"
    fi
}

cmd_qt_only() {
    echo "=== Killing app ==="
    kill_app
    echo "=== Backing up Qt config ==="
    backup_one "$CONFIG_DIR" "Qt config"
    echo
    echo "Reset complete (Qt config only)."
    echo "Python venv preserved → first launch will be fast."
    echo "Relaunch: finterm"
}

# Surgical: strip ONLY the window/toolbar/dock-layout state from
# FinceptTerminal.conf. Leaves portfolio, workspaces, theme, component
# usage stats, and every other setting alone. Safe to run on every launch.
cmd_window_only() {
    local conf="$CONFIG_DIR/FinceptTerminal.conf"
    if [[ ! -f "$conf" ]]; then
        return 0  # nothing to do (first launch — no config yet)
    fi
    # Remove top-level window_count and window_ids keys, plus every
    # [window_<n>] section (Qt state save/restore data).
    # The [window_<n>] section in QSettings .ini files runs from its
    # header to the next "[" header or EOF. Use awk for that.
    local tmp
    tmp=$(mktemp)
    awk '
        BEGIN { in_window_section = 0 }
        /^\[window_[0-9]+\]/ { in_window_section = 1; next }
        /^\[/ { in_window_section = 0 }
        in_window_section { next }
        # Drop top-level multi-window enumeration keys too.
        /^window_count=/ { next }
        /^window_ids=/   { next }
        { print }
    ' "$conf" > "$tmp"
    mv "$tmp" "$conf"
}

cmd_full() {
    echo "=== Killing app ==="
    kill_app
    echo "=== Backing up Qt config + share dir ==="
    backup_one "$CONFIG_DIR" "Qt config"
    backup_one "$SHARE_DIR" "Share dir (Python venv, caches, workspaces)"
    echo
    echo "Full reset complete."
    echo "WARNING: Python venv was wiped. Next launch re-provisions ~120 pip"
    echo "packages — expect 5–15 minutes of 'PythonSetup' activity before the"
    echo "auth screen appears."
    echo "Relaunch: finterm"
}

cmd_list() {
    local found=0 d
    echo "Available backups:"
    for d in "${CONFIG_DIR}".bak.* "${SHARE_DIR}".bak.*; do
        if [[ -d "$d" ]]; then
            echo "  $(stat -c '%y  %n' "$d" 2>/dev/null || echo "$d")"
            found=1
        fi
    done
    [[ $found -eq 0 ]] && echo "  (none)"
}

cmd_restore() {
    local id="$1"
    local cbak="${CONFIG_DIR}.bak.${id}"
    local sbak="${SHARE_DIR}.bak.${id}"
    if [[ ! -d "$cbak" && ! -d "$sbak" ]]; then
        echo "Error: no backup with timestamp '$id' (try --list)." >&2
        exit 2
    fi
    echo "=== Killing app ==="
    kill_app
    echo "=== Replacing live state with backup '$id' ==="
    if [[ -d "$cbak" ]]; then
        [[ -d "$CONFIG_DIR" ]] && rm -rf "$CONFIG_DIR"
        mv "$cbak" "$CONFIG_DIR"
        echo "  Qt config restored from $cbak"
    fi
    if [[ -d "$sbak" ]]; then
        [[ -d "$SHARE_DIR" ]] && rm -rf "$SHARE_DIR"
        mv "$sbak" "$SHARE_DIR"
        echo "  Share dir restored from $sbak"
    fi
    echo
    echo "Restore complete. Relaunch: finterm"
}

case "${1:-}" in
    ""|"--config-only")
        cmd_qt_only
        ;;
    "--window-only")
        cmd_window_only
        ;;
    "--full")
        cmd_full
        ;;
    "--list")
        cmd_list
        ;;
    "--restore")
        if [[ -z "${2:-}" ]]; then
            echo "Error: --restore requires a backup timestamp ID. See --list." >&2
            exit 2
        fi
        cmd_restore "$2"
        ;;
    "--help"|"-h")
        usage
        ;;
    *)
        echo "Unknown option: $1" >&2
        usage
        exit 2
        ;;
esac
