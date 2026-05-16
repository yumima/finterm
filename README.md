# finterm

A local-first, **offline-capable** financial-research terminal. Qt6/C++ desktop app + a thin Python data layer. No SaaS account, no cloud round-trips, no telemetry — only the public market-data APIs you explicitly use.

## Today's commits (2026-05-16)

Latest first.

- [`ab58feeb`](https://github.com/yumima/finterm/commit/ab58feeb) screens(futures): debounce class-tab clicks + cap router timeout at 12s
- [`bc5c8c9b`](https://github.com/yumima/finterm/commit/bc5c8c9b) screens(futures+er): rail layout, inline Databento prompt, regions, ER relationships
- [`231929b5`](https://github.com/yumima/finterm/commit/231929b5) screens(fno): wire F&O into ToolBar + CommandBar menus
- [`77a8e1f6`](https://github.com/yumima/finterm/commit/77a8e1f6) screens(fno): port F&O analytics screen alongside Futures
- [`1c30883d`](https://github.com/yumima/finterm/commit/1c30883d) scrub: remove outbound paths to api.fincept.in (localhost-stance audit)
- [`9f7ff5da`](https://github.com/yumima/finterm/commit/9f7ff5da) mcp(tools): fix Tier 3 cherry-pick build errors
- [`65538d45`](https://github.com/yumima/finterm/commit/65538d45) **fix(trading):** load broker credentials on calling thread, not in worker
- [`9762d0da`](https://github.com/yumima/finterm/commit/9762d0da) **fix(broker/angelone):** refactor onto BrokerEnumMap + bug fixes
- [`0ae5bcb5`](https://github.com/yumima/finterm/commit/0ae5bcb5) **fix(broker/groww):** refactor onto BrokerEnumMap + bug fixes
- [`f4a0fdbf`](https://github.com/yumima/finterm/commit/f4a0fdbf) **fix(broker/aliceblue):** refactor onto BrokerEnumMap + bug fixes
- [`6e61d403`](https://github.com/yumima/finterm/commit/6e61d403) **fix(broker/dhan):** refactor onto BrokerEnumMap + bug fixes
- [`d455a9f6`](https://github.com/yumima/finterm/commit/d455a9f6) **fix(broker/alpaca):** refactor onto BrokerEnumMap + minor fixes
- [`698ceff1`](https://github.com/yumima/finterm/commit/698ceff1) trading: BrokerEnumMap + MTF + F&O OI fields
- [`fec59b64`](https://github.com/yumima/finterm/commit/fec59b64) mcp(tools): AgentsTools — agent / team / workflow / planner / memory
- [`8a943973`](https://github.com/yumima/finterm/commit/8a943973) mcp(tools): SurfaceAnalyticsTools — options vol surface (Databento)
- [`d43f3bf5`](https://github.com/yumima/finterm/commit/d43f3bf5) mcp(tools): QuantLabTools — factor models, backtests, attribution
- [`15ba9828`](https://github.com/yumima/finterm/commit/15ba9828) mcp(tools): GovDataTools — CKAN providers, datasets, resources
- [`3ab06933`](https://github.com/yumima/finterm/commit/3ab06933) mcp(tools): GeopoliticsTools — conflict events, sanctions, maritime
- [`a3332c7e`](https://github.com/yumima/finterm/commit/a3332c7e) mcp(tools): EquityResearchTools — fundamentals, sentiment, ratings
- [`e934d2a4`](https://github.com/yumima/finterm/commit/e934d2a4) mcp(tools): DBnomicsTools — economic data (providers/datasets/series)
- [`0d093e31`](https://github.com/yumima/finterm/commit/0d093e31) mcp(tools): MetaTools — tool discovery + introspection
- [`2662ced4`](https://github.com/yumima/finterm/commit/2662ced4) mcp(tools): AgenticMemoryTools — durable scratchpad for agent runs
- [`8da6cdb2`](https://github.com/yumima/finterm/commit/8da6cdb2) mcp: dual-shape dispatch — call_tool_async + AuthChecker + validation
- [`2f5d281d`](https://github.com/yumima/finterm/commit/2f5d281d) mcp: SchemaValidator — validate + default-inject tool args
- [`1a3f3dc9`](https://github.com/yumima/finterm/commit/1a3f3dc9) mcp: add dual-shape ToolDef + ToolContext + ToolSchemaBuilder + AsyncDispatch
- [`99817b73`](https://github.com/yumima/finterm/commit/99817b73) **ui:** ToastService + ErrorPipeline — in-app toast queue and error aggregation
- [`67e0fc29`](https://github.com/yumima/finterm/commit/67e0fc29) **feat(voice):** local tts (pyttsx3) + clap detector
- [`1d84c5c2`](https://github.com/yumima/finterm/commit/1d84c5c2) **feat(analytics):** statsmodels_service entry point
- [`f843f564`](https://github.com/yumima/finterm/commit/f843f564) **feat(derivatives):** option_greeks_daemon — IV + Greeks via py_vollib
- [`59f7c462`](https://github.com/yumima/finterm/commit/59f7c462) **feat(crypto):** Kraken symbol resolver via ccxt
- [`9bdb708b`](https://github.com/yumima/finterm/commit/9bdb708b) **feat(data):** FII/DII daily flows scraper (NSE cash market)
- [`5b561d8f`](https://github.com/yumima/finterm/commit/5b561d8f) **feat(data):** Taiwan TWSE/TPEX market connector
- [`01cb572d`](https://github.com/yumima/finterm/commit/01cb572d) **feat(markets):** Copy Symbol right-click on market panel rows
- [`95df6b86`](https://github.com/yumima/finterm/commit/95df6b86) **feat(news):** COPY TITLE button on article detail panel
- [`02cd4fa8`](https://github.com/yumima/finterm/commit/02cd4fa8) **feat(watchlist):** Export + Import CSV

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
