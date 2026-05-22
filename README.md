# finterm

A local-first, **offline-capable** financial-research terminal. Qt6/C++ desktop app + a thin Python data layer. No SaaS account, no cloud round-trips, no telemetry — only the public market-data APIs you explicitly use.

## Today's commits (2026-05-22)

Latest first.

- [`853c1e95`](https://github.com/yumima/finterm/commit/853c1e95) **ci(ai):** nightly SDK eval + vendor-skills diff (Track 1C)
- [`7e6d6957`](https://github.com/yumima/finterm/commit/7e6d6957) ai(capabilities): declarative capability matrix + generator (Track 1A)
- [`839d9461`](https://github.com/yumima/finterm/commit/839d9461) mcp: server-initiated notification dispatch on McpClient (stdio)
- [`37f5af04`](https://github.com/yumima/finterm/commit/37f5af04) agents(elicit): cross-thread modal bridge for MCP elicitation
- [`f2c8d009`](https://github.com/yumima/finterm/commit/f2c8d009) runtime(local): SSE streaming via run_text_stream
- [`fc48604d`](https://github.com/yumima/finterm/commit/fc48604d) **docs:** how-to/ user guides + blocked-item categorisation
- [`f5a7421e`](https://github.com/yumima/finterm/commit/f5a7421e) agents(ctx): wire process-wide default on_sample / on_elicit / on_log
- [`b37772cd`](https://github.com/yumima/finterm/commit/b37772cd) settings(credentials): gut unused keys, point users at the right surfaces
- [`f36caa4e`](https://github.com/yumima/finterm/commit/f36caa4e) mcp(http): OAuth 2.0 + DCR with client_credentials grant (Track 4 #14b)
- [`a46fb477`](https://github.com/yumima/finterm/commit/a46fb477) mcp_bridge: forward prompts/list + prompts/get to the SDK (Track 5 follow-up)
- [`20f4caba`](https://github.com/yumima/finterm/commit/20f4caba) video(spotify): accept Spotify show/episode/playlist embeds
- [`91ad30df`](https://github.com/yumima/finterm/commit/91ad30df) mcp: wire-level prompts/list + prompts/get on McpClientBase
- [`fed8519c`](https://github.com/yumima/finterm/commit/fed8519c) runtime(local): OpenAI-style tool-use loop
- [`901655a9`](https://github.com/yumima/finterm/commit/901655a9) mcp(memory): memory_delete tool
- [`5bc77ca3`](https://github.com/yumima/finterm/commit/5bc77ca3) **docs:** consolidate AI-stack plan + status into one current-state file
- [`4fd77d24`](https://github.com/yumima/finterm/commit/4fd77d24) mcp(memory): local-profile MemoryTools stub (Track 9)
- [`d899e008`](https://github.com/yumima/finterm/commit/d899e008) screens(workbench): single-screen left-nav consolidation (Track 13 full)
- [`1b28bf33`](https://github.com/yumima/finterm/commit/1b28bf33) alpha_arena: flip BaseAgent default to two-runtime path (Track 12 full)
- [`c4db8060`](https://github.com/yumima/finterm/commit/c4db8060) runtime(local): OpenAI-compat local runtime adapter (Track 2)
- [`0ffa338a`](https://github.com/yumima/finterm/commit/0ffa338a) settings(ai-system): share one trace fetch across spend + table (/review fix)
- [`f0225111`](https://github.com/yumima/finterm/commit/f0225111) settings(scheduler): UI for agent_schedule entries (Track 10 finish)

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
   `agent_traces` with prompts, tokens, cost, status.  Per-tool
   kill-switch refuses problem tools system-wide.  Per-agent
   daily USD cap refuses dispatch before tokens are spent.
   `<untrusted>` wrapping + a system-prompt directive teach the
   model to treat external news / forum text as data.
3. **Local-first switch.**  One profile flip from Anthropic to a
   local OpenAI-compatible server (Ollama, vLLM, llama.cpp, LM
   Studio).  When positions can't leave the box, the local profile
   is *the* differentiator.  No silent failover — you choose.
4. **Deterministic across runtimes.**  Same slash commands,
   same named agents, same SKILL.md methodology, same tool
   catalog on Anthropic or local.  Swap the runtime; the surface
   doesn't move.  What *does* differ — Files API, citations,
   computer use, sub-agents — is laid out in the [capability
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
- **Workspaces & docking** — multi-window Qt-Advanced-Docking with persistable layouts; dock geometry and the last primary screen are restored across launches. `finterm install` registers a desktop launcher.

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
