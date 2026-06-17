# finterm

A local-first, **AI-native**, **offline-capable** financial-research terminal. Qt6/C++ desktop app + a thin Python data layer, with an agent / MCP / skill stack wired into the primary surfaces. No SaaS account, no cloud round-trips, no telemetry — only the public market-data APIs (and the LLM provider) you explicitly use.

## Today's commits (2026-06-16)

Latest first.

- [`55c2c03bd`](https://github.com/yumima/finterm/commit/55c2c03bd) No fabricated Databento contango baseline or demo-portfolio JSON template
- [`fcd297c48`](https://github.com/yumima/finterm/commit/fcd297c48) No fake $1000 paper fills: market orders fill at the real price or reject
- [`d606fa684`](https://github.com/yumima/finterm/commit/d606fa684) Power-trader: real price-based returns (replace fabricated sector-constant P&L)
- [`379d72039`](https://github.com/yumima/finterm/commit/379d72039) Remove the demo portfolio (fabricated holdings/cost-basis)
- [`5079b8f05`](https://github.com/yumima/finterm/commit/5079b8f05) No fake order-book data: show only real broker bid/ask, never synthesize depth
- [`7666359fa`](https://github.com/yumima/finterm/commit/7666359fa) Move dashboard portfolio selector to the title bar; fix power-trader rankings drawer dismiss
- [`12480c00f`](https://github.com/yumima/finterm/commit/12480c00f) Fix symbol search app-wide and wire dashboard portfolio to real data

[See all commits →](https://github.com/yumima/finterm/commits/main)

## AI / MCP / agentic stack

A power user can run `claude` + the `financial-services` MCPs
standalone and get part of what finterm offers.  Bundling them
into the terminal earns its weight on **four properties** the CLI
can't promise:

1. **Shared state surface.**  The agent sees the *same* watchlist,
   portfolio, paper-trade book, scheduler, and kill-switch you
   edit.  No "paste your CSV" tax — your positions are an MCP
   resource the agent reads directly.
2. **Audit + safety as product.**  Every dispatch lands in
   `agent_traces` with prompts, tokens, cost, status, *and a
   per-turn tool-call timeline* (which tools the agent invoked,
   with duration + result preview).  Per-tool kill-switch refuses
   problem tools system-wide.  Per-agent daily USD cap refuses
   dispatch before tokens are spent.  `<untrusted>` wrapping + a
   system-prompt directive teach the model to treat external news /
   forum text as data.  Mark a turn *wrong* and finterm drafts a
   `SKILL.md` patch you review and apply — feedback closes the loop
   instead of evaporating.
3. **Local-first switch.**  One profile flip from Anthropic to a
   local OpenAI-compatible server (Ollama, vLLM, llama.cpp, LM
   Studio).  When positions can't leave the box, the local profile
   is *the* differentiator.  No silent failover — you choose.
4. **Deterministic across runtimes.**  Same slash commands,
   same named agents, same SKILL.md methodology, same tool
   catalog, same **multi-agent teams** on Anthropic or local.
   Define a team (one coordinator + N members), dispatch with
   `/team <name> <query>`, and the coordinator synthesises the
   members' lenses into one answer.  Swap the runtime; the
   surface doesn't move.  What *does* differ — Files API,
   citations, computer use — is laid out in the [capability
   matrix](docs/how-to/capabilities.md).

**Quick start (users):**
- [docs/how-to/ai-features.md](docs/how-to/ai-features.md) —
  configure a runtime, use slash commands, schedule agents,
  manage spend + kill-switches
- [docs/how-to/quant-features.md](docs/how-to/quant-features.md)
  — AI Quant Lab, paper trading, the quant_critic agent

**Architecture (engineers):**
- [plans/ai-stack.md](plans/ai-stack.md) — single current-state
  reference: runtimes, MCP surface, agents, skills, scheduler,
  safety + observability, storage, UI, track status, open work

[All commits →](https://github.com/yumima/finterm/commits/main)

## What you get

- **Markets / Watchlist / News** — live global quotes, charts, and a two-column news feed with a portfolio filter (PTF) that matches articles by ticker *or* company name, plus an on-demand TL;DR of the visible headlines via your LLM. Inline article body via reader-mode extraction.
- **Portfolio** — multi-account holdings, performance chart, heatmap, blotter, extended-hours sub-view, and CSV/JSON export + JSON import from the selector dropdown.
- **Equity Research** — per-ticker dashboard: financials, analyst targets, technicals, peers, news, sentiment. Chart adds log scale, volume, SMA, earnings markers, custom date ranges, saved per-ticker views, keyboard shortcuts, and a multi-symbol comparison overlay with hover prices. Live tick push via Finnhub WebSocket when configured.
- **Futures** — multi-asset-class watchlist with heatmap, term structure, spreads, settlements, and continuous charts. CHINA commodities via akshare with pre-warm + 60 s cache.
- **Power Trader** — STOCK Act congressional trading analytics with member drawers, signal scores, and side-by-side compare.
- **Pre-IPO** — private-market cockpit fed by public SEC EDGAR sources (Form D, S-1, mutual-fund N-PORT marks): picks, screener, company dossiers, IPO pipeline, and a LOCKUPS lens for post-IPO supply-surge signals (Finnhub).
- **Knowledge** — markets and quant curriculum with practice tools, formula calculators, and an embedded AI tutor. The quant catalogue ships 47 strategy entries (FX, factor, FI, vol, dispersion, macro, …) with intuition, math, and citations.
- **Live video** — embedded Bloomberg / CNBC HLS streams with pause/play, auto-pause on terminal lock, and user-pickable max resolution (480/720/1080).
- **AI chat bubble** — pluggable LLM provider; none wired by default.
- **Concurrent yfinance daemon** — long-lived Python child runs market-data fetches in parallel over a Unix domain socket; interactive requests are prioritised over background sweeps. A local `QueryStore` (SWR + LRU) fronts it for the ER tabs.
- **Workspaces & docking** — multi-window Qt-Advanced-Docking with persistable layouts; dock geometry and the last primary screen are restored across launches. `finterm install` adds a desktop launcher.

## Data flow

finterm is **data-in only**:

- **Outbound, on your behalf** — Yahoo Finance, Stooq, akshare (China futures only). Quotes/history flow *into* the app.
- **Outbound, never** — no analytics, no crash reporter, no SaaS backend, no LLM unless *you* configure one in Settings → Agent.
- **Local only** — auth/profile/session/passcode handled entirely in-process by `AuthService` (C++, backed by `auth.db`). Nothing leaves the machine.
- **Local only** — everything else lives in `~/.local/share/com.fincept.terminal/` (SQLite + JSON) and `~/.config/Fincept/`.

## Architecture

```
finterm/
├── finterm.sh                      ← one-stop CLI (Linux/macOS): setup / start / build / repair / reset / install / …
├── finterm.ps1                     ← same CLI for native Windows (PowerShell)
├── setup.sh                        ← deprecated shim → `finterm.sh setup`
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

1. **finterm binary** — the Qt app. Auth/profile/session run in-process via `AuthService` (no stub server). Spawns `yfinance_data.py --daemon --socket <path>` as a Python child for all market-data work; communicates via a Unix domain socket at `~/.local/share/com.fincept.terminal/runtime/yfinance.sock`. The daemon runs a `ThreadPoolExecutor` internally so multiple yfinance fetches execute concurrently.

Nothing talks to anything outside `localhost` except the data scripts and any LLM provider you explicitly configure.

## How to run

### Platform support

Linux is regularly tested (Ubuntu / Debian-likes, x86-64). macOS and Windows build but are less exercised — expect minor friction. `finterm.sh` drives Linux + macOS; **`finterm.ps1`** is the native-Windows equivalent (same commands).

### Prerequisites

- Qt 6.8+ — the **system** Qt (distro `qt6-*` packages, or Homebrew `qt` on macOS); `finterm setup` installs it. Not vendored into the tree.
- CMake 3.27+, GCC 12.3+ / Clang 15+ / MSVC 2022.
- Python 3.11+ (for the data scripts; no stub server needed).
- A separate venv for the data scripts — `finterm setup` provisions it under `~/.local/share/com.fincept.terminal/`. ~110 packages, ~5–15 minutes the first time.

### First-time setup

```bash
git clone <this-repo-url> ~/fin/finterm
cd ~/fin/finterm
./finterm.sh setup             # installs system deps + Qt, configures, and builds
```

`finterm setup` installs system deps on Linux/macOS automatically (build tools, the Qt 6 packages, OpenGL/xkbcommon/dbus, `portaudio19-dev` for AI voice input, etc.) and then configures and builds — no separate script.

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
| `finterm setup` *(`--ci`)* | First-run: install system deps + Qt, configure, build. |
| `finterm` *(or `finterm start`)* | Launch the Qt app (auth is in-process; no stub needed). |
| `finterm build` | Incremental cmake/ninja build. |
| `finterm build --clean` | Clean rebuild. |
| `finterm build --tests` | Configure with `-DFINCEPT_BUILD_TESTS=ON`. |
| `finterm repair` | **Fix a misbehaving app, keeping all your data.** Clears caches + transient state + stale window/dock layout, and rebuilds the Python venv only if a health check finds it broken. Keeps portfolio, API keys, GUI layout, data-source, workspaces. Takes an insurance snapshot first. |
| `finterm reset` | **Wipe everything to a clean install.** Confirms first (`-y` skips), snapshots your data to `~/.local/share/finterm-backups/` and prints copy-back commands, then clears config + share. |
| `finterm install` | Create a `.desktop` launcher (pinnable to your dock). |
| `finterm uninstall` | Remove the `.desktop` launcher. |
| `finterm stop` | `pkill` the Qt binary. |
| `finterm status` | Print what's running (Qt binary + data daemon). |

When something's wrong, reach for `finterm repair` — it fixes the common breakages (stale cache, corrupt layout, broken venv) without ever losing your portfolio, keys, or settings. Use `finterm reset` only when you want a genuinely clean install.

What `finterm start` does:

1. **Strips Qt window/dock layout state** from `~/.config/Fincept/finterm.conf`. Surgical: portfolio/watchlists/theme/workspaces preserved. Skip with `FINCEPT_KEEP_WINDOW=1 finterm`.
2. **Execs the Qt binary in the foreground.** Closing the window returns control to your shell.

### Auth & user accounts

Local-only multi-user, no email, no SaaS round-trip. Auth runs entirely in-process via `AuthService` (C++); users and sessions are stored in `~/.local/share/com.fincept.terminal/data/auth.db`. Each user's portfolio, watchlists, and settings live in their own isolated `users/<username>.db`.

**Sign-up flow:**
1. Launch the app → on first run, the login picker is empty; click **Create Account**.
2. Pick a username (2–32 chars, `[a-zA-Z0-9_-]`) and a 4–6 digit PIN, confirm the PIN, submit.
3. The login picker now lists your user — click it and enter the PIN to enter the terminal.

**Multiple users:** Each user on the same machine appears in the picker (sorted by last login). Pick yours, enter your PIN, and you see only your own data. Forgot your PIN? On the picker, select the user → "Forgot PIN? Reset this user" — the row and its per-user DB are wiped; sign up again to recreate.

### Windows

Use the PowerShell sibling **`finterm.ps1`** — same commands as `finterm.sh`, native to Windows (MSVC build, registry settings, Start Menu shortcut):

```powershell
.\finterm.ps1              # first run sets everything up, then launches
.\finterm.ps1 repair      # fix without losing data    .\finterm.ps1 reset   # clean wipe
.\finterm.ps1 install     # add a Start Menu shortcut
```
(`finterm.sh` is bash and only covers Linux/macOS; on Windows use `finterm.ps1`, or run the Linux flow under WSL.)

### Verify a launch

- Window title reads **`finterm @ <hostname>`**.
- Top status bar shows **● LIVE** in green and `DATA: YAHOO ▾`.
- `finterm status` reports the Qt binary and data daemon running.

### Recovery

`finterm repair` covers ~95% of breakage — it clears caches/transient state and rebuilds a broken venv while keeping your portfolio, API keys, GUI layout, and workspaces. Use `finterm reset` only for a genuinely clean install. Both snapshot your data to `~/.local/share/finterm-backups/` first (printed path), so nothing is lost. Portfolio data lives in SQLite under `~/.local/share/com.fincept.terminal/data/`.

| Symptom | Try |
|---|---|
| Two floating empty windows on launch / PIN screen behind another | `finterm repair` (resets the window layout, keeps your data) |
| Stuck on a weird theme / layout, stale cache, or general flakiness | `finterm repair` |
| Python venv / data scripts broken (e.g. "Install Analytics Libraries" keeps reappearing) | `finterm repair` — it rebuilds the venv if a health check finds it broken |
| Want a genuinely clean install | `finterm reset` (snapshots your data first) |
| Daemon hung / Yahoo timing out | `pkill -f "yfinance_data.py --daemon"` — the Qt app respawns it automatically. |
| Stale socket file after crash | `rm ~/.local/share/com.fincept.terminal/runtime/yfinance.sock` — cleared automatically on next launch too. |
| Auth DB locked / corrupt | Delete `~/.local/share/com.fincept.terminal/data/auth.db` and restart — every local user disappears; sign up again. |
| Forgot a user's PIN | Login picker → select the user → **Forgot PIN? Reset this user**. Per-user DB is removed too. |

## License

finterm is free, open-source software released under **AGPL-3.0** (see [`LICENSE`](LICENSE)) — free to download, use, modify, and redistribute under that license. It is a community project, not a company product.

finterm is a fully-local financial terminal: no email signup, no OTP, no paywall, no SaaS round-trip. It uses a local username + PIN multi-user picker and ships the FUTURES / POWER TRADER / KNOWLEDGE (with QUANT) tabs, the after-hours Portfolio heatmap mode, the 2-column News feed with PTF / TL;DR, a 47-entry quant strategy catalogue, and the various local-first conveniences described above.
