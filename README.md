# finterm

A local-first, **offline-capable** financial-research terminal. Qt6/C++ desktop app + a thin Python data layer. No SaaS account, no cloud round-trips, no telemetry — only the public market-data APIs you explicitly use.

## Today's commits (2026-05-13)

Oldest first.

- [`30e6fa9`](https://github.com/yumima/finterm/commit/30e6fa90) **fix(portfolio):** money-market tickers (FCASH, SPAXX, …) now render a flat $1 line on the 1D chart instead of a briefly-flashed line that disappears the moment the empty intraday fetch lands.
- [`763da2c`](https://github.com/yumima/finterm/commit/763da2cd) **ui(news):** command-bar reordered to **WIRE | PTF | CLST** so the portfolio filter sits next to the default view; CLST (clusters) lives on the far side.
- [`13c2546`](https://github.com/yumima/finterm/commit/13c25462) **docs(design):** new AI-stack wiring plan at `fincept-qt/docs/design/ai-stack-free-local.md` — two first-class paths (localhost LLM via a sibling project, *or* external API keys), no silent failover between them.
- [`b25e4fb`](https://github.com/yumima/finterm/commit/b25e4fb1) **perf(pre-ipo):** first-paint refresh cut from ~96 s to ~24 s. Form D fetcher parses 30 filings instead of 120; status label no longer overwrites the "loaded from cache" message during background pulls.
- [`f0126e7`](https://github.com/yumima/finterm/commit/f0126e75) **ui(news):** unseen-headline amber row tint is gone (its fade timer was disabled so the tint was pinned at peak brightness until the user hovered); the 3 px amber dot still flags new items.
- [`df89561`](https://github.com/yumima/finterm/commit/df89561e) **perf(pre-ipo):** opening the Pre-IPO screen no longer triggers a network refresh if the cached data is less than 24 h old. The manual Refresh button still forces a pull.
- [`9bad451`](https://github.com/yumima/finterm/commit/9bad451f) **feat(pre-ipo):** background auto-refresh every day at **05:00 America/New_York** while the app is running, so morning opens find fresh SEC data without a wait. DST handled automatically by re-arming from "now in ET" each tick.

[See all commits →](https://github.com/yumima/finterm/commits/main)

## What you get

- **Markets / Watchlist / News** — live global quotes, charts, and a two-column news feed with a portfolio filter (PTF) that matches articles by ticker *or* company name, plus an on-demand TL;DR of the visible headlines via your LLM.
- **Portfolio** — multi-account holdings, performance chart, heatmap, blotter, and an extended-hours sub-view.
- **Equity Research** — per-ticker dashboard: financials, analyst targets, technicals, peers, news, sentiment.
- **Futures** — multi-asset-class watchlist with heatmap, term structure, spreads, settlements, and continuous charts.
- **Power Trader** — STOCK Act congressional trading analytics with member drawers, signal scores, and side-by-side compare.
- **Pre-IPO** — private-market cockpit fed by public SEC EDGAR sources (Form D, S-1, mutual-fund N-PORT marks): picks, screener, company dossiers, IPO pipeline.
- **Knowledge** — markets and quant curriculum with practice tools, formula calculators, and an embedded AI tutor. The quant catalogue ships 47 strategy entries (FX, factor, FI, vol, dispersion, macro, …) with intuition, math, and citations.
- **AI chat bubble** — pluggable LLM provider; none wired by default.
- **Concurrent yfinance daemon** — long-lived Python child runs market-data fetches in parallel over a Unix domain socket; interactive requests are prioritised over background sweeps.
- **Workspaces & docking** — multi-window Qt-Advanced-Docking with persistable layouts; `finterm install` registers a desktop launcher.

## Data flow

finterm is **data-in only**:

- **Outbound, on your behalf** — Yahoo Finance, Stooq, akshare (China futures only). Quotes/history flow *into* the app.
- **Outbound, never** — no analytics, no crash reporter, no SaaS backend, no LLM unless *you* configure one in Settings → Agent.
- **Local only** — auth/profile/session/passcode handled entirely in-process by `AuthService` (C++, backed by `auth.db`). Nothing leaves the machine.
- **Local only** — everything else lives in `~/.local/share/com.fincept.terminal/` (SQLite + JSON) and `~/.config/Fincept/`.

## Architecture

```
finterm/
├── finterm.sh                      ← one-stop CLI: start / build / reset / install / stop / status
├── setup.sh                        ← preflight: Qt / cmake / Python toolchain
├── fincept-qt/                     ← Qt6/C++ application
│   ├── src/auth/AuthService.cpp    ← in-process auth (users stored in auth.db)
│   ├── src/                        ← C++ source
│   ├── scripts/                    ← Python data layer (yfinance, akshare, stooq, …)
│   ├── resources/knowledge/        ← KNOWLEDGE tab content (markdown + JSON manifests)
│   └── build/<preset>/             ← compiled binary lands here
└── tools/
    └── local_stub/server.py        ← DEPRECATED — kept for reference only, do not run
```

One process runs when you launch:

1. **FinceptTerminal binary** — the Qt app. Auth/profile/session run in-process via `AuthService` (no stub server). Spawns `yfinance_data.py --daemon --socket <path>` as a Python child for all market-data work; communicates via a Unix domain socket at `~/.local/share/com.fincept.terminal/runtime/yfinance.sock`. The daemon runs a `ThreadPoolExecutor` internally so multiple yfinance fetches execute concurrently.

Nothing talks to anything outside `localhost` except the data scripts and any LLM provider you explicitly configure.

## How to run

### Platform support

Linux is regularly tested (Ubuntu / Debian-likes, x86-64). macOS and Windows build but are unverified — expect minor friction. `finterm.sh` is bash; on Windows, run under WSL or invoke the binary directly.

### Prerequisites

- Qt 6.8.3 (`setup.sh` provisions via `aqtinstall` if absent).
- CMake 3.27+, GCC 12.3+ / Clang 15+ / MSVC 2022.
- Python 3.11+ (for the data scripts; no stub server needed).
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
| `finterm` *(or `finterm start`)* | Launch the Qt app (auth is in-process; no stub needed). |
| `finterm build` | Incremental cmake/ninja build. |
| `finterm build --clean` | Clean rebuild. |
| `finterm build --tests` | Configure with `-DFINCEPT_BUILD_TESTS=ON`. |
| `finterm reset` | Strip Qt window/dock state only — ~1s, preserves everything else. Run on every `start`. |
| `finterm reset --config-only` | Back up entire `~/.config/Fincept/`. Python venv preserved. |
| `finterm reset --full` | Nuke Qt config + Python venv + caches. Reprovisions ~110 pip packages on next launch. |
| `finterm reset --list` / `--restore <ts>` | List / roll back prior reset backups. |
| `finterm install` | Register a `.desktop` launcher (pinnable to your dock). |
| `finterm uninstall` | Remove the `.desktop` launcher. |
| `finterm stop` | `pkill` the Qt binary. |
| `finterm status` | Print what's running (Qt binary + data daemon). |

What `finterm start` does:

1. **Strips Qt window/dock layout state** from `~/.config/Fincept/FinceptTerminal.conf`. Surgical: portfolio/watchlists/theme/workspaces preserved. Skip with `FINCEPT_KEEP_WINDOW=1 finterm`.
2. **Execs the Qt binary in the foreground.** Closing the window returns control to your shell.

### Auth & user accounts

Local-only multi-user, no email, no SaaS round-trip. Auth runs entirely in-process via `AuthService` (C++); users and sessions are stored in `~/.local/share/com.fincept.terminal/data/auth.db`. Each user's portfolio, watchlists, and settings live in their own isolated `users/<username>.db`.

**Sign-up flow:**
1. Launch the app → on first run, the login picker is empty; click **Create Account**.
2. Pick a username (2–32 chars, `[a-zA-Z0-9_-]`) and a 4–6 digit PIN, confirm the PIN, submit.
3. The login picker now lists your user — click it and enter the PIN to enter the terminal.

**Multiple users:** Each user on the same machine appears in the picker (sorted by last login). Pick yours, enter your PIN, and you see only your own data. Forgot your PIN? On the picker, select the user → "Forgot PIN? Reset this user" — the row and its per-user DB are wiped; sign up again to recreate.

### Windows

```powershell
fincept-qt\build\windows-release\FinceptTerminal.exe
```

### Verify a launch

- Window title reads **`finterm @ <hostname>`**.
- Top status bar shows **● LIVE** in green and `DATA: YAHOO ▾`.
- `finterm status` reports the Qt binary and data daemon running.

### Recovery

`finterm reset` covers ~95% of breakage. Portfolio data lives in SQLite under `~/.local/share/com.fincept.terminal/data/` and is never moved. Every destructive reset moves the affected dir to `<dir>.bak.<unix-ts>` rather than deleting outright.

| Symptom | Try |
|---|---|
| Two floating empty windows on launch / PIN screen behind another | `finterm reset` (default = window-only) |
| Stuck on a weird theme / layout | `finterm reset --config-only` |
| Setup keeps offering "Install Analytics Libraries" | Install `portaudio19-dev` (or distro equivalent), then `finterm reset --full` once. |
| Daemon hung / Yahoo timing out | `pkill -f "yfinance_data.py --daemon"` — the Qt app respawns it automatically. |
| Stale socket file after crash | `rm ~/.local/share/com.fincept.terminal/runtime/yfinance.sock` — cleared automatically on next launch too. |
| Auth DB locked / corrupt | Delete `~/.local/share/com.fincept.terminal/data/auth.db` and restart — every local user disappears; sign up again. |
| Forgot a user's PIN | Login picker → select the user → **Forgot PIN? Reset this user**. Per-user DB is removed too. |

## License & attribution

Licensed under **AGPL-3.0** (see [`LICENSE`](LICENSE)).

finterm is derived from [Fincept Corporation's FinceptTerminal](https://github.com/Fincept-Corporation/FinceptTerminal). The Qt UI and most domain code originate there; this fork strips the SaaS dependency (no email signup, no OTP, no paywall), replaces it with a local username + PIN multi-user picker, adds the FUTURES / POWER TRADER / KNOWLEDGE (with QUANT) tabs, the after-hours Portfolio heatmap mode, the 2-column News feed with PTF / TL;DR, a 47-entry quant strategy catalogue, and the various local-first conveniences described above. AGPL-3.0 inherits.
