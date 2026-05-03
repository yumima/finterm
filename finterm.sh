#!/usr/bin/env bash
# finterm.sh — one-stop launcher / builder / state utility for finterm.
#
# Subcommands:
#   start    (default) — launch the Qt app + the localhost stub
#   build              — build the Qt binary via cmake
#   reset              — reset persistent state (window-only / full / etc.)
#   stop               — stop the running app and stub
#   status             — print what's running
#   help               — print this help
#
# Safe to invoke from anywhere; paths resolve relative to this script's
# real location (so a symlink at ~/bin/finterm still finds the repo).

set -uo pipefail

# ── Paths ────────────────────────────────────────────────────────────────────

REPO_DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
APP_DIR="$REPO_DIR/fincept-qt"
STUB_PY="$REPO_DIR/tools/local_stub/server.py"
BUILD_PRESET="linux-release"
BUILD_DIR="$APP_DIR/build/$BUILD_PRESET"
FINTERM_BIN="$BUILD_DIR/FinceptTerminal"
STUB_LOG_DIR="${XDG_CACHE_HOME:-$HOME/.cache}"
STUB_LOG="$STUB_LOG_DIR/fincept-stub.log"
STUB_HEALTH_URL="http://127.0.0.1:8765/health"

# State dirs that `reset` operates on.
CONFIG_DIR="$HOME/.config/Fincept"
SHARE_DIR="$HOME/.local/share/com.fincept.terminal"

# ── Help ─────────────────────────────────────────────────────────────────────

usage() {
    cat <<'EOF'
finterm — local Fincept Terminal management

USAGE
    finterm                                Launch (same as `finterm start`)
    finterm start                          Launch the Qt app + localhost stub
    finterm build [--clean] [--tests]      Build the Qt binary
    finterm reset [reset-options...]       Reset persistent state
    finterm stop                           Stop the Qt app and the stub
    finterm status                         Show what's running
    finterm help                           Show this message

START
    Strips Qt window/toolbar/dock-layout state (preserves portfolio,
    watchlists, theme, workspaces), starts the localhost stub if it
    isn't already up, then exec's the Qt binary in the foreground.

    Set FINCEPT_KEEP_WINDOW=1 to skip the window-state cleanup if you
    want Qt to remember a hand-arranged layout across launches.

BUILD
    Default: incremental build of the existing build/<preset>.
    --clean   Re-run cmake configure and force a clean rebuild.
    --tests   Configure with -DFINCEPT_BUILD_TESTS=ON (test suite enabled).

RESET
    finterm reset [no flags]       Strip Qt window/dock/toolbar state only
                                   (~1s; safe; preserves everything else).
                                   This is what `start` runs each launch.
    finterm reset --window-only    Explicit alias for the default.
    finterm reset --config-only    Back up the entire Qt config dir.
                                   Preserves Python venv → first launch fast.
    finterm reset --full           Nuke Qt config + Python venv + caches.
                                   Next launch reprovisions ~110 pip
                                   packages (5–15 min).
    finterm reset --list           List timestamped backups.
    finterm reset --restore <ts>   Roll back a prior reset.

    Every destructive reset moves the affected directory to
    <dir>.bak.<unix-ts> rather than deleting outright, so --restore is
    always available.

STOP
    pkill the FinceptTerminal binary and the localhost stub.

STATUS
    Show whether the Qt binary, the data daemon, and the stub are
    running, and whether http://127.0.0.1:8765/health is responding.
EOF
}

# ── Subcommand: start ────────────────────────────────────────────────────────

