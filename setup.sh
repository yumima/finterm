#!/usr/bin/env bash
set -euo pipefail

# ── Parse args ──────────────────────────────────────────────
CI_MODE=false
for arg in "$@"; do
    case "$arg" in
        --ci) CI_MODE=true ;;
    esac
done

# ── Pinned versions (must match CMakeLists.txt) ─────────────
QT_VERSION="6.8.3"
PYTHON_MIN="3.11"
CMAKE_MIN="3.27"
GCC_MIN="12.3"
CLANG_MIN="15.0"
NODE_MIN="18.0"      # yt-dlp 2025+ requires a JS runtime; Node 18 LTS minimum

# ── Colours ─────────────────────────────────────────────────
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

ok()   { echo -e "  ${GREEN}OK${NC}"; }
fail() { echo -e "  ${RED}ERROR: $1${NC}"; exit 1; }
info() { echo -e "  ${YELLOW}$1${NC}"; }

echo ""
echo "================================================"
echo "  Fincept Terminal v4.0.2 — Setup"
echo "  Pinned: Qt ${QT_VERSION} | CMake ${CMAKE_MIN}+ | Python ${PYTHON_MIN}+"
[ "$CI_MODE" = true ] && echo "  (CI mode — skipping interactive steps)"
echo "================================================"
echo ""

# ── Detect OS ───────────────────────────────────────────────
OS="$(uname -s)"
case "$OS" in
    Linux*)  PLATFORM="linux" ; QT_KIT="gcc_64" ; PRESET="linux-release" ;;
    Darwin*) PLATFORM="macos" ; QT_KIT="macos"  ; PRESET="macos-release" ;;
    *)       fail "Unsupported OS: $OS" ;;
esac
echo "Platform: $OS"
echo ""

# ── Helper: version >= min ──────────────────────────────────
version_ge() {
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]
}

# ── Step 1/8: System dependencies ───────────────────────────
echo "[1/8] Installing system build tools..."
if [ "$PLATFORM" = "linux" ]; then
    if command -v apt-get &>/dev/null; then
        sudo apt-get update -qq
        sudo apt-get install -y --no-install-recommends \
            git cmake ninja-build g++ \
            python3 python3-pip python3-venv \
            \
            `# OpenGL / EGL (QOpenGLWidget, Qt Charts, Wayland render offload)` \
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
        info "Required: cmake ninja g++ python3 nodejs libva-dev libpulse-dev libegl-dev"
    fi
elif [ "$PLATFORM" = "macos" ]; then
    if ! command -v brew &>/dev/null; then
        [ "$CI_MODE" = true ] && fail "Homebrew not found in CI environment."
        info "Homebrew not found. Installing..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    fi
    # yt-dlp installed system-wide via brew on macOS; standalone binary used on Linux
    brew install cmake ninja python@3.11 openssl@3 node yt-dlp portaudio
fi
ok

# ── Step 2/8: Verify compiler ───────────────────────────────
echo "[2/8] Checking C++ compiler..."
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
CMAKE_VER="$(cmake --version | head -1 | awk '{print $3}')"
echo "  cmake ${CMAKE_VER}"
version_ge "$CMAKE_VER" "$CMAKE_MIN" || fail "CMake ${CMAKE_MIN}+ required. Found ${CMAKE_VER}. Download from https://cmake.org/download/"
ok

# ── Step 4/8: Verify Python ─────────────────────────────────
echo "[4/8] Checking Python..."
PYTHON="$(command -v python3.11 || command -v python3 || true)"
[ -n "$PYTHON" ] || fail "python3 not found."
PY_VER="$($PYTHON -c 'import sys; print("%d.%d.%d" % sys.version_info[:3])')"
echo "  python ${PY_VER}"
version_ge "$PY_VER" "$PYTHON_MIN" || fail "Python ${PYTHON_MIN}+ required. Found ${PY_VER}."
ok

# ── Step 5/8: Verify Node.js (yt-dlp JS runtime) ───────────
echo "[5/8] Checking Node.js..."
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

# ── Step 6/8: Locate / install Qt ${QT_VERSION} ─────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QT_INSTALL_ROOT="${FINCEPT_QT_ROOT:-$SCRIPT_DIR/.qt}"
QT_PREFIX="$QT_INSTALL_ROOT/$QT_VERSION/$QT_KIT"

