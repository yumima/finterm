# finterm

A local-first, **offline-capable** financial-research terminal. Qt6/C++ desktop app + a thin Python data layer. No SaaS account, no cloud round-trips, no telemetry — only the public market-data APIs you explicitly use.

## Today's commits (2026-05-18)

Latest first.

- [`b819355a`](https://github.com/yumima/finterm/commit/b819355a) services(video): LiveHlsProxy review fixes — cache aging, no-copy, tests
- [`59a0e513`](https://github.com/yumima/finterm/commit/59a0e513) services(video): live-HLS trimming proxy — fix Bloomberg/CNBC freeze in GL mode
- [`e85d4c07`](https://github.com/yumima/finterm/commit/e85d4c07) services(finnhub): WS source-side throttle — fix GUI thread CPU storm
- [`d1c8aab4`](https://github.com/yumima/finterm/commit/d1c8aab4) screens(ipo): PRIVATE dedup + Wikipedia summaries for private companies
- [`7b78fb79`](https://github.com/yumima/finterm/commit/7b78fb79) **ui(widgets):** StatusPill — shared idle/loading/stale/error/empty pill
- [`4fbd59ea`](https://github.com/yumima/finterm/commit/4fbd59ea) services(query): LRU eviction so long sessions don't grow unbounded
- [`b3ca1b83`](https://github.com/yumima/finterm/commit/b3ca1b83) screens(ipo): PRIVATE star + in-app detail rail + mixed WATCHLIST
- [`5ac16d27`](https://github.com/yumima/finterm/commit/5ac16d27) screens(er): keyboard shortcuts for chart actions (perf/UX #1)
- [`0458f1ac`](https://github.com/yumima/finterm/commit/0458f1ac) screens(ipo): PRIVATE lens + clearer LOCKUPS empty-state messaging
- [`9369ef4d`](https://github.com/yumima/finterm/commit/9369ef4d) services(finnhub): live-tick WebSocket → ER price push (Round 4)
- [`8b92d6ee`](https://github.com/yumima/finterm/commit/8b92d6ee) screens(futures): CHINA pre-warm + 60s cache for akshare commodities
- [`173b95e4`](https://github.com/yumima/finterm/commit/173b95e4) screens(er): multi-symbol comparison overlay on the chart (Pass 5)
- [`79a3c122`](https://github.com/yumima/finterm/commit/79a3c122) screens(er): saved chart view per ticker
- [`48d23bc8`](https://github.com/yumima/finterm/commit/48d23bc8) screens(er): custom date-range picker for the chart
- [`3bb18995`](https://github.com/yumima/finterm/commit/3bb18995) screens(ipo): LOCKUPS lens — Finnhub-sourced post-IPO supply-surge signal
- [`fec34036`](https://github.com/yumima/finterm/commit/fec34036) screens(er): rich search popup — symbol/name + venue/ccy/type chips
- [`9911e69d`](https://github.com/yumima/finterm/commit/9911e69d) review-noted: tooltip + CHINA leftover guards
- [`39d14e63`](https://github.com/yumima/finterm/commit/39d14e63) screens(futures): CHINA uses the standard grid (no more special case)
- [`2ecd3b8d`](https://github.com/yumima/finterm/commit/2ecd3b8d) screens(er): analyst recommendation-trend chip (Finnhub)
- [`7ef8ac2b`](https://github.com/yumima/finterm/commit/7ef8ac2b) screens(er): market-status badge in quote bar (OPEN/PRE/AFTER/CLOSED)
- [`304165c8`](https://github.com/yumima/finterm/commit/304165c8) services(finnhub): review-noted — drop junk rows in two parsers
- [`cb96c759`](https://github.com/yumima/finterm/commit/cb96c759) screens(er): fix SMA50 crosshair label + dedup compute
- [`7ce42588`](https://github.com/yumima/finterm/commit/7ce42588) services(finnhub): FinnhubService + IPO/lockup augmentation in PreIpoService
- [`2fcb19ce`](https://github.com/yumima/finterm/commit/2fcb19ce) screens(er): Pass 1 chart upgrades — log scale + volume + SMA + earnings + comparators
- [`c3757889`](https://github.com/yumima/finterm/commit/c3757889) screens(er): migrate Analysis/Financials/News/Peers tabs to QueryStore
- [`0a354691`](https://github.com/yumima/finterm/commit/0a354691) screens(er): migrate Technicals tab to QueryStore
- [`1794b417`](https://github.com/yumima/finterm/commit/1794b417) services(query+equity): SWR + speculative period prefetch
- [`9378ff5d`](https://github.com/yumima/finterm/commit/9378ff5d) services(query): introduce QueryStore; migrate ER overview tab

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

**Optional — enable tracked git hooks.** This repo ships a pre-commit hook at `.githooks/pre-commit` that regenerates the "Today's commits" section near the top of this README (oldest-first, GitHub-linked) by calling `tools/update_todays_commits.py`. To turn it on in a fresh clone:

```bash
git config core.hooksPath .githooks
```

Per-commit opt-out: `git commit --no-verify`.

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
