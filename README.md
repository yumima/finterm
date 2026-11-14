# finterm

A local-first, **AI-native**, **offline-capable** financial-research terminal. Qt6/C++ desktop app + a thin Python data layer, with an agent / MCP / skill stack wired into the primary surfaces. No SaaS account, no cloud round-trips, no telemetry — only the public market-data APIs (and the LLM provider) you explicitly use.

## Today's commits (2026-11-13)

Latest first.

- [`60bc20eb9`](https://github.com/yumima/finterm/commit/60bc20eb9) social: SOCIAL screen — publish the weekly brief by hand, with confirmation
- [`263b40d11`](https://github.com/yumima/finterm/commit/263b40d11) brief: X posts carry one cashtag (X refuses more)
- [`c087d3807`](https://github.com/yumima/finterm/commit/c087d3807) social: shared posting backend for an in-app Social screen; publishes issue.json
- [`78e578f88`](https://github.com/yumima/finterm/commit/78e578f88) **ui:** nine screenshot bugs fixed; News gets a WHAT'S ON panel; chat panel resizable
- [`6362065e8`](https://github.com/yumima/finterm/commit/6362065e8) brief: weekly insider-buying site + X announcements; Form 4 scan stops writing off refusals
- [`b5751017e`](https://github.com/yumima/finterm/commit/b5751017e) **ci:** clean under clang and MSVC too; garbled UTF-8 labels fixed everywhere
- [`259f40edb`](https://github.com/yumima/finterm/commit/259f40edb) **ci:** compile without Qt WebEngine; standard __VA_OPT__; wincred include order
- [`ea9ff379a`](https://github.com/yumima/finterm/commit/ea9ff379a) **ci:** builds clean under -Werror again; Windows IP-helper include order
- [`645fdc12c`](https://github.com/yumima/finterm/commit/645fdc12c) screens: every screen listed again — the hiding switch is gone
- [`243296cc0`](https://github.com/yumima/finterm/commit/243296cc0) install: free installers documented per platform; updates only on request
- [`1d43e33fd`](https://github.com/yumima/finterm/commit/1d43e33fd) tooltips: bounded everywhere, and cut to what a reader needs
- [`0191b5b7e`](https://github.com/yumima/finterm/commit/0191b5b7e) data: your own price source — Tiingo, Polygon/Massive, Alpaca, Finnhub, FMP
- [`07c4255e7`](https://github.com/yumima/finterm/commit/07c4255e7) news: drawer headlines shrink again when the drawer narrows
- [`d195c3201`](https://github.com/yumima/finterm/commit/d195c3201) rename: fincept-qt/ → app/
- [`3c2b0e3c8`](https://github.com/yumima/finterm/commit/3c2b0e3c8) rename: product names, IDs and file names off "fincept"
- [`ca8b5235f`](https://github.com/yumima/finterm/commit/ca8b5235f) tools: weekly brief — insider-buying clusters and the IPO pipeline as Markdown
- [`4dabec3dc`](https://github.com/yumima/finterm/commit/4dabec3dc) ipo: a $10.00 unit ticker marks a SPAC even when EDGAR's SIC isn't 6770
- [`4587eabbf`](https://github.com/yumima/finterm/commit/4587eabbf) rename: FINCEPT_* macros, build options and env vars → FINTERM_*
- [`0f6918c80`](https://github.com/yumima/finterm/commit/0f6918c80) rename: C++ namespace fincept → finterm
- [`20260a553`](https://github.com/yumima/finterm/commit/20260a553) trim: experimental screens hidden by default; upstream strategy fixtures removed
- [`a08b118ee`](https://github.com/yumima/finterm/commit/a08b118ee) portfolio: import holdings from a broker CSV export
- [`225e7b1cc`](https://github.com/yumima/finterm/commit/225e7b1cc) account: local-only — the upstream SaaS account layer is gone
- [`09d9de90f`](https://github.com/yumima/finterm/commit/09d9de90f) remove dead upstream cloud features: Forum, Support, hosted LLM provider
- [`42b960aee`](https://github.com/yumima/finterm/commit/42b960aee) identity: data, settings and keyring move off the old com.fincept names
- [`e5c33d835`](https://github.com/yumima/finterm/commit/e5c33d835) license: full AGPL text, NOTICE carries the one required attribution
- [`5dd3c1183`](https://github.com/yumima/finterm/commit/5dd3c1183) chat: untagged sessions bind '' not NULL — bubble "Session error" fixed

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
- **Pre-IPO** — private-market cockpit fed by public SEC EDGAR sources (Form D, S-1, mutual-fund N-PORT marks): picks, screener, company dossiers, IPO pipeline, and a LOCKUPS lens for post-IPO supply-surge signals (Finnhub).
- **Knowledge** — markets and quant curriculum with practice tools, formula calculators, and an embedded AI tutor. The quant catalogue ships 47 strategy entries (FX, factor, FI, vol, dispersion, macro, …) with intuition, math, and citations.
- **Live video** — embedded Bloomberg / CNBC HLS streams with pause/play, auto-pause on terminal lock, and user-pickable max resolution (480/720/1080).
- **AI chat bubble** — pluggable LLM provider; none wired by default.
- **Concurrent yfinance daemon** — long-lived Python child runs market-data fetches in parallel over a Unix domain socket; interactive requests are prioritised over background sweeps. A local `QueryStore` (SWR + LRU) fronts it for the ER tabs.
- **Workspaces & docking** — multi-window Qt-Advanced-Docking with persistable layouts; dock geometry and the last primary screen are restored across launches. `finterm install` adds a desktop launcher.

## Data flow

finterm is **data-in only**:

- **Outbound, on your behalf** — Yahoo Finance, akshare. Quotes/history flow *into* the app.
- **Outbound, never** — no analytics, no crash reporter, no SaaS backend, no LLM unless *you* configure one in Settings → Agent.
- **Local only** — auth/profile/session/passcode handled entirely in-process by `AuthService` (C++, backed by `auth.db`). Nothing leaves the machine.
- **Local only** — everything else lives in `~/.local/share/finterm/` (SQLite + JSON; everything personal under its `user/`) and `~/.config/finterm/`.

## Architecture

```
finterm/
├── finterm.sh                      ← one-stop CLI (Linux/macOS): setup / start / build / repair / reset / install / …
├── finterm.ps1                     ← same CLI for native Windows (PowerShell)
├── setup.sh                        ← deprecated shim → `finterm.sh setup`
├── app/                     ← Qt6/C++ application
│   ├── src/auth/AuthService.cpp    ← in-process auth (users stored in auth.db)
│   ├── src/                        ← C++ source
│   ├── scripts/                    ← Python data layer (yfinance, akshare, …)
│   ├── resources/knowledge/        ← KNOWLEDGE tab content (markdown + JSON manifests)
│   └── build/<preset>/             ← compiled binary lands here
└── tools/
    └── update_todays_commits.py    ← README changelog helper (used by the pre-commit hook)
```

One process runs when you launch:

1. **finterm binary** — the Qt app. Auth/profile/session run in-process via `AuthService` (no stub server). Spawns `yfinance_data.py --daemon --socket <path>` as a Python child for all market-data work; communicates via a Unix domain socket at `~/.local/share/finterm/runtime/yfinance.sock`. The daemon runs a `ThreadPoolExecutor` internally so multiple yfinance fetches execute concurrently.

Nothing talks to anything outside `localhost` except the data scripts and any LLM provider you explicitly configure.

## Install

Download the file for your system from the [latest release](https://github.com/yumima/finterm/releases/latest). Everything is free — including the installers, which is why they aren't signed with a paid certificate and the first launch needs one extra click.

| System | File | First launch |
|---|---|---|
| **Linux** (x86-64) | `finterm-<version>-linux-x64-setup.run` — an **AppImage**: one self-contained file, no root, no install, any mainstream distro | `chmod +x finterm-*.run && ./finterm-*.run`. On Ubuntu 22.04+ install FUSE 2 once: `sudo apt install libfuse2` (or add `--appimage-extract-and-run`). |
| **Windows** 10/11 (x64) | `finterm-<version>-windows-x64-setup.exe` | Run it. If SmartScreen says *"Windows protected your PC"*: **More info → Run anyway**. |
| **macOS** 13+ (Apple Silicon) | `finterm-<version>-macos-arm64-setup.dmg` | Open, drag **finterm** to Applications. The first time, **right-click the app → Open → Open** (or System Settings → Privacy & Security → **Open Anyway**). |

The first start sets up finterm's Python environment — about 5–15 minutes, once. Updates: **Help › Check for Updates** (finterm never checks on its own).

Why AppImage on Linux: it runs unchanged across distributions and needs no package manager, store review or root. A Flatpak's sandbox would get in the way of what finterm does by design — its own Python environments, the desktop keyring for API keys, and a local AI engine.

Why the extra click: Windows and macOS trust installers signed with a certificate that costs money every year (Apple's notarization requires a paid developer membership). The installers are built in this repository's public CI from this source; the click is the only difference.

## Build from source

### Platform support

Linux is regularly tested (Ubuntu / Debian-likes, x86-64). macOS and Windows build but are less exercised — expect minor friction. `finterm.sh` drives Linux + macOS; **`finterm.ps1`** is the native-Windows equivalent (same commands).

### Prerequisites

- Qt 6.8+ — the **system** Qt (distro `qt6-*` packages, or Homebrew `qt` on macOS); `finterm setup` installs it. Not vendored into the tree.
- CMake 3.27+, GCC 12.3+ / Clang 15+ / MSVC 2022.
- Python 3.11+ (for the data scripts; no stub server needed).
- A separate venv for the data scripts — `finterm setup` provisions it under `~/.local/share/finterm/`. ~110 packages, ~5–15 minutes the first time.

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
| `finterm build --tests` | Configure with `-DFINTERM_BUILD_TESTS=ON`. |
| `finterm repair` | **Fix a misbehaving app, keeping all your data.** Clears caches + transient state + stale window/dock layout, and rebuilds the Python venv only if a health check finds it broken. Keeps portfolio, API keys, GUI layout, data-source, workspaces. Takes an insurance snapshot first. |
| `finterm reset` | **Wipe everything to a clean install.** Confirms first (`-y` skips), snapshots your data to `~/.local/share/finterm-backups/` and prints copy-back commands, then clears config + share. |
| `finterm install` | Create a `.desktop` launcher (pinnable to your dock). |
| `finterm uninstall` | Remove the `.desktop` launcher. |
| `finterm stop` | `pkill` the Qt binary. |
| `finterm status` | Print what's running (Qt binary + data daemon). |

When something's wrong, reach for `finterm repair` — it fixes the common breakages (stale cache, corrupt layout, broken venv) without ever losing your portfolio, keys, or settings. Use `finterm reset` only when you want a genuinely clean install.

What `finterm start` does:

1. **Strips Qt window/dock layout state** from `~/.config/finterm/finterm.conf`. Surgical: portfolio/watchlists/theme/workspaces preserved. Skip with `FINTERM_KEEP_WINDOW=1 finterm`.
2. **Execs the Qt binary in the foreground.** Closing the window returns control to your shell.

### Auth & user accounts

Local-only multi-user, no email, no SaaS round-trip. Auth runs entirely in-process via `AuthService` (C++); users and sessions are stored in `~/.local/share/finterm/user/data/auth.db`. Each user's portfolio, watchlists, and settings live in their own isolated `users/<username>.db`.

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

`finterm repair` covers ~95% of breakage — it clears caches/transient state and rebuilds a broken venv while keeping your portfolio, API keys, GUI layout, and workspaces. Use `finterm reset` only for a genuinely clean install. Both snapshot your data to `~/.local/share/finterm-backups/` first (printed path), so nothing is lost. Portfolio data lives in SQLite under `~/.local/share/finterm/user/data/`.

| Symptom | Try |
|---|---|
| Two floating empty windows on launch / PIN screen behind another | `finterm repair` (resets the window layout, keeps your data) |
| Stuck on a weird theme / layout, stale cache, or general flakiness | `finterm repair` |
| Python venv / data scripts broken (e.g. "Install Analytics Libraries" keeps reappearing) | `finterm repair` — it rebuilds the venv if a health check finds it broken |
| Want a genuinely clean install | `finterm reset` (snapshots your data first) |
| Daemon hung / Yahoo timing out | `pkill -f "yfinance_data.py --daemon"` — the Qt app respawns it automatically. |
| Stale socket file after crash | `rm ~/.local/share/finterm/runtime/yfinance.sock` — cleared automatically on next launch too. |
| Auth DB locked / corrupt | Delete `~/.local/share/finterm/user/data/auth.db` and restart — every local user disappears; sign up again. |
| Forgot a user's PIN | Login picker → select the user → **Forgot PIN? Reset this user**. Per-user DB is removed too. |

## License

finterm is free, open-source software released under **AGPL-3.0** (see [`LICENSE`](LICENSE)) — free to download, use, modify, and redistribute under that license. It is a community project, not a company product.

finterm is a fully-local financial terminal: no email signup, no OTP, no paywall, no SaaS round-trip. It uses a local username + PIN multi-user picker and ships the KNOWLEDGE (with QUANT) tab, the after-hours Portfolio heatmap mode, the 2-column News feed with PTF / TL;DR, a 47-entry quant strategy catalogue, and the various local-first conveniences described above.
