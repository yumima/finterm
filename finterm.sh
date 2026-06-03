#!/usr/bin/env bash
# finterm.sh — one-stop launcher / builder / state utility for finterm.
#
# Subcommands:
#   setup              — install system deps + Qt, configure, and build (first run)
#   start    (default) — launch the Qt app (auth runs in-process; no stub needed)
#   build              — build the Qt binary via cmake
#   repair             — fix a misbehaving app, keeping all your data
#   reset              — wipe everything to a clean install (snapshots data first)
#   stop               — stop the running app
#   status             — print what's running
#   install            — create a desktop-launcher entry (apps menu / dock pin)
#   uninstall          — remove the desktop-launcher entry
#   help               — print this help
#
# Safe to invoke from anywhere; paths resolve relative to this script's
# real location (so a symlink at ~/bin/finterm still finds the repo).

set -uo pipefail

# ── Paths ────────────────────────────────────────────────────────────────────
# This script handles Linux + macOS (both POSIX). On Windows use finterm.ps1.

# Resolve this script's real directory portably — macOS's BSD `readlink` has no
# -f, so walk the symlink chain by hand instead.
_src="${BASH_SOURCE[0]}"
while [ -h "$_src" ]; do
    _dir="$(cd -P "$(dirname "$_src")" && pwd)"
    _src="$(readlink "$_src")"
    [[ "$_src" != /* ]] && _src="$_dir/$_src"
done
REPO_DIR="$(cd -P "$(dirname "$_src")" && pwd)"
APP_DIR="$REPO_DIR/fincept-qt"

case "$(uname -s)" in
    Darwin*) FINCEPT_OS="macos" ;;
    *)       FINCEPT_OS="linux" ;;
esac

# Per-OS build preset, binary, and state dirs. State dirs mirror the app's
# AppPaths::root() / QSettings locations (see fincept-qt/src/core/config/AppPaths.cpp).
if [[ "$FINCEPT_OS" == "macos" ]]; then
    BUILD_PRESET="macos-release"
    BUILD_DIR="$APP_DIR/build/$BUILD_PRESET"
    FINTERM_BIN="$BUILD_DIR/finterm.app/Contents/MacOS/finterm"
    FINTERM_PROC="finterm.app/Contents/MacOS"   # pgrep/pkill match (binary path; never matches finterm.sh)
    SHARE_DIR="$HOME/Library/Application Support/com.fincept.terminal"
    CONFIG_DIR="$HOME/Library/Preferences"   # SHARED dir — QSettings writes a plist here
    BACKUP_DIR="${FINCEPT_BACKUP_DIR:-$HOME/Library/Application Support/finterm-backups}"
else
    BUILD_PRESET="linux-release"
    BUILD_DIR="$APP_DIR/build/$BUILD_PRESET"
    FINTERM_BIN="$BUILD_DIR/finterm.bin"
    FINTERM_PROC="finterm.bin"                  # pgrep/pkill match (never matches finterm.sh)
    SHARE_DIR="$HOME/.local/share/com.fincept.terminal"
    CONFIG_DIR="$HOME/.config/Fincept"       # app-specific dir (safe to wipe wholesale)
    BACKUP_DIR="${FINCEPT_BACKUP_DIR:-$HOME/.local/share/finterm-backups}"
fi
DATA_DIR="$SHARE_DIR/data"              # SQLite DBs (per-user portfolio under data/users/, auth.db)
WORKSPACES_DIR="$SHARE_DIR/workspaces"  # saved .fwsp workspace layouts
# BACKUP_DIR (set per-OS above) holds portable snapshots OUTSIDE the reset
# targets, so `reset` never deletes them.

# ── Output helpers (used by `setup`) ─────────────────────────────────────────
GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[1;33m'; NC='\033[0m'
ok()   { echo -e "  ${GREEN}OK${NC}"; }
fail() { echo -e "  ${RED}ERROR: $1${NC}" >&2; exit 1; }
info() { echo -e "  ${YELLOW}$1${NC}"; }
version_ge() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]; }

# confirm "<prompt>" — interactive y/N gate for destructive actions. Honors the
# caller's local ASSUME_YES=1 (set by -y/--yes) to bypass. A non-y reply, EOF,
# or non-TTY stdin all count as "no" (safe default).
confirm() {
    [[ "${ASSUME_YES:-0}" == "1" ]] && return 0
    local reply
    read -r -p "  $1 [y/N]: " reply || return 1
    [[ "$reply" =~ ^[Yy]$ ]]
}

# ── Help ─────────────────────────────────────────────────────────────────────

usage() {
    cat <<'EOF'
finterm — local terminal management

USAGE
    finterm <subcommand> [subcommand-flags...]

    Subcommands are bare words — no leading dashes. Flags inside a
    subcommand take "--" (e.g. `finterm build --clean`). Leading-dash
    forms like `finterm --build` are also accepted as aliases.

    Just run it — first launch sets everything up automatically:
        finterm                            Launch (sets up on first run)

  RUN
    finterm              (or start)        Launch the app
    finterm stop                           Stop it
    finterm status                         Show what's running

  FIX
    finterm repair                         Fix a misbehaving app — keeps your data
    finterm reset                          Wipe everything to a clean install

  SET UP
    finterm setup [--ci]                   (Re)install deps + Qt and build
    finterm build [--clean] [--tests]      Rebuild after code changes (dev)

  DESKTOP
    finterm install                        Add a desktop/menu launcher
    finterm uninstall                      Remove the launcher

    finterm help                           Show this message

SETUP
    First-run provisioning (formerly the separate setup.sh). Installs system
    build tools + the system Qt 6 packages (apt/pacman/dnf, or Homebrew on
    macOS), verifies the toolchain (compiler / CMake / Python / Node), locates
    Qt, configures the CMake preset, builds the binary, and fetches the yt-dlp
    helper. Re-runnable; safe to invoke on an already-provisioned checkout.
      --ci    Non-interactive (skip the "launch now?" prompt). For CI.
    Qt resolution honours Qt6_DIR and FINCEPT_QT_ROOT overrides; otherwise the
    distro/Homebrew Qt on CMake's default search path is used.

START
    Strips Qt window/toolbar/dock-layout state (preserves portfolio,
    watchlists, theme, workspaces), starts the localhost stub if it
    isn't already up, then exec's the Qt binary in the foreground.

    Set FINCEPT_KEEP_WINDOW=1 to skip the window-state cleanup if you
    want Qt to remember a hand-arranged layout across launches.

    GPU selection (hybrid laptops):
      FINCEPT_GPU=auto    (default) iGPU; auto-avoids a wedged NVIDIA dGPU
      FINCEPT_GPU=igpu    Force the integrated GPU (Mesa)
      FINCEPT_GPU=nvidia  Render on the discrete NVIDIA GPU (PRIME offload;
                          needs a working driver, else falls back to iGPU)

    Set FINCEPT_GDB=1 to run under gdb --batch and capture a backtrace
    in $SHARE_DIR/crashdumps/ on crash. Off by default — ptrace overhead
    hurts the QtMultimedia FFmpeg pipeline, audio, and GPU sync timing.
    Kernel core files (apport / systemd-coredump) stay enabled regardless.

    Video hwaccel diagnostics:
      FINCEPT_VIDEO_DEBUG=1     Enable QtMultimedia FFmpeg-backend logging
                                (hwaccel attempt, texture-converter, etc.)
      FINCEPT_HWACCEL=vaapi     Force VAAPI decode (Intel iGPU on Linux).
      FINCEPT_HWACCEL=off       Force CPU-only decode (baseline test).
    Qt 6.8.3's plugin does NOT support NVDEC for decode — only VAAPI on
    Linux. NVIDIA-only systems fall back to CPU decode regardless.

BUILD
    Default: incremental build of the existing build/<preset>.
    --clean   Re-run cmake configure and force a clean rebuild.
    --tests   Configure with -DFINCEPT_BUILD_TESTS=ON (test suite enabled).

REPAIR
    The everyday "make it work" button. Holistically clears the regenerable
    state that causes misbehavior — caches, transient runtime, stale window/dock
    layout, and a broken Python venv — while KEEPING everything you care about:
    portfolio, API keys, GUI layout, data-source settings, and workspaces.

    Surgical and safe: your data is never deleted (the venv is rebuilt only if
    a health check finds it broken), and an insurance snapshot of your data is
    written to $BACKUP_DIR first regardless. Reach for this first whenever the
    app is crashing, hanging, or showing stale/garbled state.

RESET
    The "wipe it clean" button — a true factory reset to a fresh install. Asks
    to confirm (add -y to skip). Snapshots your data to $BACKUP_DIR first and
    prints the exact copy-back commands, so a regretted wipe is still
    recoverable — but if you just want to fix a problem, use `repair` instead.

STOP
    pkill the finterm binary.

STATUS
    Show whether the Qt binary and the data daemon are running.
    Auth runs in-process (no stub server).

INSTALL / UNINSTALL
    `install` writes ~/.local/share/applications/finterm.desktop with
    Exec/Icon paths resolved to this checkout, so the launcher works no
    matter where you cloned the repo. Then find "finterm" in your apps menu
    and right-click → Pin to Dash / Add to Favorites.
    `uninstall` removes that file. (This only adds a menu/dock entry — it
    does NOT install the application itself; use `setup` for that.)
EOF
}

# ── Subcommand: start ────────────────────────────────────────────────────────

cmd_start() {
    # First run: no binary yet. From a terminal, provision automatically
    # (setup installs deps + Qt and builds) then launch — so the whole first
    # run is just `./finterm.sh`. A GUI/desktop launch has no TTY to answer
    # sudo/prompts, so there we guide the user to run setup in a terminal.
    if [[ ! -x "$FINTERM_BIN" ]]; then
        if [[ -t 0 && -t 1 ]]; then
            echo "First run — provisioning finterm (installs dependencies + builds; one time)…"
            echo
            # Run in a subshell so cmd_setup's `set -e` can't leak into the rest
            # of cmd_start (it would abort the launch on any benign non-zero, e.g.
            # a restricted `ulimit -c`). Disk side-effects persist; the binary
            # check below is the success gate.
            ( cmd_setup --ci )
            echo
            [[ -x "$FINTERM_BIN" ]] || { echo "Setup finished but no binary at $FINTERM_BIN — see errors above." >&2; exit 1; }
            echo "Setup complete — launching."
        else
            cat >&2 <<EOF
finterm isn't set up yet (no binary found).
Open a terminal and run:
    $REPO_DIR/finterm.sh setup
EOF
            exit 1
        fi
    fi

    # Strip Qt window/toolbar/dock-layout state from finterm.conf
    # so every launch starts with a clean default layout. Surgical —
    # preserves portfolio, workspaces, theme, component-usage stats, and
    # every other setting. Set FINCEPT_KEEP_WINDOW=1 to skip.
    if [[ "${FINCEPT_KEEP_WINDOW:-0}" != "1" ]]; then
        do_window_only_reset
    fi

    # ── GPU selection (hybrid laptops, Linux) ────────────────────────────────
    # FINCEPT_GPU picks which GPU finterm renders on:
    #   auto   (default) — use the iGPU; if a discrete NVIDIA card is present but
    #                      its driver is wedged (GSP/FSP bring-up failing, etc.),
    #                      pin glvnd to Mesa so the dead dGPU can't hang startup
    #                      (Qt's GL/EGL probe of a wedged card spins the GUI
    #                      thread → 100% CPU, no window). On healthy/NVIDIA-less
    #                      machines this is a no-op; Qt's own selection stands.
    #   igpu             — always render on the integrated GPU (Mesa).
    #   nvidia           — render on the discrete NVIDIA GPU via PRIME offload.
    #                      Requires a working driver; falls back to the iGPU with
    #                      a warning if nvidia-smi can't reach the card.
    _have_nvidia() { command -v lspci >/dev/null 2>&1 && lspci -d 10de: 2>/dev/null | grep -qiE 'VGA|3D|Display'; }
    _nvidia_alive() { command -v nvidia-smi >/dev/null 2>&1 && timeout 3 nvidia-smi >/dev/null 2>&1; }
    _use_mesa() {
        export __EGL_VENDOR_LIBRARY_FILENAMES="${__EGL_VENDOR_LIBRARY_FILENAMES:-/usr/share/glvnd/egl_vendor.d/50_mesa.json}"
        export __GLX_VENDOR_LIBRARY_NAME="${__GLX_VENDOR_LIBRARY_NAME:-mesa}"
        export DRI_PRIME=0
    }
    _use_nvidia() {  # PRIME render offload onto the discrete GPU
        export __NV_PRIME_RENDER_OFFLOAD=1
        export __GLX_VENDOR_LIBRARY_NAME=nvidia
        export __VK_LAYER_NV_optimus=NVIDIA_only
        export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json
    }
    # glvnd/PRIME is a Linux concept; macOS selects the GPU itself (skip there).
    if [[ "$FINCEPT_OS" == "linux" ]]; then
        case "${FINCEPT_GPU:-auto}" in
            igpu|intel|mesa)
                _use_mesa ;;
            nvidia|dgpu)
                if _have_nvidia && _nvidia_alive; then
                    echo "finterm: rendering on the discrete NVIDIA GPU (PRIME offload)." >&2
                    _use_nvidia
                else
                    echo "finterm: NVIDIA GPU requested but unavailable — falling back to Mesa/iGPU." >&2
                    _use_mesa
                fi ;;
            auto|*)
                if _have_nvidia && ! _nvidia_alive; then
                    echo "finterm: NVIDIA GPU present but unresponsive — rendering on Mesa/iGPU." >&2
                    _use_mesa
                fi ;;
        esac
    fi

    # ── Crash capture ────────────────────────────────────────────────────────
    # Kernel core file (apport / systemd-coredump) is always enabled.
    # The GDB wrapper is opt-in via FINCEPT_GDB=1 — by default we plain-exec
    # so timing-sensitive subsystems (QtMultimedia FFmpeg pipeline, audio,
    # GPU sync) don't suffer ptrace overhead on every signal/vfork/thread
    # event. Use FINCEPT_GDB=1 only when investigating a specific crash.
    local CRASHDUMP_DIR="$SHARE_DIR/crashdumps"
    mkdir -p "$CRASHDUMP_DIR"
    ulimit -c unlimited

    # ── Video hwaccel knobs ──────────────────────────────────────────────────
    # Qt 6.8.3's FFmpeg media plugin supports VAAPI for decode on Linux but
    # NOT NVDEC/CUVID (only the encoders h264_nvenc etc.). On NVIDIA-only
    # systems the H.264 decode falls back to CPU; on systems with an Intel
    # iGPU + VAAPI, decode should land on the iGPU automatically. Use these
    # env vars to diagnose or force the path when Qt's auto-detect isn't
    # doing what you'd expect.
    #
    # FINCEPT_VIDEO_DEBUG=1 — enables FFmpeg-backend logging
    #   QT_FFMPEG_DEBUG=1 and qt.multimedia.ffmpeg.* logging rules so the
    #   hwaccel attempt + texture-converter outcome is visible on stderr.
    # FINCEPT_HWACCEL=vaapi|off — overrides Qt's auto-detection
    #   "vaapi" forces VAAPI decode (QT_FFMPEG_DECODING_HW_DEVICE_TYPES).
    #   "off"   disables all hwaccel (forces software decode — slow but
    #            useful as a baseline to confirm CPU is decode-bound).
    if [[ "${FINCEPT_VIDEO_DEBUG:-0}" == "1" ]]; then
        export QT_FFMPEG_DEBUG=1
        export QT_LOGGING_RULES="${QT_LOGGING_RULES:+$QT_LOGGING_RULES;}qt.multimedia.ffmpeg.*=true"
    fi
    case "${FINCEPT_HWACCEL:-}" in
        vaapi) export QT_FFMPEG_DECODING_HW_DEVICE_TYPES=vaapi ;;
        off)   export QT_FFMPEG_DECODING_HW_DEVICE_TYPES=     ;;
    esac

    if [[ "${FINCEPT_GDB:-0}" == "1" ]] && command -v gdb >/dev/null 2>&1; then
        local GDB_LOG="$CRASHDUMP_DIR/gdb-$(date +%Y%m%d-%H%M%S).log"
        # Pass non-crash signals through to Qt so they are not intercepted.
        exec gdb --batch \
            -ex "set pagination off" \
            -ex "set logging file $GDB_LOG" \
            -ex "set logging enabled on" \
            -ex "handle SIGPIPE  nostop noprint pass" \
            -ex "handle SIGHUP   nostop noprint pass" \
            -ex "handle SIGCHLD  nostop noprint pass" \
            -ex "handle SIGUSR1  nostop noprint pass" \
            -ex "handle SIGUSR2  nostop noprint pass" \
            -ex "run" \
            -ex "thread apply all bt full" \
            -ex "info registers" \
            -ex "quit" \
            --args "$FINTERM_BIN" "$@"
    else
        exec "$FINTERM_BIN" "$@"
    fi
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
        # ${extra[@]+"${extra[@]}"} expands to nothing when the array is empty and
        # to the quoted elements otherwise — safe under `set -u` even on bash 3.2
        # (macOS's system /bin/bash), where a bare "${extra[@]}" is "unbound".
        cmake --preset "$BUILD_PRESET" ${extra[@]+"${extra[@]}"}
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
    pkill -9 -f "$FINTERM_PROC" 2>/dev/null || true
    pkill -9 -f "yfinance_data.py --daemon" 2>/dev/null || true
    sleep 1
    if pgrep -f "$FINTERM_PROC" >/dev/null 2>&1; then
        echo "WARN: finterm still running after kill — proceeding anyway." >&2
    fi
}

# Surgical: strip ONLY the window/toolbar/dock-layout state from
# finterm.conf. Leaves portfolio, workspaces, theme, component
# usage stats, and every other setting alone. Safe to run every launch.
do_window_only_reset() {
    local conf="$CONFIG_DIR/finterm.conf"
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

# _snapshot_data <dest> — copy the portable user data (portfolio, API keys,
# GUI/data-source config, saved workspaces) into <dest>. Used as an insurance
# snapshot by both `repair` and `reset`. Regenerable state (caches, venv,
# models) is never captured. Per-user DBs live under data/users/.
_snapshot_data() {
    local dest="$1" f g
    mkdir -p "$dest/config" "$dest/data" "$dest/workspaces"
    # Settings: Linux keeps .conf files in ~/.config/Fincept; macOS keeps a
    # QSettings plist under ~/Library/Preferences (filename varies by Qt).
    for f in finterm.conf finterm-Secure.conf Terminal.conf FinceptTerminal.conf FinceptTerminal-Secure.conf; do
        [[ -f "$CONFIG_DIR/$f" ]] && cp -p "$CONFIG_DIR/$f" "$dest/config/"
    done
    if [[ "$FINCEPT_OS" == "macos" ]]; then
        cp -p "$HOME/Library/Preferences/"*[Ff]incept*.plist "$dest/config/" 2>/dev/null || true
    fi
    for g in auth.db auth.db-wal auth.db-shm; do
        [[ -f "$DATA_DIR/$g" ]] && cp -p "$DATA_DIR/$g" "$dest/data/"
    done
    if [[ -d "$DATA_DIR/users" ]]; then
        mkdir -p "$dest/data/users"
        cp -rp "$DATA_DIR/users/." "$dest/data/users/" 2>/dev/null || true
    fi
    for g in fincept.db fincept.db-wal fincept.db-shm; do
        [[ -f "$DATA_DIR/$g" ]] && cp -p "$DATA_DIR/$g" "$dest/data/"
    done
    [[ -d "$WORKSPACES_DIR" ]] && cp -rp "$WORKSPACES_DIR/." "$dest/workspaces/" 2>/dev/null || true
    for f in config data workspaces; do rmdir "$dest/$f" 2>/dev/null || true; done
    echo "  data snapshot → $dest"
}

# _venv_healthy — true if the active Python venv is provisioned and importable.
_venv_healthy() {
    local py="$SHARE_DIR/venv-numpy2/bin/python3"
    [[ -f "$SHARE_DIR/.setup_complete" && -x "$py" ]] && "$py" -c 'import numpy' >/dev/null 2>&1
}

# ── Subcommand: repair ───────────────────────────────────────────────────────
# The everyday "make it work" button. Holistically clears the regenerable state
# that causes misbehavior — caches, transient runtime, stale window/dock layout,
# and a broken Python venv — while KEEPING everything you care about: portfolio,
# API keys, GUI layout, data-source settings, and workspaces. Surgical: your
# data is never deleted (an insurance snapshot is taken first regardless).
cmd_repair() {
    local a
    for a in "$@"; do
        case "$a" in
            -h|--help) echo "finterm repair — fix a misbehaving app, keeping all your data"; return 0 ;;
            *) echo "unknown repair flag: $a" >&2; return 2 ;;
        esac
    done

    echo "=== Stopping app ==="
    kill_app
    echo "=== Insurance snapshot (your data is also kept in place) ==="
    _snapshot_data "$BACKUP_DIR/repair-$(date +%Y%m%d-%H%M%S)"

    echo "=== Clearing caches + transient state ==="
    rm -f  "$DATA_DIR"/cache.db "$DATA_DIR"/cache.db-wal "$DATA_DIR"/cache.db-shm
    rm -rf "$SHARE_DIR"/cache "$SHARE_DIR"/uv-cache "$SHARE_DIR"/runtime "$SHARE_DIR"/crashdumps
    echo "  cleared: market/screen cache, uv cache, runtime sockets, crash dumps"

    echo "=== Resetting window/dock layout ==="
    do_window_only_reset
    echo "  window/dock arrangement reset to default"

    echo "=== Checking Python venv ==="
    if _venv_healthy; then
        echo "  venv: OK — kept"
    else
        echo "  venv: broken/missing → removing (rebuilds on next launch, ~5–15 min)"
        rm -rf "$SHARE_DIR"/venv-numpy1 "$SHARE_DIR"/venv-numpy2 "$SHARE_DIR"/.setup_complete
    fi

    echo
    echo "Repair complete — kept your portfolio, API keys, GUI layout, data-source,"
    echo "and workspaces. Relaunch: finterm"
}

# ── Subcommand: reset ────────────────────────────────────────────────────────
# The "wipe it clean" button — a true factory reset. Snapshots your data first
# (printed path) so a regretted wipe is still recoverable, then removes config +
# share entirely. Use `repair` instead if you just want to fix a problem.
cmd_reset() {
    local ASSUME_YES=0 a
    for a in "$@"; do
        case "$a" in
            -y|--yes|--assume-yes) ASSUME_YES=1 ;;
            -h|--help) echo "finterm reset — wipe everything to a clean install (use 'repair' to keep data)"; return 0 ;;
            *) echo "unknown reset flag: $a" >&2; return 2 ;;
        esac
    done

    confirm "Wipe EVERYTHING to a clean install? ('finterm repair' fixes problems without losing data)" \
        || { echo "Aborted — nothing changed."; return 1; }

    echo "=== Stopping app ==="
    kill_app
    local snap="$BACKUP_DIR/pre-reset-$(date +%Y%m%d-%H%M%S)"
    echo "=== Snapshotting your data first ==="
    _snapshot_data "$snap"
    echo "=== Wiping app state (factory reset) ==="
    rm -rf "$SHARE_DIR"
    if [[ "$FINCEPT_OS" == "macos" ]]; then
        # CONFIG_DIR is the SHARED ~/Library/Preferences — only remove OUR plist(s),
        # never the whole directory. Kill cfprefsd FIRST so it flushes its in-memory
        # cache and can't immediately rewrite the plist we're about to delete.
        killall -u "$USER" cfprefsd 2>/dev/null || true
        rm -f "$HOME/Library/Preferences/"*[Ff]incept*.plist 2>/dev/null || true
    else
        rm -rf "$CONFIG_DIR"     # app-specific dir on Linux — safe to remove wholesale
    fi

    echo
    echo "Clean slate. Next launch reprovisions from scratch (~5–15 min)."
    echo "Your data was snapshotted to:"
    echo "    $snap"
    echo "  To recover it after relaunching, stop the app and copy it back:"
    echo "    finterm stop"
    echo "    cp -a \"$snap/data/.\"       \"$DATA_DIR/\""
    echo "    cp -a \"$snap/workspaces/.\" \"$WORKSPACES_DIR/\""
    if [[ "$FINCEPT_OS" == "macos" ]]; then
        echo "    cp -a \"$snap/config/.\"     \"$HOME/Library/Preferences/\"   # settings + keys"
    else
        echo "    cp -a \"$snap/config/.\"     \"$CONFIG_DIR/\"   # settings + keys"
    fi
    echo "Relaunch: finterm  (or: finterm setup)"
}

# ── Subcommand: stop ─────────────────────────────────────────────────────────

cmd_stop() {
    local stopped=0
    if pgrep -f "$FINTERM_PROC" >/dev/null; then
        pkill -f "$FINTERM_PROC" && echo "Stopped finterm" && stopped=1
    fi
    if [[ "$stopped" == 0 ]]; then
        echo "Nothing to stop — finterm is not running."
    fi
}

# ── Subcommand: status ───────────────────────────────────────────────────────

cmd_status() {
    local app daemon

    if app=$(pgrep -af "$FINTERM_PROC"); then
        echo "● finterm:"
        echo "$app" | sed 's/^/    /'
    else
        echo "○ finterm: not running"
    fi

    if daemon=$(pgrep -af "yfinance_data.py --daemon"); then
        echo "● Data daemon:"
        echo "$daemon" | sed 's/^/    /'
    else
        echo "○ Data daemon: not running"
    fi

    # Auth stub is gone — auth/profile run in-process via AuthService.
    echo "● Auth: in-process (no stub server)"
}

# ── Subcommand: setup ─────────────────────────────────────────────────────────
# First-run provisioning (merged from the former standalone setup.sh): system
# deps + system Qt, toolchain checks, configure, build, yt-dlp. Re-runnable.

cmd_setup() {
    set -e   # provisioning is fail-fast; this command exec/exits when done

    # ── Pinned versions (must match CMakeLists.txt) ─────────────
    local QT_VERSION="6.8.3" PYTHON_MIN="3.11" CMAKE_MIN="3.27"
    local GCC_MIN="12.3" CLANG_MIN="15.0" NODE_MIN="18.0"

    local CI_MODE=false arg
    for arg in "$@"; do
        case "$arg" in
            --ci) CI_MODE=true ;;
            -h|--help) echo "finterm setup [--ci] — install deps + Qt, configure, build"; return 0 ;;
            *) echo "unknown setup flag: $arg (try --help)" >&2; return 2 ;;
        esac
    done

    echo ""
    echo "================================================"
    echo "  finterm — Setup"
    echo "  Qt >= ${QT_VERSION%.*} (system) | CMake ${CMAKE_MIN}+ | Python ${PYTHON_MIN}+"
    [ "$CI_MODE" = true ] && echo "  (CI mode — skipping interactive steps)"
    echo "================================================"
    echo ""

    # ── Detect OS ───────────────────────────────────────────────
    local PLATFORM QT_KIT PRESET OS
    OS="$(uname -s)"
    case "$OS" in
        Linux*)  PLATFORM="linux" ; QT_KIT="gcc_64" ; PRESET="linux-release" ;;
        Darwin*) PLATFORM="macos" ; QT_KIT="macos"  ; PRESET="macos-release" ;;
        *)       fail "Unsupported OS: $OS" ;;
    esac
    echo "Platform: $OS"
    echo ""

    # ── Step 1/8: System dependencies ───────────────────────────
    echo "[1/8] Installing system build tools + Qt..."
    if [ "$PLATFORM" = "linux" ]; then
        if command -v apt-get &>/dev/null; then
            sudo apt-get update -qq
            sudo apt-get install -y --no-install-recommends \
                git cmake ninja-build g++ \
                python3 python3-pip python3-venv \
                \
                `# Qt 6 (system/distro packages — single source, auto-upgraded.` \
                `# Covers Widgets/Charts/Network/Sql/Multimedia/WebSockets/Speech/` \
                `# WebEngine/Positioning/WebChannel/Quick used by the app + tests.)` \
                qt6-base-dev qt6-base-dev-tools \
                `# Private headers: required by the bundled qtads docking` \
                `# dependency (Qt6::GuiPrivate) and re-enables QXlsx Excel export.` \
                qt6-base-private-dev \
                `# SQLite QSqlDriver plugin (libqsqlite.so) — split out from` \
                `# qt6-base-dev on Debian/Ubuntu. Without it every QSqlDatabase` \
                `# open fails with "Driver not loaded" (no auth.db / cache.db /` \
                `# portfolio). Bundled in qt6-base on Arch/Fedora; apt-only here.` \
                libqt6sql6-sqlite \
                qt6-declarative-dev qt6-charts-dev \
                qt6-multimedia-dev qt6-websockets-dev \
                qt6-speech-dev qt6-webengine-dev \
                qt6-positioning-dev qt6-webchannel-dev \
                libgl1-mesa-dev libglu1-mesa-dev libegl-dev \
                \
                `# Wayland / X11 platform plugins` \
                libxkbcommon-dev libxkbcommon-x11-dev \
                libxcb-cursor0 libxcb-icccm4 libxcb-image0 \
                libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 \
                libxcb-shape0 libxcb-xinerama0 libxcb-xfixes0 \
                \
                `# Qt Multimedia audio backends` \
                libpulse-dev libasound2-dev \
                \
                `# VAAPI hardware video decode (Qt FFmpeg backend + NVDEC path)` \
                libva-dev libdrm-dev \
                \
                `# System libraries` \
                libfontconfig1 libdbus-1-3 libssl-dev \
                libsecret-1-dev portaudio19-dev \
                \
                `# Node.js for yt-dlp 2025+ JS runtime (YouTube extraction)` \
                nodejs \
                \
                pkg-config curl
        elif command -v pacman &>/dev/null; then
            sudo pacman -Sy --noconfirm --needed \
                base-devel git cmake ninja \
                python python-pip \
                `# Qt 6 (system packages — single source, auto-upgraded)` \
                qt6-base qt6-declarative qt6-charts \
                qt6-multimedia qt6-websockets qt6-speech \
                qt6-webengine qt6-positioning qt6-webchannel \
                mesa glu libglvnd \
                libxkbcommon libxcb xcb-util-cursor \
                libpulse alsa-lib \
                libva libdrm \
                fontconfig dbus openssl \
                libsecret portaudio \
                nodejs \
                pkgconf curl
        elif command -v dnf &>/dev/null; then
            sudo dnf install -y \
                git cmake ninja-build gcc-c++ \
                python3 python3-pip python3-virtualenv \
                `# Qt 6 (system packages — single source, auto-upgraded)` \
                `# qt6-qtbase-private-devel: qtads docking (Qt6::GuiPrivate) + QXlsx Excel export` \
                qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel qt6-qtcharts-devel \
                qt6-qtmultimedia-devel qt6-qtwebsockets-devel qt6-qtspeech-devel \
                qt6-qtwebengine-devel qt6-qtpositioning-devel qt6-qtwebchannel-devel \
                mesa-libGL-devel mesa-libGLU-devel mesa-libEGL-devel \
                libxkbcommon-devel libxcb-devel \
                pulseaudio-libs-devel alsa-lib-devel \
                libva-devel libdrm-devel \
                fontconfig dbus-libs openssl-devel \
                libsecret-devel portaudio-devel \
                nodejs \
                pkgconfig curl
        else
            info "No recognised package manager. Ensure build deps are installed manually."
            info "Required: cmake ninja g++ python3 nodejs qt6 libva-dev libpulse-dev libegl-dev"
        fi
    elif [ "$PLATFORM" = "macos" ]; then
        if ! command -v brew &>/dev/null; then
            [ "$CI_MODE" = true ] && fail "Homebrew not found in CI environment."
            info "Homebrew not found. Installing..."
            /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
        fi
        # yt-dlp installed system-wide via brew on macOS; standalone binary used on Linux.
        # qt is the system/single-source Qt 6 on macOS (Homebrew keg `qt` == Qt 6).
        brew install cmake ninja python@3.11 openssl@3 node yt-dlp portaudio qt
    fi
    ok

    # ── Step 2/8: Verify compiler ───────────────────────────────
    echo "[2/8] Checking C++ compiler..."
    local GCC_VER CLANG_VER
    if [ "$PLATFORM" = "linux" ]; then
        command -v g++ &>/dev/null || fail "g++ not found."
        GCC_VER="$(g++ -dumpfullversion -dumpversion 2>/dev/null || g++ --version | head -1 | awk '{print $NF}')"
        echo "  g++ ${GCC_VER}"
        version_ge "$GCC_VER" "$GCC_MIN" || fail "GCC ${GCC_MIN}+ required. Found ${GCC_VER}. Install g++-12 or newer."
    elif [ "$PLATFORM" = "macos" ]; then
        command -v clang++ &>/dev/null || fail "clang++ not found. Run: xcode-select --install"
        CLANG_VER="$(clang++ --version | head -1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1)"
        echo "  Apple Clang ${CLANG_VER}"
        version_ge "$CLANG_VER" "$CLANG_MIN" || fail "Apple Clang ${CLANG_MIN}+ required (Xcode 15.2+). Found ${CLANG_VER}."
    fi
    ok

    # ── Step 3/8: Verify CMake ──────────────────────────────────
    echo "[3/8] Checking CMake..."
    command -v cmake &>/dev/null || fail "cmake not found."
    local CMAKE_VER; CMAKE_VER="$(cmake --version | head -1 | awk '{print $3}')"
    echo "  cmake ${CMAKE_VER}"
    version_ge "$CMAKE_VER" "$CMAKE_MIN" || fail "CMake ${CMAKE_MIN}+ required. Found ${CMAKE_VER}. Download from https://cmake.org/download/"
    ok

    # ── Step 4/8: Verify Python ─────────────────────────────────
    echo "[4/8] Checking Python..."
    local PYTHON PY_VER
    PYTHON="$(command -v python3.11 || command -v python3 || true)"
    [ -n "$PYTHON" ] || fail "python3 not found."
    PY_VER="$($PYTHON -c 'import sys; print("%d.%d.%d" % sys.version_info[:3])')"
    echo "  python ${PY_VER}"
    version_ge "$PY_VER" "$PYTHON_MIN" || fail "Python ${PYTHON_MIN}+ required. Found ${PY_VER}."
    ok

    # ── Step 5/8: Verify Node.js (yt-dlp JS runtime) ───────────
    echo "[5/8] Checking Node.js..."
    local NODE_VER
    if command -v node &>/dev/null; then
        NODE_VER="$(node --version | tr -d 'v')"
        echo "  node ${NODE_VER}"
        version_ge "$NODE_VER" "$NODE_MIN" \
            || info "Node ${NODE_MIN}+ recommended for yt-dlp YouTube extraction (found ${NODE_VER}). Streams may work with reduced format availability."
        ok
    else
        info "Node.js not found — yt-dlp will still work for direct HLS streams."
        info "Install nodejs >= ${NODE_MIN} for full YouTube channel live extraction."
        echo -e "  ${YELLOW}WARN${NC}"
    fi

    # ── Step 6/8: Locate Qt (system / single-source) ────────────
    # Qt is a *system* dependency installed in step 1 (distro qt6-* / Homebrew
    # qt). We do not vendor a private Qt into the tree — one shared, OS-managed
    # Qt serves every checkout and gets security updates for free. QT_PREFIX is
    # left empty when Qt is on CMake's default search path (Linux distro case)
    # so find_package(Qt6) discovers it. It is set only for an explicit override
    # or a non-default prefix (Homebrew). Overrides: Qt6_DIR, FINCEPT_QT_ROOT.
    local QT_PREFIX="" QMAKE SYS_QT_VER
    echo "[6/8] Locating system Qt (>= ${QT_VERSION%.*}) ..."
    if [ -n "${Qt6_DIR:-}" ] && [ -f "$Qt6_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]; then
        QT_PREFIX="$Qt6_DIR"
        info "Using Qt from Qt6_DIR env: $QT_PREFIX"
    elif [ -n "${FINCEPT_QT_ROOT:-}" ] && [ -f "$FINCEPT_QT_ROOT/$QT_VERSION/$QT_KIT/lib/cmake/Qt6/Qt6Config.cmake" ]; then
        QT_PREFIX="$FINCEPT_QT_ROOT/$QT_VERSION/$QT_KIT"
        info "Using pinned Qt from FINCEPT_QT_ROOT: $QT_PREFIX"
    elif [ "$PLATFORM" = "macos" ]; then
        QT_PREFIX="$(brew --prefix qt 2>/dev/null || true)"
        [ -n "$QT_PREFIX" ] && [ -d "$QT_PREFIX" ] \
            || fail "Homebrew Qt not found. Run: brew install qt"
        info "Using Homebrew Qt: $QT_PREFIX ($("$QT_PREFIX/bin/qmake6" -query QT_VERSION 2>/dev/null))"
    else
        QMAKE="$(command -v qmake6 || command -v qtpaths6 || true)"
        [ -n "$QMAKE" ] \
            || fail "System Qt 6 not found. Install it (re-run setup) — apt: qt6-base-dev qt6-* ; pacman: qt6-base qt6-* ; dnf: qt6-qtbase-devel qt6-*."
        SYS_QT_VER="$("$QMAKE" -query QT_VERSION 2>/dev/null || true)"
        version_ge "${SYS_QT_VER:-0}" "${QT_VERSION%.*}" \
            || fail "System Qt ${SYS_QT_VER:-unknown} is older than the required ${QT_VERSION%.*}. Upgrade the qt6 packages."
        info "Using system Qt ${SYS_QT_VER} (on default CMake search path)"
    fi
    if [ -n "$QT_PREFIX" ]; then
        export CMAKE_PREFIX_PATH="$QT_PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
        echo "  CMAKE_PREFIX_PATH=$QT_PREFIX"
    fi
    ok

    # ── Step 7/8: Configure ─────────────────────────────────────
    [ -d "$APP_DIR" ] || fail "fincept-qt directory not found at $APP_DIR. Ensure you cloned the full repository."
    cd "$APP_DIR"

    echo "[7/8] Configuring (preset: $PRESET)..."
    local EXTRA_ARGS=""
    if [ "$PLATFORM" = "macos" ] && [ -d "/opt/homebrew/opt/openssl@3" ]; then
        EXTRA_ARGS="-DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3"
    fi
    # Only pin CMAKE_PREFIX_PATH for an explicit/non-default Qt (override or
    # Homebrew). For distro Qt on the default search path, QT_PREFIX is empty
    # and find_package(Qt6) discovers it on its own.
    [ -n "$QT_PREFIX" ] && EXTRA_ARGS="$EXTRA_ARGS -DCMAKE_PREFIX_PATH=$QT_PREFIX"
    cmake --preset "$PRESET" $EXTRA_ARGS \
        || fail "CMake configure failed. See error above."
    ok

    # ── Step 8/8: Build ─────────────────────────────────────────
    echo "[8/8] Compiling..."
    cmake --build --preset "$PRESET" || fail "Build failed. See error above."
    ok

    # ── Post-build: install yt-dlp next to binary ───────────────
    local SETUP_BUILD_DIR="$APP_DIR/build/$PRESET"
    local YT_DLP_BIN="$SETUP_BUILD_DIR/yt-dlp"

    echo ""
    echo "[post] Installing yt-dlp next to binary..."

    # On macOS brew already put yt-dlp on PATH; copy it to build dir for portability.
    # On Linux download the official self-contained binary (no Python/pip needed).
    install_ytdlp_binary() {
        if [ "$PLATFORM" = "linux" ]; then
            local url="https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux"
            info "Downloading yt-dlp (standalone binary)..."
            curl -fsSL "$url" -o "$YT_DLP_BIN" || fail "Failed to download yt-dlp from $url"
            chmod +x "$YT_DLP_BIN"
        elif [ "$PLATFORM" = "macos" ]; then
            if command -v yt-dlp &>/dev/null; then
                cp "$(command -v yt-dlp)" "$YT_DLP_BIN" 2>/dev/null || true
            else
                local url="https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos"
                curl -fsSL "$url" -o "$YT_DLP_BIN" || fail "Failed to download yt-dlp from $url"
                chmod +x "$YT_DLP_BIN"
            fi
        fi
    }

    install_ytdlp_binary
    local YTDLP_VER; YTDLP_VER="$("$YT_DLP_BIN" --version 2>/dev/null || echo 'unknown')"
    info "yt-dlp ${YTDLP_VER} → $YT_DLP_BIN"
    ok

    # ── Done ────────────────────────────────────────────────────
    local BIN="$SETUP_BUILD_DIR/finterm.bin"
    [ "$PLATFORM" = "macos" ] && BIN="$SETUP_BUILD_DIR/finterm.app/Contents/MacOS/finterm"

    echo ""
    echo "================================================"
    echo "  Build complete!"
    echo "  Binary:  $BIN"
    echo "  yt-dlp:  $YT_DLP_BIN ($("$YT_DLP_BIN" --version 2>/dev/null))"
    command -v node &>/dev/null && echo "  Node.js: $(node --version) — YouTube JS extraction ready"
    echo "  Launch:  finterm start"
    echo "================================================"
    echo ""

    [ "$CI_MODE" = true ] && return 0

    local LAUNCH
    read -r -p "  Launch now? (y/n): " LAUNCH || LAUNCH=n   # EOF (piped stdin) → skip, don't abort under set -e
    if [[ "$LAUNCH" =~ ^[Yy]$ ]]; then
        cmd_start
    fi
}

# ── Subcommand: install / uninstall (desktop launcher) ───────────────────────
# Adds/removes a launcher so the app shows up in the menu / Launchpad. This is
# NOT installing the application (that's `setup`). Per-OS: a .desktop entry on
# Linux, a symlinked .app under ~/Applications on macOS.

DESKTOP_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
DESKTOP_FILE="$DESKTOP_DIR/finterm.desktop"
MACOS_APP_LINK="$HOME/Applications/finterm.app"

cmd_install() {
    if [[ "$FINCEPT_OS" == "macos" ]]; then
        local app="$BUILD_DIR/finterm.app"
        [[ -d "$app" ]] || { echo "Error: app bundle not found at $app — run 'finterm setup' first." >&2; exit 1; }
        mkdir -p "$HOME/Applications"
        ln -sfn "$app" "$MACOS_APP_LINK"
        echo "Installed: $MACOS_APP_LINK → $app"
        echo "Find \"finterm\" in Launchpad / ~/Applications (drag to the Dock to pin)."
        return 0
    fi
    local icon="$REPO_DIR/finterm-icon.png"
    if [[ ! -f "$icon" ]]; then
        echo "Error: icon not found at $icon" >&2
        exit 1
    fi
    mkdir -p "$DESKTOP_DIR"
    cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=finterm
GenericName=Financial Terminal
Comment=finterm (Qt)
Exec=$REPO_DIR/finterm.sh
Icon=$icon
Terminal=false
Categories=Office;Finance;
StartupWMClass=finterm
StartupNotify=true
EOF
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database "$DESKTOP_DIR" >/dev/null 2>&1 || true
    fi
    echo "Installed: $DESKTOP_FILE"
    echo "Find \"finterm\" in your apps menu and right-click → Pin to Dash / Add to Favorites."
}

cmd_uninstall() {
    if [[ "$FINCEPT_OS" == "macos" ]]; then
        if [[ -L "$MACOS_APP_LINK" || -e "$MACOS_APP_LINK" ]]; then
            rm -f "$MACOS_APP_LINK"; echo "Removed: $MACOS_APP_LINK"
        else
            echo "Nothing to remove — $MACOS_APP_LINK not present."
        fi
        return 0
    fi
    if [[ -f "$DESKTOP_FILE" ]]; then
        rm -f "$DESKTOP_FILE"
        if command -v update-desktop-database >/dev/null 2>&1; then
            update-desktop-database "$DESKTOP_DIR" >/dev/null 2>&1 || true
        fi
        echo "Removed: $DESKTOP_FILE"
    else
        echo "Nothing to remove — $DESKTOP_FILE not present."
    fi
}

# ── Dispatch ─────────────────────────────────────────────────────────────────

sub="${1:-start}"
[[ $# -gt 0 ]] && shift || true

# Accept --start / --build / --reset / --stop / --status as ergonomic
# aliases for the bare subcommands. The canonical form is bare (matches
# `git`, `docker`, `kubectl`); the leading-dash form is forgiving for
# first-timers who guess wrong.
case "$sub" in
    setup|--setup)   cmd_setup  "$@" ;;
    start|--start)   cmd_start  "$@" ;;
    build|--build)   cmd_build  "$@" ;;
    repair|--repair) cmd_repair "$@" ;;
    reset|--reset)   cmd_reset  "$@" ;;
    stop|--stop)     cmd_stop   "$@" ;;
    status|--status) cmd_status "$@" ;;
    install|--install)     cmd_install   "$@" ;;
    uninstall|--uninstall) cmd_uninstall "$@" ;;
    help|-h|--help)  usage ;;
    *)
        echo "unknown subcommand: $sub" >&2
        echo >&2
        usage >&2
        exit 2
        ;;
esac