echo "[6/8] Locating Qt ${QT_VERSION}..."
if [ -n "${Qt6_DIR:-}" ] && [ -f "$Qt6_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    QT_PREFIX="$Qt6_DIR"
    info "Using Qt from Qt6_DIR env: $QT_PREFIX"
elif [ -f "$QT_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    info "Qt ${QT_VERSION} already installed at $QT_PREFIX"
else
    info "Installing Qt ${QT_VERSION} via aqtinstall to $QT_INSTALL_ROOT ..."
    AQT_VENV="$SCRIPT_DIR/.aqt-venv"
    "$PYTHON" -m venv "$AQT_VENV"
    "$AQT_VENV/bin/pip" install --quiet --upgrade aqtinstall
    AQT="$AQT_VENV/bin/aqt"
    [ -x "$AQT" ] || fail "aqtinstall did not install correctly."
    if [ "$PLATFORM" = "linux" ]; then
        AQT_HOST="linux" ; AQT_TARGET="desktop" ; AQT_ARCH="linux_gcc_64"
    else
        AQT_HOST="mac"   ; AQT_TARGET="desktop" ; AQT_ARCH="clang_64"
    fi
    # Modules: charts, websockets, multimedia (video+audio), speech, opengl widgets,
    # webengine (Chromium-based iframe player for YouTube live streams — ~500 MB,
    # eliminates yt-dlp dependency and all GL/Wayland rendering complexity).
    AQT_MODULES="qtcharts qtwebsockets qtmultimedia qtspeech qtpositioning qtwebchannel qtwebengine"
    "$AQT" install-qt "$AQT_HOST" "$AQT_TARGET" "$QT_VERSION" "$AQT_ARCH" \
        --outputdir "$QT_INSTALL_ROOT" \
        --modules $AQT_MODULES \
        || fail "aqtinstall failed. Check internet or install Qt ${QT_VERSION} from https://www.qt.io/download-qt-installer"
    [ -f "$QT_PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ] \
        || fail "Qt install completed but Qt6Config.cmake not found at $QT_PREFIX"
fi
export CMAKE_PREFIX_PATH="$QT_PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
echo "  CMAKE_PREFIX_PATH=$QT_PREFIX"
ok

# ── Step 7/8: Configure ─────────────────────────────────────
APP_DIR="$SCRIPT_DIR/fincept-qt"
[ -d "$APP_DIR" ] || fail "fincept-qt directory not found. Ensure you cloned the full repository."
cd "$APP_DIR"

echo "[7/8] Configuring (preset: $PRESET)..."
EXTRA_ARGS=""
if [ "$PLATFORM" = "macos" ] && [ -d "/opt/homebrew/opt/openssl@3" ]; then
    EXTRA_ARGS="-DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3"
fi
cmake --preset "$PRESET" -DCMAKE_PREFIX_PATH="$QT_PREFIX" $EXTRA_ARGS \
    || fail "CMake configure failed. See error above."
ok

# ── Step 8/8: Build ─────────────────────────────────────────
echo "[8/8] Compiling..."
cmake --build --preset "$PRESET" || fail "Build failed. See error above."
ok

# ── Post-build: install yt-dlp next to binary ───────────────
BUILD_DIR="$APP_DIR/build/$PRESET"
YT_DLP_BIN="$BUILD_DIR/yt-dlp"

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

if [ -x "$YT_DLP_BIN" ]; then
    INSTALLED_VER="$("$YT_DLP_BIN" --version 2>/dev/null || echo 'unknown')"
    info "yt-dlp already present: ${INSTALLED_VER} — checking for update..."
    # Re-download to ensure we have the latest (yt-dlp releases frequently)
    install_ytdlp_binary
fi
install_ytdlp_binary
YTDLP_VER="$("$YT_DLP_BIN" --version 2>/dev/null || echo 'unknown')"
info "yt-dlp ${YTDLP_VER} → $YT_DLP_BIN"
ok

# ── Done ────────────────────────────────────────────────────
BIN="$BUILD_DIR/FinceptTerminal"
[ "$PLATFORM" = "macos" ] && BIN="$BUILD_DIR/FinceptTerminal.app/Contents/MacOS/FinceptTerminal"

echo ""
echo "================================================"
echo "  Build complete!"
echo "  Binary:  $BIN"
echo "  yt-dlp:  $YT_DLP_BIN ($(${YT_DLP_BIN} --version 2>/dev/null))"
if command -v node &>/dev/null; then
    echo "  Node.js: $(node --version) — YouTube JS extraction ready"
fi
echo "================================================"
echo ""

if [ "$CI_MODE" = true ]; then
    exit 0
fi

read -r -p "  Launch now? (y/n): " LAUNCH
if [[ "$LAUNCH" =~ ^[Yy]$ ]]; then
    "$BIN"
fi
