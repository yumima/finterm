# finterm

A local-first, **offline-capable** financial-research terminal — Qt6/C++ desktop app plus a thin Python data layer. Everything runs on your machine: no SaaS account, no cloud round-trips, no telemetry. The only outbound traffic is the public market-data APIs you explicitly use (Yahoo Finance, Stooq, akshare).

## What you get

- **Markets / Watchlist / News** — live quotes via yfinance with an optional Stooq toggle for fast EOD pulls.
- **Portfolio** — multi-account holdings, transactions, P&L, sparklines, heatmap, blotter. Plus a *Futures & Ext Hours* sub-view that filters futures-style symbols and shows pre/post-market values.
- **Equity Research** — quote bar, financials, analyst targets, technical indicators (RSI / MACD / EMA / stochastic / ATR), peers, news, sentiment. Right-click a holding → opens ER side-by-side; symbol search is inline in the panel header.
- **FUTURES top-level tab** — 8 asset-class watchlist (Index, Rates, Energy, Metals, Ags, FX, Crypto, China), heatmap, term structure, spread monitor, settlements, and continuous chart. Backed by a shared in-memory quote cache.
- **Persistent yfinance daemon** — long-lived Python child process; cuts the ~1.5s pandas/yfinance import cost off every request.
- **Boot prefetch** — futures cache and active portfolios are warmed at startup, so first-click feels instant.
- **AI chat bubble** — drag-to-anywhere, position persisted; pluggable LLM provider (OpenAI / Anthropic / local Ollama / etc., configured in Settings → Agent). No provider is wired by default.
- **Component browser, dock layouts, multi-window, workspaces** — full Qt-Advanced-Docking layout with per-screen state save/restore.

## Data flow

finterm is **data-in only**: the app fetches market data *from* public APIs but **never sends your data anywhere**. No portfolio holdings, no watchlists, no notes, no chat history, no usage telemetry, no error reports leave the machine.

Concretely:

- **Outbound, on your behalf:** Yahoo Finance (`query1.finance.yahoo.com`, `query2.finance.yahoo.com`) and Stooq (`stooq.com`) for quotes/history. akshare hits Chinese exchange endpoints when you use the China futures panel. These calls fetch data **into** the app.
- **Outbound, never:** no analytics provider, no crash reporter, no SaaS backend, no LLM/AI provider unless *you* configure one explicitly via Settings → Agent.
- **Inbound only:** auth, profile, credits, MFA, subscription, and account flows resolve against `127.0.0.1:8765` (a local Python stub). The stub is stdlib-only — no network code at all.
- **Local only:** portfolio, watchlists, notes, alerts, candles cache, chat history, settings — all in `~/.local/share/com.fincept.terminal/` (SQLite + json) and `~/.config/Fincept/`. Nothing in those directories is uploaded.

If you wire in an external LLM via Settings → Agent, prompt+response pairs go to whatever endpoint *you* configured — that is the one and only outbound channel under your direct control. Default is no AI provider.

## Architecture

```
finterm/                            ← this repo
├── start.sh                        ← single-command launcher (used by ~/bin/finterm)
├── setup.sh                        ← preflight: checks Qt / cmake / Python toolchain
├── fincept-qt/                     ← Qt6/C++ application
│   ├── src/                        ← C++ source
│   ├── scripts/                    ← Python data layer (yfinance, akshare, stooq, …)
│   ├── cmake/                      ← build helpers
│   └── build/<preset>/             ← compiled binary lands here
└── tools/
    ├── local_stub/server.py        ← localhost backend (auth/profile/credits stubs)
    ├── reset.sh                    ← state-recovery utility
    └── systemd/fincept-stub.service ← optional user unit to keep stub running
```

Two processes run when you launch:

1. **Local stub** (`tools/local_stub/server.py`, system Python) — listens on `127.0.0.1:8765`, fields auth/profile/credits/etc. requests. Stays alive across launches.
2. **FinceptTerminal** (`fincept-qt/build/<preset>/FinceptTerminal`) — the Qt app. Spawns its own Python child for market-data work (`yfinance_data.py --daemon`).

Neither process talks to anything outside `127.0.0.1` except the data scripts you explicitly invoke (Yahoo Finance, Stooq, akshare).

## How to run

### Platform support

The codebase and CMake presets target **Linux, macOS, and Windows**, but only **Linux** is regularly tested by the maintainer (Ubuntu / Debian-likes, x86-64). macOS (`macos-release` preset) and Windows (`windows-release` preset, MSVC 2022) should build but are unverified — expect minor friction around platform-specific dependencies (audio, system tray, codecs). Reports / patches welcome.

The launcher (`start.sh`) and reset utility (`tools/reset.sh`) are bash scripts. On Windows you'd launch the Qt binary directly from the `windows-release` build dir; the localhost stub still works (it's plain Python).

### Prerequisites

- Qt 6.8.3 (the `setup.sh` script provisions it via `aqtinstall` under `<repo>/.qt/6.8.3/<kit>/` if not already present).
- CMake 3.27+, GCC 12.3+ / Clang 15+ / MSVC 2022.
- Python 3.11+ (system python is fine for the stub).
- A separate Python venv for the data scripts — `setup.sh` provisions it under `~/.local/share/com.fincept.terminal/`. ~110 packages, ~5–15 minutes the first time.

