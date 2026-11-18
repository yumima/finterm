# finterm

A local-first financial-research terminal for your own machine: a native Qt6/C++ desktop app with a thin Python data layer and a built-in AI stack. No account, no cloud backend, no telemetry.

## Today's commits (2026-11-17)

Latest first.

- [`fe868d88a`](https://github.com/yumima/finterm/commit/fe868d88a) economics, calendar: What's On and the World board read their data through owners
- [`f44df6d76`](https://github.com/yumima/finterm/commit/f44df6d76) market data: the options surface and IPO pop quotes go through MarketDataService
- [`17a963840`](https://github.com/yumima/finterm/commit/17a963840) equity research: symbol search and earnings dates are read through their owner
- [`88446df7a`](https://github.com/yumima/finterm/commit/88446df7a) connectors: delete 201 CLI commands no caller sends, and what only they ran
- [`e88a0a4f3`](https://github.com/yumima/finterm/commit/e88a0a4f3) cleanup: close the review's dead-code findings and the unread settings
- [`2a829c612`](https://github.com/yumima/finterm/commit/2a829c612) extended hours: the fetch window follows the market calendar's early close
- [`bc9b9f89e`](https://github.com/yumima/finterm/commit/bc9b9f89e) agno_trading: import what exists; Alpha Arena gets its technical features
- [`3c480977c`](https://github.com/yumima/finterm/commit/3c480977c) extended hours: one reading per session, named everywhere, from the batch quote
- [`5f18576fd`](https://github.com/yumima/finterm/commit/5f18576fd) credentials: regenerate the catalogue; list the keys libraries read
- [`9d3a4bca9`](https://github.com/yumima/finterm/commit/9d3a4bca9) eia, wto: read the API key the panels say they need
- [`952f4d948`](https://github.com/yumima/finterm/commit/952f4d948) python runner: drop the ffn_wrapper venv-routing keyword
- [`5a4af00dc`](https://github.com/yumima/finterm/commit/5a4af00dc) cleanup: delete functions, members, types and constants nothing uses
- [`f6ffcb21d`](https://github.com/yumima/finterm/commit/f6ffcb21d) cleanup: delete classes nothing in the app constructs or names
- [`1c0cf4b2e`](https://github.com/yumima/finterm/commit/1c0cf4b2e) cleanup: delete source files no target compiles or includes
- [`0e9c9b871`](https://github.com/yumima/finterm/commit/0e9c9b871) economic calendar: a failed scrape answers as JSON; a late reply is dropped
- [`44d50943e`](https://github.com/yumima/finterm/commit/44d50943e) economics: restore the analytics engine's methods to their own classes
- [`3fc9e5fe9`](https://github.com/yumima/finterm/commit/3fc9e5fe9) economics: delete the unreachable trend block after analytics_engine's return
- [`8a7cff138`](https://github.com/yumima/finterm/commit/8a7cff138) scripts: delete script actions and CLI commands no caller sends
- [`f87ba8875`](https://github.com/yumima/finterm/commit/f87ba8875) scripts: delete functions, classes and imports nothing calls
- [`ac7495159`](https://github.com/yumima/finterm/commit/ac7495159) economic calendar: fetch the day the panel asks for, not a fixed Oct 16
- [`06a056cc2`](https://github.com/yumima/finterm/commit/06a056cc2) analytics: delete the packages and modules that only a docstring mentions
- [`8e8a5f85c`](https://github.com/yumima/finterm/commit/8e8a5f85c) scripts: delete committed debug output and scratch text files
- [`50c59215b`](https://github.com/yumima/finterm/commit/50c59215b) scripts: delete 278 Python scripts that nothing runs or imports
- [`c30433ca3`](https://github.com/yumima/finterm/commit/c30433ca3) credentials: EDGAR_IDENTITY reaches the scripts (sec_http honours a user-set identity)
- [`dff029d5e`](https://github.com/yumima/finterm/commit/dff029d5e) sec http: every edgartools user gets the contact identity from one helper
- [`738690969`](https://github.com/yumima/finterm/commit/738690969) sec http: one retry policy, a deadline, a byte cap; edgartools paced by the gate
- [`7bb40ed94`](https://github.com/yumima/finterm/commit/7bb40ed94) sec directory: the C++ ticker -> CIK map reads the app's one copy
- [`cb0dddcc5`](https://github.com/yumima/finterm/commit/cb0dddcc5) storage: Equity Research and Economics keep one store, cache.db
- [`29be76870`](https://github.com/yumima/finterm/commit/29be76870) whats on: the earnings feed calls ipo_calendar's Nasdaq client by its new signature
- [`18796b452`](https://github.com/yumima/finterm/commit/18796b452) sec http: every EDGAR request in the scripts goes through sec_http
- [`428c6ac81`](https://github.com/yumima/finterm/commit/428c6ac81) geolocation, whats on: per-call answers, one failure text, narrow yields window
- [`8a45bce42`](https://github.com/yumima/finterm/commit/8a45bce42) data owners: review fixes — real ages, refusals not cached, one fan-out, one bridge
- [`ad32e62a2`](https://github.com/yumima/finterm/commit/ad32e62a2) ownership: alert reads cannot wedge, refusals are failures, one insider-buy type
- [`5f621d89f`](https://github.com/yumima/finterm/commit/5f621d89f) sec directory: one copy of SEC's ticker list; one SEC HTTP client begins
- [`2b68642f4`](https://github.com/yumima/finterm/commit/2b68642f4) extended hours: review fixes — per-row labels, strict close order, closures from data
- [`2e20e3788`](https://github.com/yumima/finterm/commit/2e20e3788) workflow nodes: read every dataset through the service that owns it
- [`9fb509452`](https://github.com/yumima/finterm/commit/9fb509452) data owners: callback reads for FRED, financials, news, Form 4, filings, option chains
- [`7d974777c`](https://github.com/yumima/finterm/commit/7d974777c) whats on: Treasury yields come from EconomicsService, not a direct script run
- [`d96b5cae5`](https://github.com/yumima/finterm/commit/d96b5cae5) geolocation: NewsNlpService runs news_geolocation.py; Geopolitics goes through it
- [`e493f7e22`](https://github.com/yumima/finterm/commit/e493f7e22) storage: one store per dataset; retired storage cleaned by its owner
- [`d6bb1f3dc`](https://github.com/yumima/finterm/commit/d6bb1f3dc) alerts: ask OwnershipService for clusters, stakes, 13F moves and fund filings instead of running the scripts
- [`1199ac031`](https://github.com/yumima/finterm/commit/1199ac031) extended hours: the AFT% column and the ER box read one set of session rules
- [`c5055aea8`](https://github.com/yumima/finterm/commit/c5055aea8) info: one company-info read; shared reads always answer later
- [`eba46712d`](https://github.com/yumima/finterm/commit/eba46712d) history: one price-history read for the whole app
- [`80b23b584`](https://github.com/yumima/finterm/commit/80b23b584) quote: Equity Research reads the app's one market quote
- [`d316dcb45`](https://github.com/yumima/finterm/commit/d316dcb45) movers: one hub topic for every screen; quotes say when they fail
- [`76e7cd2d8`](https://github.com/yumima/finterm/commit/76e7cd2d8) earnings calendar: one feed for the dashboard tile and What's On
- [`8d4383a7d`](https://github.com/yumima/finterm/commit/8d4383a7d) release notes: 4.0.9 adds one source for after-hours, IPO and earnings calendars
- [`cee256ecc`](https://github.com/yumima/finterm/commit/cee256ecc) earnings calendar: the dashboard tile reads the same script as What's On
- [`447643ef3`](https://github.com/yumima/finterm/commit/447643ef3) ipo calendar: one feed runs it; Private Market, What's On and the dashboard tile read it
- [`b5f488f6c`](https://github.com/yumima/finterm/commit/b5f488f6c) extended hours: one source for every AFT surface; a refusal is not a decline
- [`616afbb13`](https://github.com/yumima/finterm/commit/616afbb13) release: 4.0.9
- [`d9df6c999`](https://github.com/yumima/finterm/commit/d9df6c999) earnings links: the SEC contact goes to sec.gov hosts only, redirects included
- [`516a812b3`](https://github.com/yumima/finterm/commit/516a812b3) performance: back to a split in 0.1 s, the IPO calendar whole in a second
- [`22f4a9d04`](https://github.com/yumima/finterm/commit/22f4a9d04) earnings call player: actually paints on hybrid-GPU Linux; a call link outlives the host's feed
- [`637f4d6a7`](https://github.com/yumima/finterm/commit/637f4d6a7) earnings: ▶ LISTEN plays every call in finterm — host's own player when there's no public stream
- [`88281d0bf`](https://github.com/yumima/finterm/commit/88281d0bf) earnings: the current print's call + release, listened to in-app, refreshed when it lands
- [`6349e1e44`](https://github.com/yumima/finterm/commit/6349e1e44) earnings: NEXT REPORT links the call webcast and the last release
- [`df8d943bf`](https://github.com/yumima/finterm/commit/df8d943bf) dashboard: quotes on screen at once — no restyle per show, built before the first click
- [`545ec62aa`](https://github.com/yumima/finterm/commit/545ec62aa) private market: MARKET is a dashboard of movable widgets; canvas never overlaps tiles
- [`c35d27583`](https://github.com/yumima/finterm/commit/c35d27583) private market: IPO Watch becomes PRIVATE MARKET — marketplace prices, rounds, C50, IPO road
- [`a4764b580`](https://github.com/yumima/finterm/commit/a4764b580) reopen as left: the launcher keeps the layout; DAY/AFT, Ownership filters and pane sizes remembered
- [`7aeea32bc`](https://github.com/yumima/finterm/commit/7aeea32bc) research: Extended Hours loads with the same waving bars as the other panels

[See all commits →](https://github.com/yumima/finterm/commits/main)

## Install

<!-- DOWNLOAD-TABLE-START -->
| System | Download | First launch |
|---|---|---|
| **Linux** x86-64 | [AppImage (.run)](https://github.com/yumima/finterm/releases/download/v4.0.8/finterm-4.0.8-linux-x64-setup.run) | `chmod +x finterm-*.run && ./finterm-*.run` |
| **Windows** 10/11 x64 | [Installer (.exe)](https://github.com/yumima/finterm/releases/download/v4.0.8/finterm-4.0.8-windows-x64-setup.exe) | Run it. At SmartScreen: **More info → Run anyway** |
| **macOS** 13+ Apple Silicon | [Disk image (.dmg)](https://github.com/yumima/finterm/releases/download/v4.0.8/finterm-4.0.8-macos-arm64-setup.dmg) | Drag to Applications, then **right-click → Open → Open** |

Current release: [v4.0.8](https://github.com/yumima/finterm/releases/tag/v4.0.8) · [all releases](https://github.com/yumima/finterm/releases)
<!-- DOWNLOAD-TABLE-END -->

### What happens, step by step

**Windows 10/11 (x64)**

1. Run the downloaded `.exe`. At the SmartScreen warning choose **More info → Run anyway**, accept the Windows prompt, and click through the installer. It installs to `C:\Program Files\finterm` and adds Start Menu and Desktop shortcuts.
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
Windows   "C:\Program Files\finterm\finterm.exe" --selftest "%TEMP%\finterm-selftest.json"
macOS     /Applications/finterm.app/Contents/MacOS/finterm --selftest /tmp/finterm-selftest.json
Linux     ./finterm-*.run --selftest /tmp/finterm-selftest.json
```

The report says `"ok": true`, or names the stage that failed (setup or data) and why. Every release is installed and put through this check on all three systems before it is published.

- **Unsigned** — the installers are free, so they carry no paid certificate; that is the one extra click on Windows and macOS. They are built from this source by the public CI.
- **Updates** — **Help › Check for Updates**. finterm never checks on its own.

## What's inside

- **Markets, Watchlist, Dashboard** — live global quotes, charts, sector heatmap, extended-hours and futures moves.
- **News** — a deduplicated feed with region and portfolio filters, reader-mode articles, an AI TL;DR, and read-aloud.
- **Futures** — index, rates, energy, metals, grains, softs, livestock, FX and crypto futures: the forward curve of every listed month against a week and a month ago, calendar-spread history and roll yield, CFTC positioning, contract terms, and a paper ticket sized in dollars ([guide](docs/how-to/futures.md)).
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
