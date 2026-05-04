# finterm

A local-first, **offline-capable** financial-research terminal. Qt6/C++ desktop app + a thin Python data layer. No SaaS account, no cloud round-trips, no telemetry — only the public market-data APIs you explicitly use.

## What you get

- **Markets / Watchlist / News** — live quotes via yfinance; optional Stooq for fast EOD.
- **Portfolio** — multi-account holdings, P&L, sparklines, heatmap, blotter, and a *Futures & Ext Hours* sub-view.
- **Equity Research** — financials, analyst targets, technicals (RSI / MACD / EMA / stochastic / ATR), peers, news, sentiment.
- **FUTURES** — 8-asset-class watchlist (Index · Rates · Energy · Metals · Ags · FX · Crypto · China), heatmap, term structure, spread monitor, settlements, continuous chart.
- **KNOWLEDGE** — 5-pane cockpit (Glossary · Concepts · Tracks · Cases · Playbooks · Abbreviations) with curated content, live data lookups, formula calculators, peer comparison charts, and an embedded AI tutor.
- **AI chat bubble** — pluggable LLM provider (OpenAI / Anthropic / local Ollama / etc.); none wired by default.
- **Persistent yfinance daemon** — long-lived Python child; eliminates ~1.5s per-request import cost.
- **Boot prefetch** — futures cache and active portfolios warmed at startup.
- **Component browser, dock layouts, multi-window, workspaces** — full Qt-Advanced-Docking.
- **Desktop launcher** — `finterm install` registers a `.desktop` entry pinnable to your dock.

## Data flow

finterm is **data-in only**:

- **Outbound, on your behalf** — Yahoo Finance, Stooq, akshare (China futures only). Quotes/history flow *into* the app.
- **Outbound, never** — no analytics, no crash reporter, no SaaS backend, no LLM unless *you* configure one in Settings → Agent.
- **Inbound only** — auth/profile/credits/MFA resolve against `127.0.0.1:8765` (a stdlib-only Python stub).
- **Local only** — everything else lives in `~/.local/share/com.fincept.terminal/` (SQLite + JSON) and `~/.config/Fincept/`.

## Architecture

```
finterm/
├── finterm.sh                      ← one-stop CLI: start / build / reset / install / stop / status
├── setup.sh                        ← preflight: Qt / cmake / Python toolchain
├── fincept-qt/                     ← Qt6/C++ application
│   ├── src/                        ← C++ source
│   ├── scripts/                    ← Python data layer (yfinance, akshare, stooq, …)
│   ├── resources/knowledge/        ← KNOWLEDGE tab content (markdown + JSON manifests)
│   └── build/<preset>/             ← compiled binary lands here
└── tools/
    ├── local_stub/server.py        ← localhost backend (auth/profile/credits stubs)
    └── systemd/finterm-stub.service ← optional user unit to keep the stub running
```

Two processes run when you launch:

1. **Local stub** (`tools/local_stub/server.py`) — listens on `127.0.0.1:8765`. Stdlib only. Stays alive across launches.
2. **FinceptTerminal binary** — the Qt app. Spawns its own Python child for market-data work (`yfinance_data.py --daemon`).

Neither talks to anything outside `127.0.0.1` except the data scripts you invoke.

## How to run

### Platform support

Linux is regularly tested (Ubuntu / Debian-likes, x86-64). macOS and Windows build but are unverified — expect minor friction. `finterm.sh` is bash; on Windows, run under WSL or invoke the binary directly.

### Prerequisites

- Qt 6.8.3 (`setup.sh` provisions via `aqtinstall` if absent).
- CMake 3.27+, GCC 12.3+ / Clang 15+ / MSVC 2022.
- Python 3.11+ (system Python is fine for the stub).
- A separate venv for the data scripts — `setup.sh` provisions it under `~/.local/share/com.fincept.terminal/`. ~110 packages, ~5–15 minutes the first time.

### First-time setup

```bash
git clone <this-repo-url> ~/fin/finterm
cd ~/fin/finterm
./setup.sh                     # checks toolchain, provisions venvs, fetches Qt if needed
./finterm.sh build             # configure + build the Qt binary
```

`setup.sh` installs system deps on Linux/macOS automatically (build tools, OpenGL/xkbcommon/dbus, `portaudio19-dev` for AI voice input, etc.).

### `finterm.sh` — one-stop CLI

Everything day-to-day goes through `finterm.sh`. Symlink it once:

```bash
ln -sf ~/fin/finterm/finterm.sh ~/bin/finterm
finterm help
```

| Command | What it does |
|---|---|
| `finterm` *(or `finterm start`)* | Launch the Qt app + localhost stub. |
| `finterm build` | Incremental cmake/ninja build. |
| `finterm build --clean` | Clean rebuild. |
| `finterm build --tests` | Configure with `-DFINCEPT_BUILD_TESTS=ON`. |
| `finterm reset` | Strip Qt window/dock state only — ~1s, preserves everything else. Run on every `start`. |
| `finterm reset --config-only` | Back up entire `~/.config/Fincept/`. Python venv preserved. |
| `finterm reset --full` | Nuke Qt config + Python venv + caches. Reprovisions ~110 pip packages on next launch. |
| `finterm reset --list` / `--restore <ts>` | List / roll back prior reset backups. |
| `finterm install` | Register a `.desktop` launcher (pinnable to your dock). |
| `finterm uninstall` | Remove the `.desktop` launcher. |
| `finterm stop` | `pkill` the Qt binary and the stub. |
| `finterm status` | Print what's running + stub `/health` check. |

What `finterm start` does:

1. **Strips Qt window/dock layout state** from `~/.config/Fincept/FinceptTerminal.conf`. Surgical: portfolio/watchlists/theme/workspaces preserved. Skip with `FINCEPT_KEEP_WINDOW=1 finterm`.
2. **Starts the localhost stub** if not already running, via `nohup`. Detects existing instances (manual, finterm-managed, or systemd-managed). Stub log: `~/.cache/fincept-stub.log`.
3. **Execs the Qt binary in the foreground.** Closing the window returns control to your shell; the stub keeps running.

### Stub server lifecycle

The stub (`tools/local_stub/server.py`) services auth/profile/credits requests on `127.0.0.1:8765`. It's stdlib-only and stores users in `~/.fincept-localhost/users.db`. Three ways to manage it:

```bash
# Manual (foreground, with logs)
python3 tools/local_stub/server.py

# Manual (background)
nohup python3 tools/local_stub/server.py > ~/.cache/fincept-stub.log 2>&1 &

# Systemd user unit (auto-starts on login, restarts on failure)
mkdir -p ~/.config/systemd/user
cp tools/systemd/finterm-stub.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now finterm-stub.service

# Verify
curl http://127.0.0.1:8765/health
# → {"status":"ok"}

# Logs (systemd path)
journalctl --user -u finterm-stub -f

# Stop / disable
systemctl --user disable --now finterm-stub.service
```

`finterm start` auto-detects an existing stub (manual, nohup, or systemd) and won't start a duplicate. The stub accepts any 6-digit OTP (the "real" code is logged to stdout) and any password reset / MFA flow without external email. Endpoints not relevant to local use return empty success envelopes.

### Windows

```powershell
python tools\local_stub\server.py &
fincept-qt\build\windows-release\FinceptTerminal.exe
```

### Verify a launch

- Window title reads **`finterm @ <hostname>`**.
- Top status bar shows **● LIVE** in green and `DATA: YAHOO ▾`.
- `finterm status` reports the Qt binary, the data daemon, and the stub all running with `/health` returning 200.

### Recovery

`finterm reset` covers ~95% of breakage. Portfolio data lives in SQLite under `~/.local/share/com.fincept.terminal/data/` and is never moved. Every destructive reset moves the affected dir to `<dir>.bak.<unix-ts>` rather than deleting outright.

| Symptom | Try |
|---|---|
| Two floating empty windows on launch / PIN screen behind another | `finterm reset` (default = window-only) |
| Stuck on a weird theme / layout | `finterm reset --config-only` |
| Setup keeps offering "Install Analytics Libraries" | Install `portaudio19-dev` (or distro equivalent), then `finterm reset --full` once. |
| Daemon hung / Yahoo timing out | `pkill -f "yfinance_data.py --daemon"` — the Qt app respawns it. |
| Stub returning 500s | `finterm stop && finterm` |

## License & attribution

Licensed under **AGPL-3.0** (see [`LICENSE`](LICENSE)).

finterm is derived from [Fincept Corporation's FinceptTerminal](https://github.com/Fincept-Corporation/FinceptTerminal). The Qt UI and most domain code originate there; this fork strips the SaaS dependency, replaces it with a localhost stub, adds the FUTURES + KNOWLEDGE tabs, and the various local-first conveniences described above. AGPL-3.0 inherits.