### First-time setup

```bash
git clone <this-repo-url> ~/fin/finterm
cd ~/fin/finterm
./setup.sh                     # checks toolchain, provisions venvs, fetches Qt if needed
cmake --preset linux-release   # or macos-release / windows-release
cmake --build --preset linux-release
```

`setup.sh` installs system dependencies on Linux/macOS automatically (build tools, OpenGL/xkbcommon/dbus, `portaudio19-dev` for the AI chat voice-input feature, etc.) — see the script for the full list per package manager.

### Launch (Linux/macOS)

```bash
# Convenience: symlink so `finterm` works from anywhere
ln -s ~/fin/finterm/start.sh ~/bin/finterm
finterm
```

What `start.sh` does, in order:

1. **Strips Qt window/toolbar/dock-layout state** from `~/.config/Fincept/FinceptTerminal.conf` via `tools/reset.sh --window-only`. Surgical: portfolio, watchlists, theme, workspaces, and component usage stats are preserved. Defends against layout corruption persisting across launches (duplicate floating windows, focus stolen from PIN screen). Skip with `FINCEPT_KEEP_WINDOW=1 finterm` if you want Qt to remember a hand-arranged layout.
2. **Starts the localhost stub if not already running**, via `nohup`. Detects an existing stub started by `start.sh`, by hand, or by the optional systemd user unit (`tools/systemd/fincept-stub.service`). Stub log: `~/.cache/fincept-stub.log`.
3. **Execs the Qt binary in the foreground.** Closing the window returns control to your shell; the stub keeps running so the next launch is instant.

### Launch (Windows)

The bash launcher doesn't run on plain `cmd.exe`. Either run it under WSL/MSYS, or do it by hand:

```powershell
python tools\local_stub\server.py &        # in one shell
fincept-qt\build\windows-release\FinceptTerminal.exe
```

### Verify

After launch:

- The window title reads **`finterm @ <hostname>`**.
- The top status bar shows **● LIVE** in green and `DATA: YAHOO ▾`.
- `pgrep -af FinceptTerminal` shows the Qt binary.
- `pgrep -af "yfinance_data.py --daemon"` shows the data daemon (spawned by the Qt app).
- `pgrep -af "tools/local_stub/server.py"` shows the localhost stub.
- `curl http://127.0.0.1:8765/health` returns 200.

### Tear down

```bash
pkill -f FinceptTerminal              # kills the Qt app + its data daemon child
pkill -f tools/local_stub/server.py   # kills the localhost stub
```

### Reset / recovery

If the app gets into a bad state — duplicate floating windows after a layout edit, stuck PIN screen, broken dock layout, "Install Analytics Libraries" prompt looping on every launch — use `tools/reset.sh`.

```bash
tools/reset.sh                  # default: strip Qt window/dock/toolbar state only
                                # (~1s, preserves portfolio + venv + everything else)
tools/reset.sh --window-only    # explicit form of the default; what start.sh runs
tools/reset.sh --full           # nuke Qt config + Python venv + caches
                                # (next launch reprovisions ~110 pip packages, 5–15 min)
tools/reset.sh --list           # show timestamped backups created by previous resets
tools/reset.sh --restore <ts>   # roll back a specific reset
```

Every destructive reset moves the affected directory aside as `<dir>.bak.<unix-ts>` rather than deleting outright, so `--restore` is always available. None of these touch your portfolio data — that lives in SQLite under `~/.local/share/com.fincept.terminal/data/` and is never moved or rebuilt.

| Symptom | Try |
|---|---|
| Two floating empty windows on launch / PIN screen behind another window | `tools/reset.sh` (`--window-only` is the default) |
| App is fine but stuck on a weird theme / layout | `tools/reset.sh` |
| Setup screen keeps offering "Install Analytics Libraries" every launch | Install `portaudio19-dev` (or distro equivalent), then `tools/reset.sh --full` once. PyAudio's system dep is now in `setup.sh`, so a fresh clone won't hit this. |
| Daemon hung / Yahoo calls timing out | `pkill -f "yfinance_data.py --daemon"` — the Qt app respawns it on next request |
| Stub returning 500s | `pkill -f tools/local_stub/server.py` then relaunch via `finterm`; `start.sh` will restart the stub |

## Notes on the localhost stub

`tools/local_stub/server.py` is **stdlib only** (no pip deps) and stores users in `~/.fincept-localhost/users.db` (SQLite). It accepts:

- Any 6-digit OTP code (the "real" code is logged to stdout for convenience).
- Any password reset / MFA flow without external email.
- Any subscription tier you set via `/user/set-tier`.

Endpoints not relevant to local use (marine, research, quantlib, chat, forum) return empty success envelopes so the UI doesn't show errors for features that need a server-side backend.

## License & attribution

Licensed under **AGPL-3.0** (see [`LICENSE`](LICENSE)).

finterm is derived from [Fincept Corporation's FinceptTerminal](https://github.com/Fincept-Corporation/FinceptTerminal). The Qt UI and most domain code originate there; this repo strips the SaaS/cloud dependency, replaces it with a localhost stub, and adds the futures tab, performance work, and various local-first conveniences described above. AGPL-3.0 inherits.
