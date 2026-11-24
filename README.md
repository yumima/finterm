# finterm

A local-first financial-research terminal for your own machine: a native Qt6/C++ desktop app with a thin Python data layer and a built-in AI stack. No account, no cloud backend, no telemetry.

## Today's commits (2026-11-24)

Latest first.

- [`add66765a`](https://github.com/yumima/finterm/commit/add66765a) hearth in finterm, news: what the second review found
- [`b963a56e2`](https://github.com/yumima/finterm/commit/b963a56e2) hearth in finterm: one walk over a config's models, tested
- [`59b5441da`](https://github.com/yumima/finterm/commit/59b5441da) hearth in finterm: what the review found
- [`9da8360f9`](https://github.com/yumima/finterm/commit/9da8360f9) Get hearth: run hearth's own installer, the one this finterm knows
- [`f6a80eaea`](https://github.com/yumima/finterm/commit/f6a80eaea) hearth: a busy engine is not a dead one; its own voice folder
- [`57fb47f85`](https://github.com/yumima/finterm/commit/57fb47f85) hearth: finterm finds it, asks once, connects and disconnects
- [`029e6c7d4`](https://github.com/yumima/finterm/commit/029e6c7d4) hearth: no web search unless the user turns it on; one address for it; uv is verified
- [`6d771b282`](https://github.com/yumima/finterm/commit/6d771b282) review: a draft is not a decision; a cell's output grows in place; user code has slots of its own
- [`8add4d09c`](https://github.com/yumima/finterm/commit/8add4d09c) alpha arena: a model on hearth is not given a web search the others don't have
- [`1f0c6dcde`](https://github.com/yumima/finterm/commit/1f0c6dcde) alpha arena, notebook: what the review found
- [`18807b311`](https://github.com/yumima/finterm/commit/18807b311) alpha arena: portfolios come from the fills; no answer is no decision; the tabs show what there is
- [`307e860ab`](https://github.com/yumima/finterm/commit/307e860ab) updater: a version released again is offered; About and the download page name the build
- [`da59ae310`](https://github.com/yumima/finterm/commit/da59ae310) notebook: its cells run in one Python of their own; a restored tab no longer saves over its file
- [`c62926437`](https://github.com/yumima/finterm/commit/c62926437) **chore(release):** update README download links and updates.json for v4.1.0
- [`023279470`](https://github.com/yumima/finterm/commit/023279470) review: a resumed RD-Agent job does its work; a key goes to its own endpoint only; the panel says what is so
- [`a959f384e`](https://github.com/yumima/finterm/commit/a959f384e) voice: the app opens the microphone; nothing is installed by hand
- [`4ae5fa611`](https://github.com/yumima/finterm/commit/4ae5fa611) agents: their skills' text reaches them; a local model that only thinks is asked not to
- [`5083d9593`](https://github.com/yumima/finterm/commit/5083d9593) rd-agent: jobs run in a process the app keeps; the MCP server serves finterm's own tools
- [`3191ad542`](https://github.com/yumima/finterm/commit/3191ad542) deep agent: it runs, with the chosen model and finterm's own read-only tools
- [`29bad85d1`](https://github.com/yumima/finterm/commit/29bad85d1) python: a script's arguments never reach the command line; long jobs have slots of their own
- [`cfba7bb81`](https://github.com/yumima/finterm/commit/cfba7bb81) ai: an id that reads as a huge exponent no longer fails the whole answer
- [`ac7003051`](https://github.com/yumima/finterm/commit/ac7003051) site: the download page says when the files went up, next to the version

[See all commits →](https://github.com/yumima/finterm/commits/main)

## Install

<!-- DOWNLOAD-TABLE-START -->
| System | Download | First launch |
|---|---|---|
| **Linux** x86-64 | [AppImage (.run)](https://github.com/yumima/finterm/releases/download/v4.1.0/finterm-4.1.0-linux-x64-setup.run) | `chmod +x finterm-*.run && ./finterm-*.run` |
| **Windows** 10/11 x64 | [Installer (.exe)](https://github.com/yumima/finterm/releases/download/v4.1.0/finterm-4.1.0-windows-x64-setup.exe) | Run it. At SmartScreen: **More info → Run anyway** |
| **macOS** 13+ Apple Silicon | [Disk image (.dmg)](https://github.com/yumima/finterm/releases/download/v4.1.0/finterm-4.1.0-macos-arm64-setup.dmg) | Drag to Applications, then **right-click → Open → Open** |

Current release: [v4.1.0](https://github.com/yumima/finterm/releases/tag/v4.1.0) · [all releases](https://github.com/yumima/finterm/releases)
<!-- DOWNLOAD-TABLE-END -->

### What happens, step by step

**Windows 10/11 (x64)**

1. Run the downloaded `.exe`. At the SmartScreen warning choose **More info → Run anyway**, and click through the installer — no administrator rights needed. It installs for your user to `%LOCALAPPDATA%\Programs\finterm` and adds Start Menu and Desktop shortcuts. (Have an older finterm in `C:\Program Files\finterm`? Uninstall it first and answer **No** to removing your data — the new one keeps it.)
2. Start **finterm** from the Start Menu.

**macOS 13+ (Apple Silicon)**

1. Open the `.dmg` and drag **finterm** to Applications.
2. The first time, **right-click the app → Open → Open** (or System Settings → Privacy & Security → **Open Anyway**). After that, start it like any app.

**Linux (x86-64)**

1. `chmod +x finterm-*.run`
2. `./finterm-*.run` — it is an AppImage: nothing is installed and no root is needed. On Ubuntu 22.04+ run `sudo apt install libfuse2` once, or start it with `--appimage-extract-and-run`.

**Then, on every system**

1. **First start only: setup.** finterm downloads its own Python and builds its data environment. A progress screen shows it; it takes about 5–15 minutes and needs internet. Nothing else has to be installed, before or after.
2. **Create a local account:** a username and a 4–6 digit PIN, then a 6-digit security PIN that unlocks the terminal on each start. Both stay on your machine and cannot be recovered if forgotten.
3. **The dashboard opens with live data.** finterm starts its own data process in the background and restarts it if it stops; there is nothing else to launch.

Later starts skip setup and go straight to the PIN.

| | Windows | macOS | Linux |
|---|---|---|---|
| Your data | `%LOCALAPPDATA%\finterm` | `~/Library/Application Support/finterm` | `~/.local/share/finterm` |
| Uninstall | Settings → Apps → `io.github.yumima.finterm` | Drag the app to the Bin | Delete the `.run` file |

Uninstalling leaves your data folder in place; delete it too for a clean removal.

**If no data appears,** run the built-in check. Close finterm first. It runs the same setup, starts the data process and fetches one quote, then writes what it found:

```
Windows   "%LOCALAPPDATA%\Programs\finterm\finterm.exe" --selftest "%TEMP%\finterm-selftest.json"
macOS     /Applications/finterm.app/Contents/MacOS/finterm --selftest /tmp/finterm-selftest.json
Linux     ./finterm-*.run --selftest /tmp/finterm-selftest.json
```

The report says `"ok": true`, or names the stage that failed (setup or data) and why. Every release is installed and put through this check on all three systems before it is published.

- **Unsigned** — the installers are free, so they carry no paid certificate; that is the one extra click on Windows and macOS. They are built from this source by the public CI.
- **Updates** — off until you turn them on: finterm asks once whether to check daily (or Help › Check for Updates, any time). A new version downloads in the background and installs when you restart or quit finterm, keeping your workspace and data; a version that fails to start is rolled back.

## What's inside

- **Markets** — one header row of pages, each a canvas of widgets with a chart that follows that page's clicks: your OVERVIEW screens (market conditions per asset class, volatility, the Treasury curve and 10Y–2Y, credit stress, fed funds / CPI / debt-to-GDP / CAPE, cross-asset correlation, sovereign spreads, SPDR sectors, Strategy's premium to its bitcoin), a FUTURES page per group (index, rates, energy, metals, grains, softs, livestock, FX, crypto — the forward curve of every listed month against a week and a month ago, calendar spreads and roll yield, CFTC positioning, contract terms, a paper ticket sized in dollars; [guide](docs/how-to/futures.md)), and WORLD.
- **Watchlist, Dashboard** — live global quotes, charts, sector heatmap, extended-hours moves; every Markets widget can sit on the dashboard too.
- **News** — a deduplicated feed with region and portfolio filters, reader-mode articles, an AI TL;DR, read-aloud, and the **weekly brief**: the week just ended and the one ahead, reviewed and published to the site, posted to X, Bluesky and Mastodon.
- **Equity Research** — financials, earnings (each print's release and call, playable in the app), analyst targets, technicals, peers, ownership and sentiment per ticker, and a **Report** tab that assembles them into a quarterly investor report (charts, segment and geography revenue and the company's own words from its SEC filings, an AI-written summary) exportable as PDF or HTML ([guide](docs/how-to/company-report.md)).
- **Portfolio** — multi-account holdings, performance, heatmap, blotter, CSV/JSON import and export.
- **Screener, Ownership** — screens, and insider (Form 4), institutional (13F) and 5% (13D/13G) views built on public SEC EDGAR filings.
- **Private Market** — private-company marketplace prices, funding rounds and the C50 index on a dashboard of movable widgets, plus the IPO calendar from filing to pricing with the SEC stage each projected date rests on.
- **AI chat** — text or live voice conversation with the provider you choose, cloud or local.
- **Knowledge** — a markets and quant curriculum with calculators and a 47-strategy catalogue.
- **Workspaces** — multi-window docking with saved layouts, and local multi-user profiles behind a PIN.

## AI

Agents, skills and MCP tools work on the same watchlist, portfolio and paper-trade book you do, on Anthropic or on a local OpenAI-compatible server (Ollama, vLLM, llama.cpp, LM Studio). Every run is traced with its prompts, tool calls and cost; per-tool kill-switches and per-agent daily spend caps apply before tokens are spent. No provider is configured by default.

- [AI features](docs/how-to/ai-features.md) — set up a runtime, slash commands, scheduled agents, spend limits
- [Quant features](docs/how-to/quant-features.md) — AI Quant Lab, backtests, paper trading
- [Capability matrix](docs/how-to/capabilities.md) — what works on which runtime

## Privacy

- **Goes out** — only requests to the public data sources you use (Yahoo Finance, SEC EDGAR and the like) and to an LLM provider, if you configure one.
- **Never goes out** — analytics, crash reports, account data. There is no finterm server.
- **Stays local** — accounts, portfolios, keys and settings, under `~/.local/share/finterm/` and `~/.config/finterm/`.

## Build from source

Needs Qt 6.8+, CMake 3.27+, Python 3.11+ and GCC 12 / Clang 15 / MSVC 2022. `setup` installs what is missing, then configures and builds.

```bash
git clone https://github.com/yumima/finterm.git && cd finterm
./finterm.sh setup      # Windows: .\finterm.ps1 setup
./finterm.sh            # launch
```

| Command | What it does |
|---|---|
| `finterm` / `finterm start` | Launch the app |
| `finterm build` | Incremental build (`--clean` rebuilds, `--tests` adds the tests) |
| `finterm repair` | Fix a misbehaving app and keep your data: clears caches and window layout, rebuilds a broken Python environment |
| `finterm reset` | Wipe to a clean install, after a confirmation and a backup |
| `finterm install` / `uninstall` | Add or remove the desktop launcher |
| `finterm status` / `stop` | Show or stop the running app |

`finterm` here is `./finterm.sh` (symlink it onto your `PATH`) or `.\finterm.ps1` on Windows. `repair` and `reset` both snapshot your data to `~/.local/share/finterm-backups/` first. Linux is the regularly tested platform; macOS and Windows build in CI and are less exercised.

Contributing? `git config core.hooksPath .githooks` turns on the commit hooks (changelog above, personal-data guard).

More: [development guide](docs/DEVELOPMENT.md) · [architecture](docs/ARCHITECTURE.md) · [AI stack](plans/ai-stack.md)

## Troubleshooting

| Symptom | Fix |
|---|---|
| Odd windows, stale layout, general flakiness | `finterm repair` |
| Data scripts or Python environment broken | `finterm repair` |
| Quotes stuck or timing out | `pkill -f "yfinance_data.py --daemon"` — the app restarts it |
| Forgot a PIN | Login picker → select the user → **Forgot PIN? Reset this user** (removes that user's data) |

## License

Free and open source under [AGPL-3.0](LICENSE). A community project, not a company product.
