# finterm

A local-first, **offline-capable** financial-research terminal. Qt6/C++ desktop app + a thin Python data layer. No SaaS account, no cloud round-trips, no telemetry — only the public market-data APIs you explicitly use.

## Today's commits (2026-05-14)

Latest first.

- [`da6ced00`](https://github.com/yumima/finterm/commit/da6ced00) **fix(video):** normalise frames to RGBA8888 to fix texture update failure
- [`df6c489c`](https://github.com/yumima/finterm/commit/df6c489c) **chore(setup):** update setup.sh to current dependency requirements
- [`e9ed7b6e`](https://github.com/yumima/finterm/commit/e9ed7b6e) **fix(video):** install yt-dlp, add node JS runtime, use channel live URLs
- [`0cf7672a`](https://github.com/yumima/finterm/commit/0cf7672a) **fix(video/gl):** address all OpenGL review findings
- [`19b53e30`](https://github.com/yumima/finterm/commit/19b53e30) **feat(video):** GPU rendering via QOpenGLWidget + RTX PRIME offload
- [`a7c24e2a`](https://github.com/yumima/finterm/commit/a7c24e2a) **fix(calendar):** add re-entry guard to EconomicCalendarWidget::refresh_data()
- [`2585e4c9`](https://github.com/yumima/finterm/commit/2585e4c9) **fix:** address all review findings across calendar, video, and pre-IPO
- [`96fecc6d`](https://github.com/yumima/finterm/commit/96fecc6d) **fix(video):** replace QVideoWidget with QVideoSink+QPainter — eliminates native surface
- [`c4ce26ee`](https://github.com/yumima/finterm/commit/c4ce26ee) **fix(video):** stop()+setSource(QUrl()) released Wayland surface → rogue windows
- [`a3d815c9`](https://github.com/yumima/finterm/commit/a3d815c9) **feat(dashboard):** Economic Calendar live data + new IPO Calendar widget
- [`53f42b3a`](https://github.com/yumima/finterm/commit/53f42b3a) **fix(video+dashboard):** restore inline video, add portfolio_summary to template
- [`3a5d0c84`](https://github.com/yumima/finterm/commit/3a5d0c84) **fix(video):** restore inline video — root cause was stack page ordering
- [`5cbbc28a`](https://github.com/yumima/finterm/commit/5cbbc28a) **fix(video):** root-cause fix for Wayland window-spawn loop and shmem OOM
- [`1c970ab9`](https://github.com/yumima/finterm/commit/1c970ab9) **fix(video+chart):** address all review findings
- [`0322aaff`](https://github.com/yumima/finterm/commit/0322aaff) **fix(video+chart):** window-spawn loop, shmem OOM, chart bugs from review
- [`dd7b5aba`](https://github.com/yumima/finterm/commit/dd7b5aba) **fix(dashboard):** OOM crash in VideoPlayerWidget + optimise portfolio template
- [`55e7cb16`](https://github.com/yumima/finterm/commit/55e7cb16) **feat(portfolio):** gap-free chart x-axis via sequential bar indexing
- [`6b773de7`](https://github.com/yumima/finterm/commit/6b773de7) **fix(portfolio):** review findings — target coverage guard and dead field
- [`d6c8cad1`](https://github.com/yumima/finterm/commit/d6c8cad1) **fix(portfolio):** correct target NAV math; drop upside % from TGT MEAN
- [`efec30ab`](https://github.com/yumima/finterm/commit/efec30ab) **fix(portfolio):** fundamentals guard set before MV data available
- [`7928ff18`](https://github.com/yumima/finterm/commit/7928ff18) **fix(portfolio):** deadlock in load_summary emit-while-holding-mutex
- [`a7a7d9c2`](https://github.com/yumima/finterm/commit/a7a7d9c2) **fix(portfolio):** three review findings on fundamentals panel
- [`b98f4621`](https://github.com/yumima/finterm/commit/b98f4621) **feat(portfolio):** analyst-target panel in heatmap for portfolio-level view
- [`9f42c23a`](https://github.com/yumima/finterm/commit/9f42c23a) **feat(markets):** replace India with Europe panel; add HKD/TWD/SGD forex pairs
- [`499a4d7a`](https://github.com/yumima/finterm/commit/499a4d7a) **chore(hooks):** test post-commit docs follow-up
- [`897f6ea9`](https://github.com/yumima/finterm/commit/897f6ea9) **chore(hooks):** post-commit creates docs(readme) follow-up for today's commits
- [`349ecccb`](https://github.com/yumima/finterm/commit/349ecccb) **fix(portfolio-chart):** correct 1D aggregate, 1W focus, and session anchor

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
