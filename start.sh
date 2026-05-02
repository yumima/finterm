#!/usr/bin/env bash
# Single-command launcher for FinceptTerminal + the localhost stub.
#
# Starts the stub server only if it isn't already running, then launches
# the Qt binary in the foreground. Safe to invoke from anywhere — paths
# are resolved relative to this script's location.
#
# Coexists with the systemd user unit at tools/systemd/fincept-stub.service:
# if the unit has the stub up already, this script defers to it.

set -euo pipefail

# Resolve the script's real path so `~/bin/finterm` (a symlink to this file)
# still finds the repo. dirname on $BASH_SOURCE alone returns the symlink dir.
REPO_DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
STUB_PY="$REPO_DIR/tools/local_stub/server.py"
FINTERM_BIN="$REPO_DIR/fincept-qt/build/linux-release/FinceptTerminal"
STUB_LOG_DIR="${XDG_CACHE_HOME:-$HOME/.cache}"
STUB_LOG="$STUB_LOG_DIR/fincept-stub.log"
STUB_HEALTH_URL="http://127.0.0.1:8765/health"

if [[ ! -x "$FINTERM_BIN" ]]; then
    echo "FinceptTerminal binary not found at $FINTERM_BIN — build it first." >&2
    exit 1
fi

# Strip Qt window/toolbar/dock-layout state from FinceptTerminal.conf so
# every launch starts with a clean default layout. Avoids the duplicate-
# floating-window bug where dragging the toolbar out, or any prior state
# corruption, persists across launches and steals keyboard focus from
# the PIN screen. Surgical — preserves portfolio data, workspaces, theme,
# component-usage stats, and every other setting. Set FINCEPT_KEEP_WINDOW=1
# to skip if you want Qt to remember your dragged layout across launches.
if [[ "${FINCEPT_KEEP_WINDOW:-0}" != "1" ]]; then
    "$REPO_DIR/tools/reset.sh" --window-only
fi

# Ensure the stub is up. pgrep matches the process whether it was started
# by systemd, by hand, or by a previous run of this script.
if ! pgrep -f "tools/local_stub/server.py" >/dev/null; then
    mkdir -p "$STUB_LOG_DIR"
    echo "Starting localhost stub (logs: $STUB_LOG)"
    nohup python3 "$STUB_PY" >>"$STUB_LOG" 2>&1 &
    disown || true

    # Wait briefly for the port to come up so finterm doesn't race the stub.
    for _ in $(seq 1 30); do
        if curl -fs --max-time 0.2 "$STUB_HEALTH_URL" >/dev/null 2>&1; then
            break
        fi
        sleep 0.1
    done
fi

exec "$FINTERM_BIN" "$@"