cmd_start() {
    if [[ ! -x "$FINTERM_BIN" ]]; then
        cat >&2 <<EOF
FinceptTerminal binary not found at:
    $FINTERM_BIN

Build it first:
    finterm build
EOF
        exit 1
    fi

    # Strip Qt window/toolbar/dock-layout state from FinceptTerminal.conf
    # so every launch starts with a clean default layout. Surgical —
    # preserves portfolio, workspaces, theme, component-usage stats, and
    # every other setting. Set FINCEPT_KEEP_WINDOW=1 to skip.
    if [[ "${FINCEPT_KEEP_WINDOW:-0}" != "1" ]]; then
        do_window_only_reset
    fi

    # Ensure the stub is up. pgrep matches whether it was started by
    # systemd, by hand, or by a previous launch.
    if ! pgrep -f "tools/local_stub/server.py" >/dev/null; then
        mkdir -p "$STUB_LOG_DIR"
        echo "Starting localhost stub (logs: $STUB_LOG)"
        nohup python3 "$STUB_PY" >>"$STUB_LOG" 2>&1 &
        disown || true

        # Wait briefly for the port to come up so finterm doesn't race
        # the stub. ~3s budget; usually <200ms.
        for _ in $(seq 1 30); do
            if curl -fs --max-time 0.2 "$STUB_HEALTH_URL" >/dev/null 2>&1; then
                break
            fi
            sleep 0.1
        done
    fi

    exec "$FINTERM_BIN" "$@"
}

# ── Subcommand: build ────────────────────────────────────────────────────────

cmd_build() {
    local clean=0
    local tests=0
    for arg in "$@"; do
        case "$arg" in
            --clean) clean=1 ;;
            --tests) tests=1 ;;
            -h|--help)
                cat <<'EOF'
finterm build — build the Qt binary

    --clean    Re-run cmake configure and force a clean rebuild
    --tests    Configure with -DFINCEPT_BUILD_TESTS=ON

Without flags, runs an incremental ninja build against
build/linux-release using the existing configuration.
EOF
                return 0 ;;
            *) echo "unknown build flag: $arg (try --help)" >&2; return 2 ;;
        esac
    done

    cd "$APP_DIR"

    # Configure if needed (no CMakeCache.txt yet, or --clean was passed,
    # or the user asked to toggle tests on).
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]] || [[ "$clean" == 1 ]] || [[ "$tests" == 1 ]]; then
        echo "==> Configuring (preset: $BUILD_PRESET${tests:+, tests on})"
        local extra=()
        [[ "$tests" == 1 ]] && extra+=("-DFINCEPT_BUILD_TESTS=ON")
        cmake --preset "$BUILD_PRESET" "${extra[@]}"
    fi

    if [[ "$clean" == 1 ]]; then
        echo "==> Clean rebuild"
        cmake --build "$BUILD_DIR" --clean-first -j
    else
        echo "==> Incremental build"
        cmake --build "$BUILD_DIR" -j
    fi

    echo
    echo "Built: $FINTERM_BIN"
}

# ── Reset helpers (inlined; previously tools/reset.sh) ───────────────────────

kill_app() {
    pkill -9 -f FinceptTerminal 2>/dev/null || true
    pkill -9 -f "yfinance_data.py --daemon" 2>/dev/null || true
    sleep 1
    if pgrep -f FinceptTerminal >/dev/null 2>&1; then
        echo "WARN: FinceptTerminal still running after kill — proceeding anyway." >&2
    fi
}

backup_one() {
    local src="$1" label="$2" ts="$3"
    if [[ -d "$src" ]]; then
        mv "$src" "${src}.bak.${ts}"
        echo "  $label backed up → ${src}.bak.${ts}"
    else
        echo "  $label not present, skipping"
    fi
}

