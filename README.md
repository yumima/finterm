# finterm

A local-first financial-research terminal for your own machine: a native Qt6/C++ desktop app with a thin Python data layer and a built-in AI stack. No account, no cloud backend, no telemetry.

## Today's commits (2026-11-23)

Latest first.

- [`52726e0e1`](https://github.com/yumima/finterm/commit/52726e0e1) updater: hardened for release — rollback copy kept, one helper at a time, no retry loop, the Windows rules tested
- [`66a994d3f`](https://github.com/yumima/finterm/commit/66a994d3f) **docs:** 4.1.0 install and update facts — Windows per user, the self-test path, opt-in in-place updates
- [`d2d0d4147`](https://github.com/yumima/finterm/commit/d2d0d4147) release 4.1.0: Windows installs per user; the silent Windows update can't hang
- [`5f240470b`](https://github.com/yumima/finterm/commit/5f240470b) toolbar: no "FIN TERM | PROFESSIONAL RESEARCH DESK" — the row is for what changes
- [`ec9aba233`](https://github.com/yumima/finterm/commit/ec9aba233) window title: "finterm@<host> ver: <version>" — no screen names, no duplicated brand
- [`64ab10847`](https://github.com/yumima/finterm/commit/64ab10847) toolbar: the market's clock beside LIVE — New York time, the session, the countdown to the bell; the local clock labelled
- [`c395d6a42`](https://github.com/yumima/finterm/commit/c395d6a42) updater: in-place updates — download in the background, apply on quit or Restart Now, roll back a version that won't start
- [`c7d457325`](https://github.com/yumima/finterm/commit/c7d457325) agents: the local engine runs without an OpenAI key; disabled agents aren't offered
- [`03912c4e3`](https://github.com/yumima/finterm/commit/03912c4e3) portfolio: a detail view after FFN shows the detail view
- [`b8fe92ab5`](https://github.com/yumima/finterm/commit/b8fe92ab5) banner: the market tape is on every screen, under the menu; the dashboard's header joins its tab row
- [`29a107165`](https://github.com/yumima/finterm/commit/29a107165) theme: the window title bar is the terminal's colour, not Qt's light default
- [`7e646095c`](https://github.com/yumima/finterm/commit/7e646095c) markets: a correlation pair is marked in both triangles
- [`7bb2b4daa`](https://github.com/yumima/finterm/commit/7bb2b4daa) updater: only a release build checks — every other build is "<version>-dev"
- [`c0912fdbe`](https://github.com/yumima/finterm/commit/c0912fdbe) futures: each group's default is the desk's own arrangement — all ten re-taken
- [`fc62eb065`](https://github.com/yumima/finterm/commit/fc62eb065) markets: the correlation cell the chart draws is marked like a charted row
- [`5908b91cd`](https://github.com/yumima/finterm/commit/5908b91cd) updater: a found version survives a restart; review fixes
- [`70d29f8eb`](https://github.com/yumima/finterm/commit/70d29f8eb) updater: opt-in daily check; a found version is a status-bar badge, not a dialog
- [`5e134e35c`](https://github.com/yumima/finterm/commit/5e134e35c) markets: MY VIEW's default is the desk's new arrangement; condition widgets' note says what stands in for CDS / DX / UC
- [`5aea6bc76`](https://github.com/yumima/finterm/commit/5aea6bc76) workspace/dock: a session resume never picks the screen; a tab opens in the main window
- [`fda86588d`](https://github.com/yumima/finterm/commit/fda86588d) tabs: a view is the splitter tree it was left as; redirects keep the view
- [`2f79f05f9`](https://github.com/yumima/finterm/commit/2f79f05f9) tabs: a tab is a view — ER beside Portfolio stays Portfolio's; MARKETS opens on MY VIEW
- [`61ad68b96`](https://github.com/yumima/finterm/commit/61ad68b96) markets: MY VIEW first, WORLD with the market pages; pages keep their rows; the charted row is marked
- [`b0470e92c`](https://github.com/yumima/finterm/commit/b0470e92c) dock: a split is forgotten once one of its panes goes full screen
- [`1c4b935f9`](https://github.com/yumima/finterm/commit/1c4b935f9) **chore(release):** update README download links and updates.json for v4.0.10
- [`265976316`](https://github.com/yumima/finterm/commit/265976316) **build:** Windows' near/far macros, an unused lambda capture (clang -Werror)
- [`9de1c9c23`](https://github.com/yumima/finterm/commit/9de1c9c23) release: 4.0.10
- [`1fec7304f`](https://github.com/yumima/finterm/commit/1fec7304f) news: WEEKLY BRIEF joins What's On — topics, detail, platforms, post now; the SOCIAL tab goes
- [`92f2afc62`](https://github.com/yumima/finterm/commit/92f2afc62) markets: one header row of widget pages — OVERVIEW screens, FUTURES groups, WORLD; futures as widgets

[See all commits →](https://github.com/yumima/finterm/commits/main)

## Install

<!-- DOWNLOAD-TABLE-START -->
| System | Download | First launch |
|---|---|---|
| **Linux** x86-64 | [AppImage (.run)](https://github.com/yumima/finterm/releases/download/v4.0.10/finterm-4.0.10-linux-x64-setup.run) | `chmod +x finterm-*.run && ./finterm-*.run` |
| **Windows** 10/11 x64 | [Installer (.exe)](https://github.com/yumima/finterm/releases/download/v4.0.10/finterm-4.0.10-windows-x64-setup.exe) | Run it. At SmartScreen: **More info → Run anyway** |
| **macOS** 13+ Apple Silicon | [Disk image (.dmg)](https://github.com/yumima/finterm/releases/download/v4.0.10/finterm-4.0.10-macos-arm64-setup.dmg) | Drag to Applications, then **right-click → Open → Open** |

Current release: [v4.0.10](https://github.com/yumima/finterm/releases/tag/v4.0.10) · [all releases](https://github.com/yumima/finterm/releases)
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