# Surgical: strip ONLY the window/toolbar/dock-layout state from
# FinceptTerminal.conf. Leaves portfolio, workspaces, theme, component
# usage stats, and every other setting alone. Safe to run every launch.
do_window_only_reset() {
    local conf="$CONFIG_DIR/FinceptTerminal.conf"
    if [[ ! -f "$conf" ]]; then
        return 0  # nothing to do (first launch — no config yet)
    fi
    # Remove top-level window_count and window_ids keys, plus every
    # [window_<n>] section (Qt state save/restore data). A QSettings .ini
    # section runs from its header to the next "[" header or EOF.
    local tmp
    tmp=$(mktemp)
    awk '
        BEGIN { in_window_section = 0 }
        /^\[window_[0-9]+\]/ { in_window_section = 1; next }
        /^\[/ { in_window_section = 0 }
        in_window_section { next }
        /^window_count=/ { next }
        /^window_ids=/   { next }
        { print }
    ' "$conf" > "$tmp"
    mv "$tmp" "$conf"
}

do_config_only_reset() {
    local ts; ts=$(date +%s)
    echo "=== Killing app ==="
    kill_app
    echo "=== Backing up Qt config ==="
    backup_one "$CONFIG_DIR" "Qt config" "$ts"
    echo
    echo "Reset complete (Qt config only)."
    echo "Python venv preserved → first launch will be fast."
    echo "Relaunch: finterm"
}

do_full_reset() {
    local ts; ts=$(date +%s)
    echo "=== Killing app ==="
    kill_app
    echo "=== Backing up Qt config + share dir ==="
    backup_one "$CONFIG_DIR" "Qt config" "$ts"
    backup_one "$SHARE_DIR" "Share dir (Python venv, caches, workspaces)" "$ts"
    echo
    echo "Full reset complete."
    echo "WARNING: Python venv was wiped. Next launch re-provisions ~110 pip"
    echo "packages — expect 5–15 minutes of 'PythonSetup' activity before the"
    echo "auth screen appears."
    echo "Relaunch: finterm"
}

do_list_resets() {
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

do_restore_reset() {
    local id="$1"
    local cbak="${CONFIG_DIR}.bak.${id}"
    local sbak="${SHARE_DIR}.bak.${id}"
    if [[ ! -d "$cbak" && ! -d "$sbak" ]]; then
        echo "Error: no backup with timestamp '$id' (try \`finterm reset --list\`)." >&2
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

# ── Subcommand: reset ────────────────────────────────────────────────────────

cmd_reset() {
    case "${1:-}" in
        ""|--window-only)
            do_window_only_reset ;;
        --config-only)
            do_config_only_reset ;;
        --full)
            do_full_reset ;;
        --list)
            do_list_resets ;;
        --restore)
            if [[ -z "${2:-}" ]]; then
                echo "Error: --restore requires a backup timestamp ID. See \`finterm reset --list\`." >&2
                exit 2
            fi
            do_restore_reset "$2" ;;
        -h|--help)
            usage ;;
        *)
            echo "unknown reset flag: $1 (try \`finterm help\`)" >&2
            exit 2 ;;
    esac
}

# ── Subcommand: stop ─────────────────────────────────────────────────────────

cmd_stop() {
    local stopped=0
    if pgrep -f FinceptTerminal >/dev/null; then
        pkill -f FinceptTerminal && echo "Stopped FinceptTerminal" && stopped=1
    fi
    if pgrep -f "tools/local_stub/server.py" >/dev/null; then
        pkill -f "tools/local_stub/server.py" && echo "Stopped localhost stub" && stopped=1
    fi
    if [[ "$stopped" == 0 ]]; then
        echo "Nothing to stop — neither FinceptTerminal nor the stub is running."
    fi
}

# ── Subcommand: status ───────────────────────────────────────────────────────

cmd_status() {
    local app stub daemon

    if app=$(pgrep -af FinceptTerminal); then
        echo "● FinceptTerminal:"
        echo "$app" | sed 's/^/    /'
    else
        echo "○ FinceptTerminal: not running"
    fi

    if daemon=$(pgrep -af "yfinance_data.py --daemon"); then
        echo "● Data daemon:"
        echo "$daemon" | sed 's/^/    /'
    else
        echo "○ Data daemon: not running"
    fi

    if stub=$(pgrep -af "tools/local_stub/server.py"); then
        echo "● Localhost stub:"
        echo "$stub" | sed 's/^/    /'
    else
        echo "○ Localhost stub: not running"
    fi

    if curl -fsS --max-time 1 "$STUB_HEALTH_URL" >/dev/null 2>&1; then
        echo "● Stub /health: 200 OK"
    else
        echo "○ Stub /health: not responding"
    fi
}

# ── Dispatch ─────────────────────────────────────────────────────────────────

sub="${1:-start}"
[[ $# -gt 0 ]] && shift || true

case "$sub" in
    start)         cmd_start  "$@" ;;
    build)         cmd_build  "$@" ;;
    reset)         cmd_reset  "$@" ;;
    stop)          cmd_stop   "$@" ;;
    status)        cmd_status "$@" ;;
    help|-h|--help) usage ;;
    *)
        echo "unknown subcommand: $sub" >&2
        echo >&2
        usage >&2
        exit 2
        ;;
esac
